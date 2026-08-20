#include <LuAudio/Common.h>

#include <LuAudio/Audio/Sources/Mp3FileReader.h>

namespace LuAudio::Audio {

namespace {

constexpr std::size_t kDecodeChunkFrames = 4096;
constexpr std::size_t kBufferedChunks = 8;

}

Mp3FileReader::Mp3FileReader(std::unique_ptr<IAudioDecoder> decoder)
    : decoder_(std::move(decoder))
{
}

Mp3FileReader::~Mp3FileReader()
{
    StopWorker();
}

Result Mp3FileReader::Open(const AudioFile& file)
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
        return Result::Failure(ResultCode::InvalidState, "MP3 decoder is not available");
    }
    if (!file.IsValid()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio file path is empty");
    }
    if (file.Type() != AudioFileType::Mp3) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio file type is not MP3");
    }

    DecoderInfo info;
    const auto result = decoder_->Open(file, info);
    if (!result.Succeeded()) {
        return result;
    }
    if (!info.format.IsValid() || info.format.sampleType != SampleType::Float32 ||
        info.format.channelLayout != ChannelLayout::Interleaved || info.format.channelCount == 0) {
        return Result::Failure(ResultCode::ProcessingFailed, "MP3 decoder returned invalid stream metadata");
    }

    {
        std::lock_guard lock(mutex_);
        format_ = info.format;
        frameCount_ = info.frameCount;
        stopRequested_.store(false, std::memory_order_release);
        decoderEnd_.store(false, std::memory_order_release);
        open_ = true;
    }
    worker_ = std::thread(&Mp3FileReader::DecodeWorker, this);
    return Result::Success();
}

void Mp3FileReader::StopWorker() noexcept
{
    stopRequested_.store(true, std::memory_order_release);
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Mp3FileReader::DecodeWorker()
{
    while (!stopRequested_.load(std::memory_order_acquire)) {
        std::uint64_t seekFrame = 0;
        bool performSeek = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return stopRequested_.load(std::memory_order_acquire) ||
                    seekPending_ || (!decoderEnd_.load(std::memory_order_acquire) &&
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
                performSeek = true;
            }
        }

        if (performSeek) {
            const auto result = decoder_->Seek(seekFrame);
            if (!result.Succeeded()) {
                decoderEnd_.store(true, std::memory_order_release);
                continue;
            }
            decoderEnd_.store(false, std::memory_order_release);
        }

        std::vector<float> decoded;
        std::size_t framesRead = 0;
        const auto result = decoder_->Read(decoded, kDecodeChunkFrames, framesRead);
        if (!result.Succeeded()) {
            decoderEnd_.store(true, std::memory_order_release);
            continue;
        }
        if (framesRead == 0) {
            decoderEnd_.store(true, std::memory_order_release);
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

Result Mp3FileReader::Read(AudioBuffer& destination)
{
    std::lock_guard lock(mutex_);
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "MP3 file is not open");
    }
    if (destination.Format().sampleRate != format_.sampleRate ||
        destination.Format().channelCount != format_.channelCount ||
        destination.Format().sampleType != format_.sampleType ||
        destination.Format().channelLayout != format_.channelLayout) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio buffer format does not match MP3 format");
    }

    const std::size_t availableFrames = bufferedSamples_.size() / format_.channelCount;
    const std::size_t framesToCopy = std::min(availableFrames, destination.FrameCount());
    const std::size_t sampleCount = framesToCopy * format_.channelCount;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        destination.Data()[index] = bufferedSamples_.front();
        bufferedSamples_.pop_front();
    }
    if (framesToCopy < destination.FrameCount()) {
        std::fill(destination.Data() + sampleCount, destination.Data() + destination.SampleCount(), 0.0F);
    }
    readFrame_ += framesToCopy;
    condition_.notify_all();
    return Result::Success();
}

Result Mp3FileReader::Rewind()
{
    return Seek(0);
}

Result Mp3FileReader::Seek(std::uint64_t frame)
{
    std::lock_guard lock(mutex_);
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "MP3 file is not open");
    }
    if (frameCount_ != 0 && frame > frameCount_) {
        return Result::Failure(ResultCode::InvalidArgument, "MP3 seek position is outside the file");
    }
    pendingSeek_ = frame;
    seekPending_ = true;
    readFrame_ = frame;
    bufferedSamples_.clear();
    decoderEnd_.store(false, std::memory_order_release);
    condition_.notify_all();
    return Result::Success();
}

bool Mp3FileReader::IsOpen() const noexcept
{
    std::lock_guard lock(mutex_);
    return open_;
}

bool Mp3FileReader::EndOfFile() const noexcept
{
    std::lock_guard lock(mutex_);
    return open_ && decoderEnd_.load(std::memory_order_acquire) && bufferedSamples_.empty();
}

std::uint64_t Mp3FileReader::Position() const noexcept
{
    std::lock_guard lock(mutex_);
    return readFrame_;
}

const AudioFormat& Mp3FileReader::Format() const noexcept
{
    return format_;
}

std::uint64_t Mp3FileReader::FrameCount() const noexcept
{
    std::lock_guard lock(mutex_);
    return frameCount_;
}

std::uint64_t Mp3FileReader::FramesRemaining() const noexcept
{
    std::lock_guard lock(mutex_);
    return frameCount_ > readFrame_ ? frameCount_ - readFrame_ : 0;
}

bool Mp3FileReader::CanSeek() const noexcept
{
    return IsOpen();
}

}
