#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>

namespace LuAudio::Providers::Windows::Wasapi {

class WasapiDevice {
public:
    WasapiDevice();
    ~WasapiDevice();

    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig);
    Audio::Result Start(Audio::AudioCallback callback);
    Audio::Result Stop();
    void Close() noexcept;
    const Audio::AudioStreamConfig& ActualConfig() const noexcept;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}
