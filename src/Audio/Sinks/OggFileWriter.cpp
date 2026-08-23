#include <LuAudio/Audio/Sinks/OggFileWriter.h>

#include <vorbis/vorbisenc.h>
#include <ogg/ogg.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>

namespace LuAudio::Audio {

namespace {

Result InvalidFormat(const char* message)
{
    return Result::Failure(ResultCode::InvalidArgument, message);
}

Result ProcessingFailure(const char* message)
{
    return Result::Failure(ResultCode::ProcessingFailed, message);
}

bool WritePage(FILE* file, ogg_page& page)
{
    return std::fwrite(page.header, 1, page.header_len, file) ==
            static_cast<std::size_t>(page.header_len) &&
        std::fwrite(page.body, 1, page.body_len, file) ==
            static_cast<std::size_t>(page.body_len);
}

}

struct OggFileWriter::Impl {
    FILE* file = nullptr;
    vorbis_info info{};
    vorbis_dsp_state dsp{};
    vorbis_block block{};
    vorbis_comment comment{};
    ogg_stream_state stream{};
    bool infoInitialized = false;
    bool dspInitialized = false;
    bool blockInitialized = false;
    bool commentInitialized = false;
    bool streamInitialized = false;

    void Reset() noexcept
    {
        if (file != nullptr) {
            std::fclose(file);
            file = nullptr;
        }
        if (streamInitialized) {
            ogg_stream_clear(&stream);
            streamInitialized = false;
        }
        if (blockInitialized) {
            vorbis_block_clear(&block);
            blockInitialized = false;
        }
        if (dspInitialized) {
            vorbis_dsp_clear(&dsp);
            dspInitialized = false;
        }
        if (commentInitialized) {
            vorbis_comment_clear(&comment);
            commentInitialized = false;
        }
        if (infoInitialized) {
            vorbis_info_clear(&info);
            infoInitialized = false;
        }
    }

