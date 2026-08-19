#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>
#include <LuAudio/Providers/Windows/Wasapi/WasapiDevice.h>

namespace LuAudio::Providers::Windows::Wasapi {

class WasapiBackend final : public Audio::IAudioBackend {
public:
    WasapiBackend();
    ~WasapiBackend() override;

    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig) override;
    Audio::Result Start() override;
    Audio::Result Stop() override;
    void Close() noexcept override;
    void SetCallback(Audio::AudioCallback callback) override;
    const Audio::AudioStreamConfig& ActualConfig() const noexcept override;

private:
    std::unique_ptr<WasapiDevice> device_;
    Audio::AudioCallback callback_;
};

}
