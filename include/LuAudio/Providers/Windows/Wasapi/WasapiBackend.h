#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>
#include <LuAudio/Providers/Windows/Wasapi/WasapiDevice.h>

namespace LuAudio::Providers::Windows::Wasapi {

/**
 * @summary Adapts WASAPI to the common audio backend interface.
 */
class WasapiBackend final : public Audio::IAudioBackend {
public:
    /** @summary Creates a WASAPI backend. */
    WasapiBackend();
    /** @summary Releases the WASAPI backend. */
    ~WasapiBackend() override;

    /**
     * @summary Opens the default output device.
     * @param requestedConfig Requested stream settings.
     * @returns Operation result.
     */
    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig) override;
    /**
     * @summary Starts output.
     * @returns Operation result.
     */
    Audio::Result Start() override;
    /** @summary Reopens the current default output endpoint. */
    Audio::Result Recover() override;
    /**
     * @summary Stops output.
     * @returns Operation result.
     */
    Audio::Result Stop() override;
    /** @summary Closes the backend. */
    void Close() noexcept override;
    /**
     * @summary Sets the output callback.
     * @param callback Callback used to fill output buffers.
     */
    void SetCallback(Audio::AudioCallback callback) override;
    /**
     * @summary Gets the negotiated stream settings.
     * @returns Active stream configuration.
     */
    const Audio::AudioStreamConfig& ActualConfig() const noexcept override;

private:
    std::unique_ptr<WasapiDevice> device_;
    Audio::AudioCallback callback_;
};

}
