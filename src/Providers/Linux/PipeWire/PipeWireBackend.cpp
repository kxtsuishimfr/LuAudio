#include <LuAudio/Common.h>

#if defined(__linux__)

#include <LuAudio/Providers/Linux/PipeWire/PipeWireBackend.h>

#include <LuAudio/Utils/Diagnostics/Log.h>

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <pipewire/thread-loop.h>
#include <spa/param/audio/raw.h>
#include <spa/param/audio/raw-utils.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace LuAudio::Providers::Linux::PipeWire {

namespace {

Audio::Result ToResult(const char* action, int status)
{
    if (status == 0) {
        return Audio::Result::Success();
    }

    const auto code = status < 0
        ? Audio::ResultCode::BackendUnavailable
        : Audio::ResultCode::InvalidArgument;
    return Audio::Result::Failure(code, std::string(action) + " failed with status " + std::to_string(status));
}

}

class PipeWireBackend::Implementation {
public:
    Implementation()
    {
        pw_init(nullptr, nullptr);
    }

    ~Implementation()
    {
        Close();
        pw_deinit();
    }

    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig)
    {
        if (!requestedConfig.IsValid()) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidArgument,
                "Invalid PipeWire stream configuration");
        }
        if (stream_ != nullptr) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidState,
                "PipeWire backend is already open");
        }

        requestedConfig_ = requestedConfig;

        mainLoop_ = pw_thread_loop_new("luaudio-pipewire", nullptr);
        if (!mainLoop_) {
            return Audio::Result::Failure(
                Audio::ResultCode::BackendUnavailable,
                "Unable to create PipeWire main loop");
        }

        context_ = pw_context_new(pw_thread_loop_get_loop(mainLoop_), nullptr, 0);
        if (!context_) {
            Close();
            return Audio::Result::Failure(
                Audio::ResultCode::BackendUnavailable,
                "Unable to create PipeWire context");
        }

        if (pw_thread_loop_start(mainLoop_) < 0) {
            Close();
            return Audio::Result::Failure(
                Audio::ResultCode::BackendUnavailable,
                "Unable to start PipeWire thread loop");
        }
        loopStarted_ = true;

        pw_thread_loop_lock(mainLoop_);
        core_ = pw_context_connect(context_, nullptr, 0);
        pw_thread_loop_unlock(mainLoop_);
        if (!core_) {
            Close();
            return Audio::Result::Failure(
                Audio::ResultCode::BackendUnavailable,
                "Unable to connect to PipeWire core");
        }

        const auto sampleRate = static_cast<uint32_t>(requestedConfig.format.sampleRate);
        const auto channelCount = static_cast<uint32_t>(requestedConfig.format.channelCount);

        std::array<uint8_t, 1024> buffer{};
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer.data(), static_cast<uint32_t>(buffer.size()));
        struct spa_audio_info_raw info = SPA_AUDIO_INFO_RAW_INIT(
            .format = SPA_AUDIO_FORMAT_F32,
            .rate = sampleRate,
            .channels = channelCount);

        const spa_pod* params[1] = {
            spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)
        };

        auto* properties = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Music",
            nullptr);
        pw_thread_loop_lock(mainLoop_);
        stream_ = pw_stream_new(core_, "LuAudio PipeWire Output", properties);
        if (!stream_) {
            pw_thread_loop_unlock(mainLoop_);
            Close();
            return Audio::Result::Failure(
                Audio::ResultCode::BackendUnavailable,
                "Unable to create PipeWire stream");
        }

        pw_stream_add_listener(stream_, &streamListener_, StreamEvents(), this);
        pw_thread_loop_unlock(mainLoop_);

        auto flags = static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_INACTIVE |
            PW_STREAM_FLAG_MAP_BUFFERS |
            PW_STREAM_FLAG_RT_PROCESS);
        if (requestedConfig.exclusiveMode) {
            flags = static_cast<pw_stream_flags>(flags | PW_STREAM_FLAG_EXCLUSIVE);
        }

        connectParams_[0] = params[0];
        connectFlags_ = flags;
        pw_thread_loop_lock(mainLoop_);
        const auto connectResult = pw_stream_connect(stream_, PW_DIRECTION_OUTPUT,
            PW_ID_ANY, connectFlags_, connectParams_, 1);
        pw_thread_loop_unlock(mainLoop_);
        if (connectResult < 0) {
            Close();
            return ToResult("Connecting PipeWire stream", connectResult);
        }

        {
            std::unique_lock lock(stateMutex_);
            const auto ready = stateCondition_.wait_for(lock, std::chrono::seconds(5), [this] {
                return streamState_ == PW_STREAM_STATE_ERROR ||
                    streamState_ >= PW_STREAM_STATE_PAUSED;
            });
            if (!ready || streamState_ == PW_STREAM_STATE_ERROR) {
                const auto error = streamError_.empty()
                    ? "PipeWire stream negotiation timed out"
                    : streamError_;
                Close();
                return Audio::Result::Failure(
                    Audio::ResultCode::BackendUnavailable,
                    error);
            }
        }

        if (!actualConfig_.IsValid()) {
            actualConfig_ = requestedConfig;
            actualConfig_.format.sampleRate = sampleRate;
            actualConfig_.format.channelCount = channelCount;
        }
        // Grow-only: AddBuffer() may already have sized buffer_ to the real
        // negotiated buffer capacity during connect/negotiation above. Don't
        // clobber that with a smaller fixed floor, or Render() will end up
        // permanently hitting the "frameCount > buffer_.FrameCount()" silence
        // branch and callback_ will never be invoked.
        const auto minimumFrameCount = std::max<std::size_t>(requestedConfig.framesPerBuffer, 4096);
        if (buffer_.FrameCount() < minimumFrameCount) {
            buffer_ = Audio::AudioBuffer(actualConfig_.format, minimumFrameCount);
        }
        return Audio::Result::Success();
    }

    Audio::Result Start()
    {
        if (stream_ == nullptr) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidState,
                "PipeWire backend is not open");
        }
        if (running_) {
            return Audio::Result::Success();
        }

        if (streamState_ == PW_STREAM_STATE_ERROR) {
            return Audio::Result::Failure(
                Audio::ResultCode::BackendUnavailable,
                streamError_.empty() ? "PipeWire stream is in an error state" : streamError_);
        }
        pw_thread_loop_lock(mainLoop_);
        const auto result = pw_stream_set_active(stream_, true);
        pw_thread_loop_unlock(mainLoop_);
        if (result < 0) {
            return Audio::Result::Failure(
                Audio::ResultCode::BackendUnavailable,
                "Unable to activate PipeWire stream");
        }

        running_ = true;
        return Audio::Result::Success();
    }

    Audio::Result Recover()
    {
        if (!requestedConfig_.IsValid()) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidState,
                "PipeWire backend has no configuration to recover");
        }

        Close();
        return Open(requestedConfig_);
    }

    Audio::Result Stop()
    {
        if (stream_ == nullptr) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidState,
                "PipeWire backend is not open");
        }
        if (!running_) {
            return Audio::Result::Success();
        }

        pw_thread_loop_lock(mainLoop_);
        const auto result = pw_stream_set_active(stream_, false);
        pw_thread_loop_unlock(mainLoop_);
        if (result < 0) {
            return Audio::Result::Failure(
                Audio::ResultCode::BackendUnavailable,
                "Unable to deactivate PipeWire stream");
        }

        running_ = false;
        return Audio::Result::Success();
    }

    void Close() noexcept
    {
        if (mainLoop_ != nullptr && loopStarted_ && pw_thread_loop_in_thread(mainLoop_) == false) {
            pw_thread_loop_lock(mainLoop_);
            if (stream_ != nullptr) {
                pw_stream_disconnect(stream_);
                pw_stream_destroy(stream_);
                stream_ = nullptr;
            }
            if (core_ != nullptr) {
                pw_core_disconnect(core_);
                core_ = nullptr;
            }
            if (context_ != nullptr) {
                pw_context_destroy(context_);
                context_ = nullptr;
            }
            pw_thread_loop_unlock(mainLoop_);
            pw_thread_loop_stop(mainLoop_);
            loopStarted_ = false;
        }

        if (mainLoop_ != nullptr) {
            pw_thread_loop_destroy(mainLoop_);
            mainLoop_ = nullptr;
        }

        running_ = false;
        actualConfig_ = Audio::AudioStreamConfig{};
        buffer_ = Audio::AudioBuffer{};
        {
            std::lock_guard lock(stateMutex_);
            streamState_ = PW_STREAM_STATE_UNCONNECTED;
            streamError_.clear();
        }
    }

    void SetCallback(Audio::AudioCallback callback)
    {
        callback_ = std::move(callback);
    }

    const Audio::AudioStreamConfig& ActualConfig() const noexcept
    {
        return actualConfig_;
    }

