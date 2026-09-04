#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>

namespace LuAudio::Providers::Linux::PipeWire {

/**
 * @summary Adapts a PipeWire output stream to the common audio backend interface.
 *
 * The backend connects to the default PipeWire output target and renders
 * float32 interleaved audio through the callback supplied by the mixer.
 */
class PipeWireBackend final : public Audio::IAudioBackend {
public:
    /** @summary Creates a closed PipeWire backend. */
    PipeWireBackend();
    /** @summary Releases the PipeWire backend and its stream resources. */
    ~PipeWireBackend() override;

    /**
     * @summary Opens the default PipeWire output stream.
     * @param requestedConfig Requested stream settings.
     * @returns Operation result.
     */
    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig) override;
    /**
     * @summary Starts PipeWire output rendering.
     * @returns Operation result.
     */
    Audio::Result Start() override;
    /**
     * @summary Reopens the PipeWire output stream using its last configuration.
     * @returns Operation result.
     */
    Audio::Result Recover() override;
    /**
     * @summary Stops PipeWire output rendering.
     * @returns Operation result.
     */
    Audio::Result Stop() override;
    /** @summary Stops and closes the PipeWire output stream. */
    void Close() noexcept override;
    /**
     * @summary Sets the callback used to fill output buffers.
     * @param callback Callback invoked for each render buffer.
     */
    void SetCallback(Audio::AudioCallback callback) override;
    /**
     * @summary Gets the configuration used by the PipeWire stream.
     * @returns Active stream configuration.
     */
    const Audio::AudioStreamConfig& ActualConfig() const noexcept override;

    /** @internal Stores the PipeWire stream implementation. */
    class Implementation;

private:
    std::unique_ptr<Implementation> implementation_;
};

}