    ~Impl()
    {
        Reset();
    }
};

OggFileWriter::OggFileWriter(std::string path)
    : path_(std::move(path)), impl_(std::make_unique<Impl>())
{
}

OggFileWriter::~OggFileWriter()
{
    Abort();
}

Result OggFileWriter::Open(const AudioFormat& format)
{
    Abort();
    if (path_.empty()) {
        return InvalidFormat("Ogg output path is empty");
    }
    if (!format.IsValid() || format.sampleType != SampleType::Float32 ||
        format.channelLayout != ChannelLayout::Interleaved || format.channelCount == 0 ||
        format.channelCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        format.sampleRate > static_cast<std::uint32_t>(std::numeric_limits<long>::max())) {
        return InvalidFormat("Ogg writer requires a valid interleaved Float32 format");
    }

    impl_->file = std::fopen(path_.c_str(), "wb");
    if (impl_->file == nullptr) {
        return ProcessingFailure("Unable to open Ogg output file");
    }
    open_ = true;
    finalized_ = false;

    vorbis_info_init(&impl_->info);
    impl_->infoInitialized = true;
    if (vorbis_encode_init_vbr(&impl_->info, static_cast<long>(format.channelCount),
            static_cast<long>(format.sampleRate), 0.4F) != 0) {
        Abort();
        return ProcessingFailure("Unable to initialize Ogg Vorbis encoder");
    }

    vorbis_comment_init(&impl_->comment);
    impl_->commentInitialized = true;
    vorbis_comment_add_tag(&impl_->comment, const_cast<char*>("ENCODER"),
        const_cast<char*>("LuAudio"));
    if (vorbis_analysis_init(&impl_->dsp, &impl_->info) != 0) {
        Abort();
        return ProcessingFailure("Unable to initialize Ogg Vorbis analysis");
    }
    impl_->dspInitialized = true;
    if (vorbis_block_init(&impl_->dsp, &impl_->block) != 0) {
        Abort();
        return ProcessingFailure("Unable to initialize Ogg Vorbis block");
    }
    impl_->blockInitialized = true;

    std::random_device random;
    if (ogg_stream_init(&impl_->stream, static_cast<int>(random())) != 0) {
        Abort();
        return ProcessingFailure("Unable to initialize Ogg stream");
    }
    impl_->streamInitialized = true;

    ogg_packet header;
    ogg_packet comment;
    ogg_packet codebook;
    if (vorbis_analysis_headerout(&impl_->dsp, &impl_->comment, &header,
            &comment, &codebook) != 0) {
        Abort();
        return ProcessingFailure("Unable to create Ogg Vorbis headers");
    }
    ogg_stream_packetin(&impl_->stream, &header);
    ogg_stream_packetin(&impl_->stream, &comment);
    ogg_stream_packetin(&impl_->stream, &codebook);
    ogg_page page;
    while (ogg_stream_flush(&impl_->stream, &page) != 0) {
        if (!WritePage(impl_->file, page)) {
            Abort();
            return ProcessingFailure("Unable to write Ogg Vorbis headers");
        }
    }

    format_ = format;
    return Result::Success();
}

Result OggFileWriter::Write(const AudioBuffer& buffer)
{
    if (!open_ || finalized_) {
        return Result::Failure(ResultCode::InvalidState, "Ogg output is not open");
    }
    if (buffer.Format().sampleRate != format_.sampleRate ||
        buffer.Format().channelCount != format_.channelCount ||
        buffer.Format().sampleType != format_.sampleType ||
        buffer.Format().channelLayout != format_.channelLayout) {
        return InvalidFormat("Audio buffer format does not match Ogg output");
    }
    for (std::size_t index = 0; index < buffer.SampleCount(); ++index) {
        if (!std::isfinite(buffer.Data()[index])) {
            return InvalidFormat("Ogg output does not accept non-finite samples");
        }
    }

    float** samples = vorbis_analysis_buffer(&impl_->dsp,
        static_cast<int>(buffer.FrameCount()));
    if (samples == nullptr) {
        return ProcessingFailure("Unable to allocate Ogg Vorbis analysis buffer");
    }
    for (std::size_t frame = 0; frame < buffer.FrameCount(); ++frame) {
        for (std::uint32_t channel = 0; channel < format_.channelCount; ++channel) {
            samples[channel][frame] = buffer.Data()[frame * format_.channelCount + channel];
        }
    }
    if (vorbis_analysis_wrote(&impl_->dsp, static_cast<int>(buffer.FrameCount())) != 0) {
        return ProcessingFailure("Unable to submit Ogg Vorbis samples");
    }

    ogg_packet packet;
    ogg_page page;
    while (vorbis_analysis_blockout(&impl_->dsp, &impl_->block) == 1) {
        if (vorbis_analysis(&impl_->block, nullptr) != 0 ||
            vorbis_bitrate_addblock(&impl_->block) != 0) {
            return ProcessingFailure("Unable to analyze Ogg Vorbis samples");
        }
        while (vorbis_bitrate_flushpacket(&impl_->dsp, &packet) != 0) {
            ogg_stream_packetin(&impl_->stream, &packet);
            while (ogg_stream_pageout(&impl_->stream, &page) != 0) {
                if (!WritePage(impl_->file, page)) {
                    return ProcessingFailure("Unable to write Ogg Vorbis samples");
                }
            }
        }
    }
    return Result::Success();
}

Result OggFileWriter::Finalize()
{
    if (!open_ || finalized_) {
        return Result::Failure(ResultCode::InvalidState, "Ogg output is not open");
    }

    if (vorbis_analysis_wrote(&impl_->dsp, 0) != 0) {
        return ProcessingFailure("Unable to finalize Ogg Vorbis analysis");
    }
    ogg_packet packet;
    ogg_page page;
    while (vorbis_analysis_blockout(&impl_->dsp, &impl_->block) == 1) {
        if (vorbis_analysis(&impl_->block, nullptr) != 0 ||
            vorbis_bitrate_addblock(&impl_->block) != 0) {
            return ProcessingFailure("Unable to finalize Ogg Vorbis samples");
        }
        while (vorbis_bitrate_flushpacket(&impl_->dsp, &packet) != 0) {
            ogg_stream_packetin(&impl_->stream, &packet);
            while (ogg_stream_flush(&impl_->stream, &page) != 0) {
                if (!WritePage(impl_->file, page)) {
                    return ProcessingFailure("Unable to finalize Ogg Vorbis file");
                }
            }
        }
    }
    if (std::fflush(impl_->file) != 0) {
        return ProcessingFailure("Unable to flush Ogg output file");
    }
    std::fclose(impl_->file);
    impl_->file = nullptr;
    impl_->Reset();
    open_ = false;
    finalized_ = true;
    return Result::Success();
}

void OggFileWriter::Abort() noexcept
{
    const bool wasOpen = open_;
    if (impl_ != nullptr) {
        impl_->Reset();
    }
    if (wasOpen && !path_.empty()) {
        std::remove(path_.c_str());
    }
    open_ = false;
    finalized_ = false;
}

const AudioFormat& OggFileWriter::Format() const noexcept
{
    return format_;
}

bool OggFileWriter::IsOpen() const noexcept
{
    return open_ && !finalized_;
}

}
