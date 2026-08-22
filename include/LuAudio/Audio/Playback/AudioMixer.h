#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>
#include <LuAudio/Audio/Processing/AudioEffectChain.h>
#include <LuAudio/Audio/Sources/IAudioReader.h>

namespace LuAudio::Audio {

/**
 * @summary Manages one backend stream and multiple audio sources.
 */
class AudioMixer final {
public:
    using SourceId = std::uint32_t;

    /**
     * @summary Creates a mixer with a fixed source capacity.
     * @param backend Backend used for audio output.
     * @param maxSources Maximum number of simultaneous sources.
     */
    AudioMixer(IAudioBackend& backend, std::size_t maxSources);
    /** @summary Stops playback and releases all mixer-owned sources. */
    ~AudioMixer();

    /**
     * @summary Opens the backend stream and establishes the master format.
     * @param requestedConfig Requested backend configuration.
     * @returns Operation result.
     */
    Result Open(const AudioStreamConfig& requestedConfig);
    /** @summary Starts backend rendering. */
    Result Start();
    /** @summary Stops backend rendering. */
    Result Stop();
    /** @summary Stops the backend and releases all sources. */
    void Close() noexcept;

    /**
     * @summary Adds an open reader after validating its format.
     * @param reader Reader owned by the mixer after success.
     * @param outId Receives the new source identifier.
     */
    Result AddSource(std::unique_ptr<IAudioReader> reader, SourceId& outId);
    /** @summary Removes a source and destroys it on the control thread. */
    Result RemoveSource(SourceId id);
    /** @summary Requests an absolute source-frame seek. */
    Result SeekSource(SourceId id, std::uint64_t frame);
    /** @summary Requests a relative source-frame seek. */
    Result SeekSourceRelative(SourceId id, std::int64_t frameDelta);
    /** @summary Changes a source gain for future processing. */
    Result SetSourceGain(SourceId id, float gain);
    /** @summary Pauses or resumes a source for future processing. */
    Result SetSourcePaused(SourceId id, bool paused);
    /** @summary Checks whether a source has reached EOF. */
    bool IsSourceFinished(SourceId id) const;
    /** @summary Gets the last source frame rendered by the mixer. */
    std::uint64_t SourcePosition(SourceId id) const noexcept;

    /** @summary Publishes an immutable effect-chain snapshot for a source. */
    Result SetSourceEffects(SourceId id, std::shared_ptr<const AudioEffectChain> chain);
    /** @summary Publishes an immutable master-bus effect-chain snapshot. */
    void SetMasterEffectChain(std::shared_ptr<const AudioEffectChain> chain) noexcept;

    /** @summary Gets the number of currently registered sources. */
    std::size_t ActiveSourceCount() const noexcept;

private:
    struct Entry {
        SourceId id = 0;
        std::unique_ptr<IAudioReader> reader;
        AudioBuffer sourceScratch;
        AudioBuffer masterScratch;
        std::shared_ptr<const AudioEffectChain> effects;
        float gain = 1.0F;
        bool paused = false;
        bool seekPending = false;
        std::uint64_t pendingSeekFrame = 0;
        std::atomic<std::uint64_t> renderedPosition = 0;
    };

    void Render(AudioBuffer& masterBuffer) noexcept;
    Entry* Find(SourceId id) noexcept;
    const Entry* Find(SourceId id) const noexcept;

    IAudioBackend& backend_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Entry>> sources_;
    std::vector<std::shared_ptr<Entry>> retiredSources_;
    std::size_t maxSources_;
    SourceId nextId_ = 0;
    AudioStreamConfig masterConfig_;
    std::shared_ptr<const AudioEffectChain> masterEffects_;
    bool open_ = false;
};

}
