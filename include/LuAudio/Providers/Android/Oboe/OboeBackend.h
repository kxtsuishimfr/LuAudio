#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>

namespace LuAudio::Providers::Android::Oboe {

/**
 * @summary Adapts an Oboe output stream to the common audio backend interface.
 *
 * The backend requests float32 interleaved output and reports the configuration
 * negotiated by the Android audio system.
 */
class OboeBackend final : public Audio::IAudioBackend {
public:
    /** @summary Creates a closed Oboe backend. */
    OboeBackend();
    /** @summary Releases the Oboe backend and its stream resources. */
    ~OboeBackend() override;

    /**
     * @summary Opens the default Android output stream.
     * @param requestedConfig Requested stream settings.
     * @returns Operation result.
     */
    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig) override;
    /**
     * @summary Starts audio rendering.
     * @returns Operation result.
     */
    Audio::Result Start() override;
    /** @summary Reopens the default Android output stream. */
    Audio::Result Recover() override;
    /**
     * @summary Stops audio rendering.
     * @returns Operation result.
     */
    Audio::Result Stop() override;
    /** @summary Stops and closes the stream. */
    void Close() noexcept override;
    /**
     * @summary Sets the callback used to fill output buffers.
     * @param callback Callback invoked for each render buffer.
     */
    void SetCallback(Audio::AudioCallback callback) override;
    /**
     * @summary Gets the configuration negotiated by Oboe.
     * @returns Active stream configuration.
     */
    const Audio::AudioStreamConfig& ActualConfig() const noexcept override;
    
    class Implementation;

private:
    std::unique_ptr<Implementation> implementation_;
};

}