private:
    static void StateChanged(void* userdata, enum pw_stream_state, enum pw_stream_state state, const char* error)
    {
        auto* implementation = static_cast<Implementation*>(userdata);
        {
            std::lock_guard lock(implementation->stateMutex_);
            implementation->streamState_ = state;
            implementation->streamError_ = error != nullptr ? error : "";
        }
        implementation->stateCondition_.notify_all();
    }

    static void ParamChanged(void* userdata, uint32_t id, const struct spa_pod* param)
    {
        if (param == nullptr || id != SPA_PARAM_Format) {
            return;
        }

        auto* implementation = static_cast<Implementation*>(userdata);
        struct spa_audio_info_raw info{};
        if (spa_format_audio_raw_parse(param, &info) < 0) {
            return;
        }

        std::lock_guard lock(implementation->stateMutex_);
        implementation->actualConfig_.format.sampleRate = info.rate;
        implementation->actualConfig_.format.channelCount = info.channels;
        implementation->actualConfig_.format.sampleType = Audio::SampleType::Float32;
        implementation->actualConfig_.format.channelLayout = Audio::ChannelLayout::Interleaved;
        implementation->actualConfig_.framesPerBuffer = implementation->requestedConfig_.framesPerBuffer;
        implementation->actualConfig_.exclusiveMode = implementation->requestedConfig_.exclusiveMode;
    }

    static void AddBuffer(void* userdata, struct pw_buffer* buffer)
    {
        auto* implementation = static_cast<Implementation*>(userdata);
        if (buffer == nullptr || buffer->buffer == nullptr || buffer->buffer->datas[0].maxsize == 0) {
            return;
        }

        const auto stride = sizeof(float) * implementation->actualConfig_.format.channelCount;
        const auto frameCapacity = buffer->buffer->datas[0].maxsize / stride;
        if (frameCapacity > implementation->buffer_.FrameCount()) {
            implementation->buffer_ = Audio::AudioBuffer(
                implementation->actualConfig_.format, frameCapacity);
        }
    }

    static const pw_stream_events* StreamEvents()
    {
        static pw_stream_events events{};
        static const bool initialized = [] {
            events.version = PW_VERSION_STREAM_EVENTS;
            events.state_changed = StateChanged;
            events.param_changed = ParamChanged;
            events.add_buffer = AddBuffer;
            events.process = Process;
            return true;
        }();
        (void)initialized;
        return &events;
    }

    static void Process(void* userdata)
    {
        static_cast<Implementation*>(userdata)->Render();
    }

    void Render()
    {
        auto* buffer = pw_stream_dequeue_buffer(stream_);
        if (buffer == nullptr || buffer->buffer == nullptr || buffer->buffer->datas[0].data == nullptr) {
            return;
        }

        auto& data = buffer->buffer->datas[0];
        const auto stride = static_cast<uint32_t>(sizeof(float) * actualConfig_.format.channelCount);
        auto frameCount = data.maxsize / stride;
        if (buffer->requested != 0) {
            frameCount = std::min<std::uint32_t>(frameCount,
                static_cast<std::uint32_t>(std::min<std::uint64_t>(buffer->requested, UINT32_MAX)));
        }

        if (frameCount > buffer_.FrameCount()) {
            std::memset(data.data, 0, data.maxsize);
            data.chunk->offset = 0;
            data.chunk->stride = static_cast<int32_t>(stride);
            data.chunk->size = frameCount * stride;
            pw_stream_queue_buffer(stream_, buffer);
            return;
        }

        buffer_.Resize(frameCount);
        if (callback_) {
            callback_(buffer_);
        } else {
            buffer_.Clear();
        }

        std::memcpy(data.data, buffer_.Data(), buffer_.SampleCount() * sizeof(float));
        data.chunk->offset = 0;
        data.chunk->stride = static_cast<int32_t>(stride);
        data.chunk->size = frameCount * stride;
        pw_stream_queue_buffer(stream_, buffer);
    }

