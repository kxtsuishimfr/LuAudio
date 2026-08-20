#include <LuAudio/Common.h>

#include <LuAudio/Audio/Playback/AudioPlayer.h>

namespace LuAudio::Audio {

namespace {

bool FormatsMatch(const AudioFormat& left, const AudioFormat& right)
{
    return left.sampleRate == right.sampleRate &&
        left.channelCount == right.channelCount &&
        left.sampleType == right.sampleType &&
        left.channelLayout == right.channelLayout;
}

}

AudioPlayer::AudioPlayer(IAudioBackend& backend)
    : backend_(backend)
{
}

AudioPlayer::~AudioPlayer()
{
    Close();
}

Result AudioPlayer::Open(std::unique_ptr<IAudioReader> reader, const AudioStreamConfig& requestedConfig)
{
    if (open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio player is already open");
    }
    if (!reader || !reader->IsOpen()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio player requires an open reader");
    }
    if (!reader->CanSeek()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio player requires a seekable reader");
    }
    if (!requestedConfig.IsValid() || !FormatsMatch(reader->Format(), requestedConfig.format)) {
        return Result::Failure(ResultCode::InvalidArgument, "Reader and backend formats do not match");
    }

    const auto result = backend_.Open(requestedConfig);
    if (!result.Succeeded()) {
        return result;
    }
    if (!FormatsMatch(reader->Format(), backend_.ActualConfig().format)) {
        backend_.Close();
        return Result::Failure(ResultCode::BackendUnavailable, "Backend format does not match reader format");
    }

    reader_ = std::move(reader);
    position_.store(reader_->Position(), std::memory_order_release);
    requestedPosition_.store(reader_->Position(), std::memory_order_release);
    pendingSeek_.store(0, std::memory_order_relaxed);
    seekPending_.store(false, std::memory_order_release);
    endOfFile_.store(reader_->EndOfFile(), std::memory_order_release);
    backend_.SetCallback([this](AudioBuffer& buffer) { Render(buffer); });
    open_ = true;
    return Result::Success();
}

Result AudioPlayer::Start()
{
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio player is not open");
    }
    return backend_.Start();
}

Result AudioPlayer::Stop()
{
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio player is not open");
    }
    return backend_.Stop();
}

void AudioPlayer::Close() noexcept
{
    if (!open_) {
        return;
    }
    backend_.Stop();
    backend_.Close();
    backend_.SetCallback({});
    reader_.reset();
    seekPending_.store(false, std::memory_order_release);
    endOfFile_.store(false, std::memory_order_release);
    open_ = false;
}

Result AudioPlayer::Seek(std::uint64_t frame)
{
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio player is not open");
    }
    if (frame > reader_->FrameCount()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio seek position is outside the source");
    }

    pendingSeek_.store(frame, std::memory_order_relaxed);
    requestedPosition_.store(frame, std::memory_order_release);
    seekPending_.store(true, std::memory_order_release);
    return Result::Success();
}

Result AudioPlayer::Rewind()
{
    return Seek(0);
}

std::uint64_t AudioPlayer::Position() const noexcept
{
    return position_.load(std::memory_order_acquire);
}

std::uint64_t AudioPlayer::RequestedPosition() const noexcept
{
    return requestedPosition_.load(std::memory_order_acquire);
}

std::uint64_t AudioPlayer::FrameCount() const noexcept
{
    return reader_ ? reader_->FrameCount() : 0;
}

const AudioFormat& AudioPlayer::Format() const noexcept
{
    static const AudioFormat defaultFormat{};
    return reader_ ? reader_->Format() : defaultFormat;
}

bool AudioPlayer::EndOfFile() const noexcept
{
    return endOfFile_.load(std::memory_order_acquire);
}

bool AudioPlayer::IsOpen() const noexcept
{
    return open_;
}

void AudioPlayer::Render(AudioBuffer& buffer) noexcept
{
    if (!reader_) {
        buffer.Clear();
        return;
    }

    if (seekPending_.exchange(false, std::memory_order_acq_rel)) {
        const auto result = reader_->Seek(pendingSeek_.load(std::memory_order_acquire));
        if (!result.Succeeded()) {
            buffer.Clear();
            return;
        }
        position_.store(reader_->Position(), std::memory_order_release);
        endOfFile_.store(reader_->EndOfFile(), std::memory_order_release);
    }

    const auto result = reader_->Read(buffer);
    if (!result.Succeeded()) {
        buffer.Clear();
        return;
    }
    position_.store(reader_->Position(), std::memory_order_release);
    endOfFile_.store(reader_->EndOfFile(), std::memory_order_release);
}

}