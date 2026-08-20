#include <LuAudio/Common.h>

#if defined(__ANDROID__)
#include <oboe/Oboe.h>
#endif

#include <LuAudio/Providers/Android/Oboe/OboeBackend.h>

#if defined(__ANDROID__)
namespace LuAudio::Providers::Android::Oboe {

namespace {

Audio::Result ToResult(oboe::Result result, const char* operation)
{
    if (result == oboe::Result::OK) {
        return Audio::Result::Success();
    }

    const auto code = result == oboe::Result::ErrorIllegalArgument
        ? Audio::ResultCode::InvalidArgument
        : Audio::ResultCode::BackendUnavailable;
    return Audio::Result::Failure(code, std::string(operation) + " failed: " + oboe::convertToText(result));
}

class DataCallback final : public oboe::AudioStreamDataCallback {
public:
    explicit DataCallback(OboeBackend::Implementation& implementation)
        : implementation_(implementation)
    {
    }

    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream*,
        void* audioData,
        int32_t numFrames) override;

private:
    OboeBackend::Implementation& implementation_;
};

class ErrorCallback final : public oboe::AudioStreamErrorCallback {
public:
    bool onError(oboe::AudioStream*, oboe::Result) override
    {
        return false;
    }
};

}

class OboeBackend::Implementation {
public:
    void InitializeCallbacks()
    {
        dataCallback_ = std::make_shared<DataCallback>(*this);
    }

    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig)
    {
        if (!requestedConfig.IsValid()) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidArgument,
                "Invalid Oboe stream configuration");
        }
        if (stream_) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidState,
                "Oboe backend is already open");
        }

        requestedConfig_ = requestedConfig;

        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output)
            ->setFormat(oboe::AudioFormat::Float)
            ->setSampleRate(static_cast<int32_t>(requestedConfig.format.sampleRate))
            ->setChannelCount(static_cast<int32_t>(requestedConfig.format.channelCount))
            ->setFramesPerDataCallback(static_cast<int32_t>(requestedConfig.framesPerBuffer))
            ->setSharingMode(requestedConfig.exclusiveMode
                ? oboe::SharingMode::Exclusive
                : oboe::SharingMode::Shared)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setDataCallback(dataCallback_)
            ->setErrorCallback(errorCallback_);

        const auto result = builder.openStream(stream_);
        if (result != oboe::Result::OK) {
            stream_.reset();
            return ToResult(result, "Opening Oboe stream");
        }

        actualConfig_ = requestedConfig;
        actualConfig_.format.sampleRate = static_cast<std::uint32_t>(stream_->getSampleRate());
        actualConfig_.format.channelCount = static_cast<std::uint32_t>(stream_->getChannelCount());
        buffer_ = Audio::AudioBuffer(actualConfig_.format, requestedConfig.framesPerBuffer);
        return Audio::Result::Success();
    }

    Audio::Result Start()
    {
        if (!stream_) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidState,
                "Oboe backend is not open");
        }
        if (running_) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidState,
                "Oboe backend is already running");
        }

        const auto result = stream_->start();
        if (result != oboe::Result::OK) {
            return ToResult(result, "Starting Oboe stream");
        }
        running_ = true;
        return Audio::Result::Success();
    }

    Audio::Result Recover()
    {
        if (!requestedConfig_.IsValid()) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidState,
                "Oboe backend has no configuration to recover");
        }
        Close();
        return Open(requestedConfig_);
    }

    Audio::Result Stop()
    {
        if (!stream_) {
            return Audio::Result::Failure(
                Audio::ResultCode::InvalidState,
                "Oboe backend is not open");
        }
        if (!running_) {
            return Audio::Result::Success();
        }

        const auto result = stream_->stop();
        if (result != oboe::Result::OK) {
            return ToResult(result, "Stopping Oboe stream");
        }
        running_ = false;
        return Audio::Result::Success();
    }

    void Close() noexcept
    {
        if (stream_) {
            stream_->stop();
            stream_->close();
        }
        stream_.reset();
        running_ = false;
        buffer_ = Audio::AudioBuffer();
        actualConfig_ = Audio::AudioStreamConfig{};
    }

    void SetCallback(Audio::AudioCallback callback)
    {
        callback_ = std::move(callback);
    }

    const Audio::AudioStreamConfig& ActualConfig() const noexcept
    {
        return actualConfig_;
    }

    oboe::DataCallbackResult Render(void* audioData, int32_t numFrames)
    {
        const auto frameCount = std::max<int32_t>(numFrames, 0);
        const auto sampleCount = static_cast<std::size_t>(frameCount)
            * actualConfig_.format.channelCount;
        std::memset(audioData, 0, sampleCount * sizeof(float));

        if (frameCount > static_cast<int32_t>(buffer_.FrameCount())) {
            return oboe::DataCallbackResult::Stop;
        }

        buffer_.Resize(static_cast<std::size_t>(frameCount));
        if (callback_) {
            callback_(buffer_);
            std::memcpy(audioData, buffer_.Data(), sampleCount * sizeof(float));
        }
        return oboe::DataCallbackResult::Continue;
    }

private:
    friend class DataCallback;

    std::shared_ptr<oboe::AudioStream> stream_;
    std::shared_ptr<DataCallback> dataCallback_;
    std::shared_ptr<ErrorCallback> errorCallback_ = std::make_shared<ErrorCallback>();
    Audio::AudioCallback callback_;
    Audio::AudioBuffer buffer_;
    Audio::AudioStreamConfig actualConfig_;
    Audio::AudioStreamConfig requestedConfig_;
    bool running_ = false;
};

oboe::DataCallbackResult DataCallback::onAudioReady(
    oboe::AudioStream*,
    void* audioData,
    int32_t numFrames)
{
    return implementation_.Render(audioData, numFrames);
}

OboeBackend::OboeBackend()
    : implementation_(std::make_unique<Implementation>())
{
    implementation_->InitializeCallbacks();
}

OboeBackend::~OboeBackend() = default;

Audio::Result OboeBackend::Open(const Audio::AudioStreamConfig& requestedConfig)
{
    return implementation_->Open(requestedConfig);
}

Audio::Result OboeBackend::Start()
{
    return implementation_->Start();
}

Audio::Result OboeBackend::Recover()
{
    return implementation_->Recover();
}

Audio::Result OboeBackend::Stop()
{
    return implementation_->Stop();
}

void OboeBackend::Close() noexcept
{
    implementation_->Close();
}

void OboeBackend::SetCallback(Audio::AudioCallback callback)
{
    implementation_->SetCallback(std::move(callback));
}

const Audio::AudioStreamConfig& OboeBackend::ActualConfig() const noexcept
{
    return implementation_->ActualConfig();
}

}
#endif