#include <LuAudio/Common.h>
#include <LuAudio/Audio/Sources/OggFileReader.h>

#include <vorbis/vorbisfile.h>

#include <limits>

namespace LuAudio::Audio {
namespace {

constexpr std::size_t kDecodeChunkFrames = 4096;
constexpr std::size_t kBufferedChunks = 8;

class VorbisDecoder final : public IAudioDecoder {
public:
    ~VorbisDecoder() override { Close(); }

    Result Open(const AudioFile& file, DecoderInfo& destination) override
    {
        Close();
        if (!file.IsValid() || file.Type() != AudioFileType::Ogg) {
            return Result::Failure(ResultCode::InvalidArgument, "Audio file is not Ogg");
        }
        if (ov_fopen(file.Path().c_str(), &stream_) != 0) {
            return Result::Failure(ResultCode::ProcessingFailed,
                "Unable to open Ogg Vorbis file");
        }
        open_ = true;
        const vorbis_info* info = ov_info(&stream_, -1);
        const long long frameCount = ov_pcm_total(&stream_, -1);
        if (info == nullptr || info->rate <= 0 || info->channels <= 0 || frameCount < 0 ||
            static_cast<unsigned long long>(info->rate) > std::numeric_limits<std::uint32_t>::max() ||
            static_cast<unsigned long long>(info->channels) > std::numeric_limits<std::uint32_t>::max()) {
            Close();
            return Result::Failure(ResultCode::ProcessingFailed,
                "Ogg stream has invalid Vorbis metadata");
        }
        destination.format = AudioFormat{static_cast<std::uint32_t>(info->rate),
            static_cast<std::uint32_t>(info->channels), SampleType::Float32,
            ChannelLayout::Interleaved};
        destination.frameCount = static_cast<std::uint64_t>(frameCount);
        return Result::Success();
    }

    Result Read(std::vector<float>& destination, std::size_t maxFrames,
        std::size_t& framesRead) override
    {
        framesRead = 0;
        if (!open_ || maxFrames > std::numeric_limits<int>::max()) {
            return Result::Failure(ResultCode::InvalidState, "Ogg decoder is not open");
        }
        const vorbis_info* info = ov_info(&stream_, -1);
        float** planar = nullptr;
        const long decoded = ov_read_float(&stream_, &planar,
            static_cast<int>(maxFrames), nullptr);
        if (decoded < 0 || info == nullptr) {
            return Result::Failure(ResultCode::ProcessingFailed,
                "Unable to decode Ogg Vorbis samples");
        }
        framesRead = static_cast<std::size_t>(decoded);
        destination.resize(framesRead * static_cast<std::size_t>(info->channels));
        for (std::size_t frame = 0; frame < framesRead; ++frame) {
            for (int channel = 0; channel < info->channels; ++channel) {
                destination[frame * static_cast<std::size_t>(info->channels) +
                    static_cast<std::size_t>(channel)] = planar[channel][frame];
            }
        }
        return Result::Success();
    }

    Result Seek(std::uint64_t frame) override
    {
        if (!open_ || frame > static_cast<std::uint64_t>(std::numeric_limits<ogg_int64_t>::max()) ||
            ov_pcm_seek(&stream_, static_cast<ogg_int64_t>(frame)) != 0) {
            return Result::Failure(ResultCode::ProcessingFailed,
                "Unable to seek Ogg Vorbis stream");
        }
        return Result::Success();
    }

    bool EndOfFile() const noexcept override
    {
        if (!open_) {
            return false;
        }
        auto* stream = const_cast<OggVorbis_File*>(&stream_);
        return ov_pcm_tell(stream) >= ov_pcm_total(stream, -1);
    }

private:
    void Close() noexcept
    {
        if (open_) {
            ov_clear(&stream_);
            open_ = false;
        }
    }

    OggVorbis_File stream_{};
    bool open_ = false;
};

}

OggFileReader::OggFileReader(std::unique_ptr<IAudioDecoder> decoder)
    : reader_(decoder != nullptr ? std::move(decoder) : std::make_unique<VorbisDecoder>())
{
}

OggFileReader::~OggFileReader()
{
}

Result OggFileReader::Open(const AudioFile& file)
{
    return reader_.Open(file, AudioFileType::Ogg);
}

Result OggFileReader::Read(AudioBuffer& destination)
{
    return reader_.Read(destination);
}

Result OggFileReader::Rewind()
{
    return reader_.Rewind();
}

Result OggFileReader::Seek(std::uint64_t frame)
{
    return reader_.Seek(frame);
}

bool OggFileReader::IsOpen() const noexcept
{
    return reader_.IsOpen();
}

bool OggFileReader::EndOfFile() const noexcept
{
    return reader_.EndOfFile();
}

std::uint64_t OggFileReader::Position() const noexcept
{
    return reader_.Position();
}

const AudioFormat& OggFileReader::Format() const noexcept
{
    return reader_.Format();
}

std::uint64_t OggFileReader::FrameCount() const noexcept
{
    return reader_.FrameCount();
}

std::uint64_t OggFileReader::FramesRemaining() const noexcept
{
    return reader_.FramesRemaining();
}

bool OggFileReader::CanSeek() const noexcept
{
    return reader_.CanSeek();
}

}
