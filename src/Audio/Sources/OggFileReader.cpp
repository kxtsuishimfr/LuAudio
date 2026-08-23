#include <LuAudio/Common.h>
#include <LuAudio/Audio/Sources/OggFileReader.h>

#include <vorbis/vorbisfile.h>

#include <algorithm>
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
    : decoder_(decoder != nullptr ? std::move(decoder) : std::make_unique<VorbisDecoder>())
{
}

OggFileReader::~OggFileReader() { StopWorker(); }

Result OggFileReader::Open(const AudioFile& file)
{
    StopWorker();
    {
        std::lock_guard lock(mutex_);
        open_ = false;
        format_ = {};
        frameCount_ = 0;
        readFrame_ = 0;
        bufferedSamples_.clear();
        seekPending_ = false;
    }
    if (!decoder_) {
        return Result::Failure(ResultCode::InvalidState, "Ogg decoder is not available");
    }
    if (!file.IsValid() || file.Type() != AudioFileType::Ogg) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio file is not Ogg");
    }

    DecoderInfo info;
    const Result result = decoder_->Open(file, info);
    if (!result.Succeeded()) {
        return result;
    }
    if (!info.format.IsValid() || info.format.sampleType != SampleType::Float32 ||
        info.format.channelLayout != ChannelLayout::Interleaved || info.format.channelCount == 0) {
        return Result::Failure(ResultCode::ProcessingFailed,
            "Ogg decoder returned invalid stream metadata");
    }
    {
        std::lock_guard lock(mutex_);
        format_ = info.format;
        frameCount_ = info.frameCount;
        stopRequested_ = false;
        decoderEnd_ = false;
        open_ = true;
    }
    worker_ = std::thread(&OggFileReader::DecodeWorker, this);
    return Result::Success();
}

void OggFileReader::StopWorker() noexcept
{
    {
        std::lock_guard lock(mutex_);
        stopRequested_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void OggFileReader::DecodeWorker()
{
    while (true) {
        std::uint64_t seekFrame = 0;
        bool performSeek = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return stopRequested_ || seekPending_ ||
                    (!decoderEnd_ && bufferedSamples_.size() <
                        kDecodeChunkFrames * kBufferedChunks * format_.channelCount);
            });
            if (stopRequested_) {
                return;
            }
            if (seekPending_) {
                seekFrame = pendingSeek_;
                seekPending_ = false;
                bufferedSamples_.clear();
                decoderEnd_ = false;
                performSeek = true;
            }
        }
        if (performSeek && !decoder_->Seek(seekFrame).Succeeded()) {
            std::lock_guard lock(mutex_);
            decoderEnd_ = true;
            condition_.notify_all();
            continue;
        }
        std::vector<float> decoded;
        std::size_t framesRead = 0;
        if (!decoder_->Read(decoded, kDecodeChunkFrames, framesRead).Succeeded() || framesRead == 0) {
            std::lock_guard lock(mutex_);
            decoderEnd_ = true;
            condition_.notify_all();
            continue;
        }
        {
            std::lock_guard lock(mutex_);
            bufferedSamples_.insert(bufferedSamples_.end(), decoded.begin(), decoded.end());
        }
        condition_.notify_all();
    }
}

Result OggFileReader::Read(AudioBuffer& destination)
{
    std::lock_guard lock(mutex_);
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Ogg file is not open");
    }
    if (destination.Format().sampleRate != format_.sampleRate ||
        destination.Format().channelCount != format_.channelCount ||
        destination.Format().sampleType != format_.sampleType ||
        destination.Format().channelLayout != format_.channelLayout) {
        return Result::Failure(ResultCode::InvalidArgument,
            "Audio buffer format does not match Ogg format");
    }
    const std::size_t availableFrames = bufferedSamples_.size() / format_.channelCount;
    const std::size_t framesToCopy = std::min(availableFrames, destination.FrameCount());
    const std::size_t sampleCount = framesToCopy * format_.channelCount;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        destination.Data()[index] = bufferedSamples_.front();
        bufferedSamples_.pop_front();
    }
    if (framesToCopy < destination.FrameCount()) {
        std::fill(destination.Data() + sampleCount,
            destination.Data() + destination.SampleCount(), 0.0F);
    }
    readFrame_ += framesToCopy;
    condition_.notify_all();
    return Result::Success();
}

Result OggFileReader::Rewind() { return Seek(0); }

Result OggFileReader::Seek(std::uint64_t frame)
{
    std::lock_guard lock(mutex_);
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Ogg file is not open");
    }
    if (frame > frameCount_) {
        return Result::Failure(ResultCode::InvalidArgument,
            "Ogg seek position is outside the file");
    }
    pendingSeek_ = frame;
    seekPending_ = true;
    readFrame_ = frame;
    bufferedSamples_.clear();
    decoderEnd_ = false;
    condition_.notify_all();
    return Result::Success();
}

bool OggFileReader::IsOpen() const noexcept
{
    std::lock_guard lock(mutex_);
    return open_;
}

bool OggFileReader::EndOfFile() const noexcept
{
    std::lock_guard lock(mutex_);
    return open_ && decoderEnd_ && bufferedSamples_.empty();
}

std::uint64_t OggFileReader::Position() const noexcept
{
    std::lock_guard lock(mutex_);
    return readFrame_;
}

const AudioFormat& OggFileReader::Format() const noexcept { return format_; }

std::uint64_t OggFileReader::FrameCount() const noexcept
{
    std::lock_guard lock(mutex_);
    return frameCount_;
}

std::uint64_t OggFileReader::FramesRemaining() const noexcept
{
    std::lock_guard lock(mutex_);
    return frameCount_ > readFrame_ ? frameCount_ - readFrame_ : 0;
}

bool OggFileReader::CanSeek() const noexcept { return IsOpen(); }

}
