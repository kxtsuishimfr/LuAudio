#pragma once

#include <cstdint>

#include <LuAudio/Audio/Contracts/AudioFormat.h>

namespace LuAudio::Audio {

struct AudioStreamConfig {
    AudioFormat format;
    std::uint32_t framesPerBuffer = 512;
    bool exclusiveMode = false;

    bool IsValid() const noexcept;
};

}
