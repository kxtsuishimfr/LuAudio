#pragma once

#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>

namespace LuAudio::Audio {

/**
 * @summary Receives sequential rendered audio blocks.
 */
class IAudioSink {
public:
    virtual ~IAudioSink() = default;

    /**
     * @summary Opens the sink for a specific output format.
     * @param format Output audio format.
     * @returns Operation result.
     */
    virtual Result Open(const AudioFormat& format) = 0;

    /**
     * @summary Writes one block of audio frames.
     * @param buffer Audio block matching the opened format.
     * @returns Operation result.
     */
    virtual Result Write(const AudioBuffer& buffer) = 0;

    /**
     * @summary Finalizes the output and completes any container metadata.
     * @returns Operation result.
     */
    virtual Result Finalize() = 0;

    /**
     * @summary Aborts output and discards incomplete sink state.
     */
    virtual void Abort() noexcept = 0;

    /**
     * @summary Gets the format used when the sink was opened.
     * @returns The opened format.
     */
    virtual const AudioFormat& Format() const noexcept = 0;

    /**
     * @summary Checks whether the sink is open for writing.
     * @returns True when the sink is ready to receive blocks.
     */
    virtual bool IsOpen() const noexcept = 0;
};

}
