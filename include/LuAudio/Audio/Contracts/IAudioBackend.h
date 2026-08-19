#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioStreamConfig.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>

namespace LuAudio::Audio {

using AudioCallback = std::function<void(AudioBuffer&)>;

class IAudioBackend {
public:
    virtual ~IAudioBackend();

    virtual Result Open(const AudioStreamConfig& requestedConfig) = 0;
    virtual Result Start() = 0;
    virtual Result Stop() = 0;
    virtual void Close() noexcept = 0;
    virtual void SetCallback(AudioCallback callback) = 0;
    virtual const AudioStreamConfig& ActualConfig() const noexcept = 0;
};

}
