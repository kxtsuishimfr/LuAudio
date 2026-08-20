#pragma once

#include <cstdint>

#include <LuAudio/Audio/Contracts/AudioFormat.h>

namespace LuAudio::Audio {

/**
 * @summary Holds the requested audio stream settings.
 */
struct AudioStreamConfig {
    /** @summary Requested sample format. */
    AudioFormat format;
    /** @summary Preferred number of frames per device buffer. */
    std::uint32_t framesPerBuffer = 512;
    /** @summary Requests exclusive device access when supported. */
    bool exclusiveMode = false;

    /**
     * @summary Checks whether the stream settings can be used.
     * @returns True when the format and buffer size are valid.
     */
    bool IsValid() const noexcept;
};

}
