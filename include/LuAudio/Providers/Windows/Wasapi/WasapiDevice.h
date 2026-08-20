#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>

namespace LuAudio::Providers::Windows::Wasapi {

/**
 * @summary Controls a native WASAPI output device.
 */
class WasapiDevice {
public:
    /** @summary Creates a closed WASAPI device. */
    WasapiDevice();
    /** @summary Releases the WASAPI device. */
    ~WasapiDevice();

    /**
     * @summary Opens the default output device.
     * @param requestedConfig Requested stream settings.
     * @returns Operation result.
     */
    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig);
    /**
     * @summary Starts output using a render callback.
     * @param callback Callback used to fill output buffers.
     * @returns Operation result.
     */
    Audio::Result Start(Audio::AudioCallback callback);
    /** @summary Reopens the current default output endpoint. */
    Audio::Result Recover();
    /**
     * @summary Stops output.
     * @returns Operation result.
     */
    Audio::Result Stop();
    /** @summary Closes the device and releases its resources. */
    void Close() noexcept;
    /**
     * @summary Gets the negotiated stream settings.
     * @returns Active stream configuration.
     */
    const Audio::AudioStreamConfig& ActualConfig() const noexcept;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}
