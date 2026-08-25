#include <LuAudio/Common.h>
#include <LuAudio/Audio/Sources/StreamingAudioReader.h>

#include <algorithm>

namespace LuAudio::Audio {
namespace {

constexpr std::size_t kDecodeChunkFrames = 4096;
constexpr std::size_t kBufferedChunks = 8;

}

StreamingAudioReader::StreamingAudioReader(std::unique_ptr<IAudioDecoder> decoder)
    : decoder_(std::move(decoder))
{
}

StreamingAudioReader::~StreamingAudioReader()
{
    StopWorker();
}

Result StreamingAudioReader::Open(const AudioFile& file, AudioFileType expectedType)
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
        seekGeneration_ = 0;
        decoderEnd_ = false;
    }

    if (!decoder_) {
        return Result::Failure(ResultCode::InvalidState, "Audio decoder is not available");
    }
    if (!file.IsValid()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio file path is empty");
    }
    if (file.Type() != expectedType) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio file type is not supported");
    }

    DecoderInfo info;
    const Result result = decoder_->Open(file, info);
    if (!result.Succeeded()) {
        return result;
    }
    if (!info.format.IsValid() || info.format.sampleType != SampleType::Float32 ||
        info.format.channelLayout != ChannelLayout::Interleaved || info.format.channelCount == 0) {
        return Result::Failure(ResultCode::ProcessingFailed,
            "Decoder returned invalid stream metadata");
    }

    {
        std::lock_guard lock(mutex_);
        format_ = info.format;
        frameCount_ = info.frameCount;
        stopRequested_.store(false, std::memory_order_release);
        decoderEnd_ = false;
        open_ = true;
    }
    worker_ = std::thread(&StreamingAudioReader::DecodeWorker, this);
    return Result::Success();
}

void StreamingAudioReader::StopWorker() noexcept
{
    stopRequested_.store(true, std::memory_order_release);
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void StreamingAudioReader::DecodeWorker()
{
    while (!stopRequested_.load(std::memory_order_acquire)) {
        std::uint64_t seekFrame = 0;
        std::uint64_t decodeGeneration = 0;
        bool performSeek = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return stopRequested_.load(std::memory_order_acquire) ||
                    seekPending_ || (!decoderEnd_ &&
                        bufferedSamples_.size() <
                            kDecodeChunkFrames * kBufferedChunks * format_.channelCount);
            });
            if (stopRequested_.load(std::memory_order_acquire)) {
                return;
            }
            if (seekPending_) {
                seekFrame = pendingSeek_;
                seekPending_ = false;
                bufferedSamples_.clear();
                decoderEnd_ = false;
                performSeek = true;
            }
            decodeGeneration = seekGeneration_;
        }

        if (performSeek && !decoder_->Seek(seekFrame).Succeeded()) {
            std::lock_guard lock(mutex_);
            if (decodeGeneration == seekGeneration_) {
                decoderEnd_ = true;
            }
            condition_.notify_all();
            continue;
        }

        std::vector<float> decoded;
        std::size_t framesRead = 0;
        if (!decoder_->Read(decoded, kDecodeChunkFrames, framesRead).Succeeded() || framesRead == 0) {
            std::lock_guard lock(mutex_);
            if (decodeGeneration == seekGeneration_) {
                decoderEnd_ = true;
            }
            condition_.notify_all();
            continue;
        }
        {
            std::lock_guard lock(mutex_);
            if (decodeGeneration != seekGeneration_) {
                condition_.notify_all();
                continue;
            }
            bufferedSamples_.insert(bufferedSamples_.end(), decoded.begin(), decoded.end());
        }
        condition_.notify_all();
    }
}

Result StreamingAudioReader::Read(AudioBuffer& destination)
{
    std::lock_guard lock(mutex_);
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio file is not open");
    }
    if (destination.Format().sampleRate != format_.sampleRate ||
        destination.Format().channelCount != format_.channelCount ||
        destination.Format().sampleType != format_.sampleType ||
        destination.Format().channelLayout != format_.channelLayout) {
        return Result::Failure(ResultCode::InvalidArgument,
            "Audio buffer format does not match source format");
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

Result StreamingAudioReader::Rewind()
{
    return Seek(0);
}

Result StreamingAudioReader::Seek(std::uint64_t frame)
{
    std::lock_guard lock(mutex_);
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio file is not open");
    }
    if (frameCount_ != 0 && frame > frameCount_) {
        return Result::Failure(ResultCode::InvalidArgument,
            "Audio seek position is outside the file");
    }
    pendingSeek_ = frame;
    ++seekGeneration_;
    seekPending_ = true;
    readFrame_ = frame;
    bufferedSamples_.clear();
    decoderEnd_ = false;
    condition_.notify_all();
    return Result::Success();
}

bool StreamingAudioReader::IsOpen() const noexcept
{
    std::lock_guard lock(mutex_);
    return open_;
}

bool StreamingAudioReader::EndOfFile() const noexcept
{
    std::lock_guard lock(mutex_);
    return open_ && decoderEnd_ && bufferedSamples_.empty();
}

std::uint64_t StreamingAudioReader::Position() const noexcept
{
    std::lock_guard lock(mutex_);
    return readFrame_;
}

const AudioFormat& StreamingAudioReader::Format() const noexcept
{
    return format_;
}

std::uint64_t StreamingAudioReader::FrameCount() const noexcept
{
    std::lock_guard lock(mutex_);
    return frameCount_;
}

std::uint64_t StreamingAudioReader::FramesRemaining() const noexcept
{
    std::lock_guard lock(mutex_);
    return frameCount_ > readFrame_ ? frameCount_ - readFrame_ : 0;
}

bool StreamingAudioReader::CanSeek() const noexcept
{
    return IsOpen();
}

}
