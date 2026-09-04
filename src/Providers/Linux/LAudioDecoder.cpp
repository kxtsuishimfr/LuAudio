#include <LuAudio/Common.h>

#if defined(__linux__)

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <LuAudio/Providers/Linux/LAudioDecoder.h>

namespace LuAudio::Providers::Linux {

namespace {

Audio::Result Failure(Audio::ResultCode code, const char* message) { return Audio::Result::Failure(code, message); }

void LinkDecodedPad(GstElement*, GstPad* pad, gpointer userData)
{
    auto* convert = GST_ELEMENT(userData);
    GstPad* sink = gst_element_get_static_pad(convert, "sink");
    if (!gst_pad_is_linked(sink)) gst_pad_link(pad, sink);
    gst_object_unref(sink);
}

}

class LAudioDecoder::Implementation {
public:
    Implementation()
    {
        static std::once_flag initialized;
        std::call_once(initialized, [] { gst_init(nullptr, nullptr); });
    }

    ~Implementation() { Close(); }

    Audio::Result Open(const Audio::AudioFile& file, Audio::DecoderInfo& destination)
    {
        destination = {};
        Close();
        if (!file.IsValid()) {
            return Failure(Audio::ResultCode::InvalidArgument, "Audio file path is empty");
        }
        GError* error = nullptr;
        gchar* uri = g_filename_to_uri(file.Path().c_str(), nullptr, &error);
        if (uri == nullptr) {
            if (error != nullptr) g_error_free(error);
            return Failure(Audio::ResultCode::InvalidArgument, "Unable to create Linux media URI");
        }
        pipeline_ = gst_pipeline_new("luaudio-decoder");
        auto* source = gst_element_factory_make("uridecodebin", "source");
        auto* convert = gst_element_factory_make("audioconvert", "convert");
        auto* resample = gst_element_factory_make("audioresample", "resample");
        sink_ = gst_element_factory_make("appsink", "sink");
        if (pipeline_ == nullptr || source == nullptr || convert == nullptr || resample == nullptr || sink_ == nullptr) {
            g_free(uri);
            Close();
            return Failure(Audio::ResultCode::BackendUnavailable, "Required GStreamer plugins are unavailable");
        }
        g_object_set(source, "uri", uri, nullptr);
        g_free(uri);
        auto* caps = gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, "F32LE",
            "layout", G_TYPE_STRING, "interleaved", nullptr);
        g_object_set(sink_, "caps", caps, "sync", FALSE, "max-buffers", 4, "drop", FALSE, nullptr);
        gst_caps_unref(caps);
        gst_bin_add_many(GST_BIN(pipeline_), source, convert, resample, sink_, nullptr);
        if (!gst_element_link_many(convert, resample, sink_, nullptr)) {
            Close();
            return Failure(Audio::ResultCode::BackendUnavailable, "Unable to configure GStreamer audio conversion");
        }
        g_signal_connect(source, "pad-added", G_CALLBACK(LinkDecodedPad), convert);
        if (gst_element_set_state(pipeline_, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE ||
            gst_element_get_state(pipeline_, nullptr, nullptr, 5 * GST_SECOND) == GST_STATE_CHANGE_FAILURE ||
            !ReadCaps()) {
            Close();
            return Failure(Audio::ResultCode::ProcessingFailed, "Unable to open Linux audio stream");
        }
        gint64 duration = GST_CLOCK_TIME_NONE;
        if (gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &duration) && duration >= 0)
            frameCount_ = gst_util_uint64_scale(static_cast<guint64>(duration), sampleRate_, GST_SECOND);
        if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            Close();
            return Failure(Audio::ResultCode::BackendUnavailable, "Unable to start GStreamer decoder");
        }
        destination.format = {sampleRate_, channelCount_, Audio::SampleType::Float32,
            Audio::ChannelLayout::Interleaved};
        destination.frameCount = frameCount_;
        open_ = true;
        return Audio::Result::Success();
    }

    Audio::Result Read(std::vector<float>& destination, std::size_t maxFrames, std::size_t& framesRead)
    {
        destination.clear();
        framesRead = 0;
        if (!open_) {
            return Failure(Audio::ResultCode::InvalidState, "Linux audio decoder is not open");
        }
        destination.reserve(maxFrames * channelCount_);
        while (framesRead < maxFrames) {
            if (pendingOffset_ < pending_.size()) {
                const auto available = (pending_.size() - pendingOffset_) / channelCount_;
                const auto count = std::min(maxFrames - framesRead, available);
                destination.insert(destination.end(), pending_.begin() + pendingOffset_,
                    pending_.begin() + pendingOffset_ + count * channelCount_);
                pendingOffset_ += count * channelCount_;
                framesRead += count;
                position_ += count;
                continue;
            }
            pending_.clear();
            pendingOffset_ = 0;
            if (endOfFile_) {
                break;
            }

            GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink_));
            if (sample == nullptr) { endOfFile_ = true; break; }
            GstBuffer* buffer = gst_sample_get_buffer(sample);
            GstMapInfo map{};
            if (buffer == nullptr || !gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                gst_sample_unref(sample);
                return Failure(Audio::ResultCode::ProcessingFailed, "Unable to read GStreamer audio buffer");
            }
            const auto sampleCount = map.size / sizeof(float);
            pending_.assign(reinterpret_cast<const float*>(map.data),
                reinterpret_cast<const float*>(map.data) + sampleCount);
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
        }
        return Audio::Result::Success();
    }

    Audio::Result Seek(std::uint64_t frame)
    {
        if (!open_) {
            return Failure(Audio::ResultCode::InvalidState, "Linux audio decoder is not open");
        }
        const auto timestamp = gst_util_uint64_scale(frame, GST_SECOND, sampleRate_);
        if (!gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), timestamp)) {
            return Failure(Audio::ResultCode::InvalidArgument, "Linux audio seek failed");
        }
        pending_.clear();
        pendingOffset_ = 0;
        position_ = frame;
        endOfFile_ = false;
        return Audio::Result::Success();
    }

    bool EndOfFile() const noexcept
    {
        return endOfFile_ && pendingOffset_ >= pending_.size();
    }

