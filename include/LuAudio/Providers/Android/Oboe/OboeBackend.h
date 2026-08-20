#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>

namespace LuAudio::Providers::Android::Oboe {

class OboeBackend final : public Audio::IAudioBackend {
public:
    OboeBackend();
    ~OboeBackend() override;

    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig) override;
    Audio::Result Start() override;
    Audio::Result Stop() override;
    void Close() noexcept override;
    void SetCallback(Audio::AudioCallback callback) override;
    const Audio::AudioStreamConfig& ActualConfig() const noexcept override;

    class Implementation;

private:
    std::unique_ptr<Implementation> implementation_;
};

}