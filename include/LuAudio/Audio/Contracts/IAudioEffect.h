#pragma once

#include <LuAudio/Audio/Processing/AudioBuffer.h>

namespace LuAudio::Audio {

/**
 * @summary Processes one audio buffer in place.
 */
class IAudioEffect {
public:
    /** @summary Releases the effect. */
    virtual ~IAudioEffect() = default;

    /**
     * @summary Processes an audio buffer in place.
     * @param buffer Buffer to process.
     * @returns True when processing succeeds.
     */
    virtual bool Process(AudioBuffer& buffer) noexcept = 0;

    /** @summary Resets effect state. */
    virtual void Reset() noexcept {}
};

}