private:
    void Close() noexcept
    {
        if (pipeline_ != nullptr) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
            sink_ = nullptr;
        }
        pending_.clear();
        pendingOffset_ = 0;
        sampleRate_ = 0;
        channelCount_ = 0;
        frameCount_ = 0;
        position_ = 0;
        open_ = false;
        endOfFile_ = false;
    }

    bool ReadCaps()
    {
        GstPad* pad = gst_element_get_static_pad(sink_, "sink");
        GstCaps* caps = gst_pad_get_current_caps(pad);
        gst_object_unref(pad);
        if (caps == nullptr || gst_caps_is_empty(caps)) {
            if (caps != nullptr) gst_caps_unref(caps);
            return false;
        }
        const auto* structure = gst_caps_get_structure(caps, 0);
        gint rate = 0;
        gint channels = 0;
        const bool valid = gst_structure_get_int(structure, "rate", &rate) &&
            gst_structure_get_int(structure, "channels", &channels) && rate > 0 && channels > 0;
        if (valid) {
            sampleRate_ = static_cast<std::uint32_t>(rate);
            channelCount_ = static_cast<std::uint32_t>(channels);
        }
        gst_caps_unref(caps);
        return valid;
    }

    GstElement* pipeline_ = nullptr;
    GstElement* sink_ = nullptr;
    std::uint32_t sampleRate_ = 0;
    std::uint32_t channelCount_ = 0;
    std::uint64_t frameCount_ = 0;
    std::uint64_t position_ = 0;
    std::vector<float> pending_;
    std::size_t pendingOffset_ = 0;
    bool open_ = false;
    bool endOfFile_ = false;
};

LAudioDecoder::LAudioDecoder() : implementation_(std::make_unique<Implementation>()) {}
LAudioDecoder::~LAudioDecoder() = default;

Audio::Result LAudioDecoder::Open(const Audio::AudioFile& file, Audio::DecoderInfo& destination)
{
    return implementation_->Open(file, destination);
}
Audio::Result LAudioDecoder::Read(std::vector<float>& destination, std::size_t maxFrames, std::size_t& framesRead)
{
    return implementation_->Read(destination, maxFrames, framesRead);
}
Audio::Result LAudioDecoder::Seek(std::uint64_t frame) { return implementation_->Seek(frame); }
bool LAudioDecoder::EndOfFile() const noexcept { return implementation_->EndOfFile(); }

}

#endif
