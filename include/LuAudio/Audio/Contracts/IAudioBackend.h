#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioStreamConfig.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>

namespace LuAudio::Audio {

/**
 * @summary Renders one audio buffer.
 * @param buffer Buffer to fill with audio samples.
 */
using AudioCallback = std::function<void(AudioBuffer&)>;

/**
 * @summary Provides device-independent stream control.
 */
class IAudioBackend {
public:
    /** @summary Releases the backend. */
    virtual ~IAudioBackend();

    /**
     * @summary Opens a stream using the requested settings.
     * @param requestedConfig Requested stream settings.
     * @returns Operation result.
     */
    virtual Result Open(const AudioStreamConfig& requestedConfig) = 0;
    /**
     * @summary Starts audio rendering.
     * @returns Operation result.
     */
    virtual Result Start() = 0;
    /**
     * @summary Reopens a failed or invalidated stream using its last configuration.
     * @returns Operation result.
     */
    virtual Result Recover()
    {
        return Result::Failure(ResultCode::BackendUnavailable, "Backend recovery is not supported");
    }
    /**
     * @summary Stops audio rendering.
     * @returns Operation result.
     */
    virtual Result Stop() = 0;
    /** @summary Closes the stream. */
    virtual void Close() noexcept = 0;
    /**
     * @summary Sets the callback used to fill output buffers.
     * @param callback Callback invoked for each render buffer.
     */
    virtual void SetCallback(AudioCallback callback) = 0;
    /**
     * @summary Gets the format selected by the backend.
     * @returns The active stream configuration.
     */
    virtual const AudioStreamConfig& ActualConfig() const noexcept = 0;
};

}
