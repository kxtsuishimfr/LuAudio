#include <algorithm>

#include <LuAudio/Audio/Processing/AudioBuffer.h>

namespace LuAudio::Audio {

AudioBuffer::AudioBuffer(AudioFormat format, std::size_t frameCount)
    : format_(format), samples_(frameCount * format.channelCount, 0.0F)
{
}

const AudioFormat& AudioBuffer::Format() const noexcept
{
    return format_;
}

std::size_t AudioBuffer::FrameCount() const noexcept
{
    if (format_.channelCount == 0) {
        return 0;
    }

    return samples_.size() / format_.channelCount;
}

float* AudioBuffer::Data() noexcept
{
    return samples_.data();
}

const float* AudioBuffer::Data() const noexcept
{
    return samples_.data();
}

std::size_t AudioBuffer::SampleCount() const noexcept
{
    return samples_.size();
}

void AudioBuffer::Resize(std::size_t frameCount) noexcept
{
    samples_.resize(frameCount * format_.channelCount, 0.0F);
}

void AudioBuffer::Clear() noexcept
{
    std::fill(samples_.begin(), samples_.end(), 0.0F);
}

}
