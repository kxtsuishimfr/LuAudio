#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioFormat.h>

namespace LuAudio::Audio {

class AudioBuffer {
public:
    AudioBuffer() = default;
    AudioBuffer(AudioFormat format, std::size_t frameCount);

    const AudioFormat& Format() const noexcept;
    std::size_t FrameCount() const noexcept;
    float* Data() noexcept;
    const float* Data() const noexcept;
    std::size_t SampleCount() const noexcept;
    void Clear() noexcept;

private:
    AudioFormat format_;
    std::vector<float> samples_;
};

}