private:
    pw_thread_loop* mainLoop_ = nullptr;
    bool loopStarted_ = false;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    pw_stream* stream_ = nullptr;
    spa_hook streamListener_{};
    Audio::AudioCallback callback_;
    Audio::AudioStreamConfig actualConfig_;
    Audio::AudioStreamConfig requestedConfig_;
    std::mutex stateMutex_;
    std::condition_variable stateCondition_;
    enum pw_stream_state streamState_ = PW_STREAM_STATE_UNCONNECTED;
    std::string streamError_;
    const struct spa_pod* connectParams_[1] = {};
    enum pw_stream_flags connectFlags_ = PW_STREAM_FLAG_NONE;
    Audio::AudioBuffer buffer_;
    bool running_ = false;
};

PipeWireBackend::PipeWireBackend()
    : implementation_(std::make_unique<Implementation>())
{
    Utils::Log::Info("PipeWire backend created");
}

PipeWireBackend::~PipeWireBackend() = default;

Audio::Result PipeWireBackend::Open(const Audio::AudioStreamConfig& requestedConfig)
{
    return implementation_->Open(requestedConfig);
}

Audio::Result PipeWireBackend::Start()
{
    return implementation_->Start();
}

Audio::Result PipeWireBackend::Recover()
{
    return implementation_->Recover();
}

Audio::Result PipeWireBackend::Stop()
{
    return implementation_->Stop();
}

void PipeWireBackend::Close() noexcept
{
    implementation_->Close();
}

void PipeWireBackend::SetCallback(Audio::AudioCallback callback)
{
    implementation_->SetCallback(std::move(callback));
}

const Audio::AudioStreamConfig& PipeWireBackend::ActualConfig() const noexcept
{
    return implementation_->ActualConfig();
}

}

#endif