#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioFormat.h>

namespace LuAudio::Audio {

/**
 * @summary Stores interleaved float audio samples.
 */
class AudioBuffer {
public:
    /** @summary Creates an empty buffer. */
    AudioBuffer() = default;
    /**
     * @summary Creates a buffer with the given format and frame count.
     * @param format Sample format.
     * @param frameCount Number of frames to allocate.
     */
    AudioBuffer(AudioFormat format, std::size_t frameCount);

    /**
     * @summary Gets the buffer format.
     * @returns The sample format.
     */
    const AudioFormat& Format() const noexcept;
    /**
     * @summary Gets the number of frames in the buffer.
     * @returns Frame count.
     */
    std::size_t FrameCount() const noexcept;
    /**
     * @summary Gets writable sample data.
     * @returns Pointer to interleaved samples.
     */
    float* Data() noexcept;
    /**
     * @summary Gets read-only sample data.
     * @returns Pointer to interleaved samples.
     */
    const float* Data() const noexcept;
    /**
     * @summary Gets the total sample count.
     * @returns Number of float samples.
     */
    std::size_t SampleCount() const noexcept;
    /**
     * @summary Changes the logical frame count.
     * @param frameCount New frame count.
     */
    void Resize(std::size_t frameCount) noexcept;
    /** @summary Sets all samples to zero. */
    void Clear() noexcept;

private:
    AudioFormat format_;
    std::vector<float> samples_;
};

}
