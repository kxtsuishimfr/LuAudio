#pragma once

#include <atomic>
#include <cstdint>

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>
#include <LuAudio/Audio/Processing/AudioEffectChain.h>
#include <LuAudio/Audio/Sources/IAudioReader.h>

namespace LuAudio::Audio {

/**
 * @summary Connects one audio reader to one output backend.
 *
 * The player owns source position. Seek requests made while rendering are
 * applied at the beginning of a later audio block.
 */
class AudioPlayer final {
public:
    /**
     * @summary Creates a player using an existing backend.
     * @param backend Backend used for audio output.
     */
    explicit AudioPlayer(IAudioBackend& backend);
    /** @summary Stops playback and releases the source. */
    ~AudioPlayer();

    /**
     * @summary Opens an already-open reader for playback.
     * @param reader Reader whose format must match the requested backend format.
     * @param requestedConfig Requested output configuration.
     * @returns Operation result.
     */
    Result Open(std::unique_ptr<IAudioReader> reader, const AudioStreamConfig& requestedConfig);
    /**
     * @summary Starts playback.
     * @returns Operation result.
     */
    Result Start();
    /**
     * @summary Reopens the backend while preserving the current source position.
     * @returns Operation result.
     */
    Result RecoverBackend();
    /**
     * @summary Pauses backend rendering without discarding source or effect state.
     * @returns Operation result.
     */
    Result Pause();
    /**
     * @summary Resumes backend rendering from the current source position.
     * @returns Operation result.
     */
    Result Resume();
    /**
     * @summary Stops playback without discarding the source position.
     * @returns Operation result.
     */
    Result Stop();
    /** @summary Stops the backend and releases the reader. */
    void Close() noexcept;

    /**
     * @summary Requests an absolute source-frame seek.
     * @param frame Position of the next source frame to play.
     * @returns Operation result for accepting the request.
     */
    Result Seek(std::uint64_t frame);
    /**
     * @summary Requests a seek to the first source frame.
     * @returns Operation result for accepting the request.
     */
    Result Rewind();

    /**
     * @summary Attaches a non-owning effect chain used during rendering.
     * @param chain Chain to process after source reads, or nullptr to disable effects.
     */
    void SetEffectChain(AudioEffectChain* chain) noexcept;

    /**
     * @summary Gets the last source position applied to rendering.
     * @returns Applied source position in frames.
     */
    std::uint64_t Position() const noexcept;
    /**
     * @summary Gets the most recently requested source position.
     * @returns Requested source position in frames.
     */
    std::uint64_t RequestedPosition() const noexcept;
    /**
     * @summary Gets the total source frame count.
     * @returns Total source frames.
     */
    std::uint64_t FrameCount() const noexcept;
    /**
     * @summary Gets the source audio format.
     * @returns Source format, or a default format when closed.
     */
    const AudioFormat& Format() const noexcept;
    /**
     * @summary Checks whether the source has reached EOF.
     * @returns True when the last rendered source block reached EOF.
     */
    bool EndOfFile() const noexcept;
    /**
     * @summary Checks whether a source is open for playback.
     * @returns True when the player owns an open reader.
     */
    bool IsOpen() const noexcept;
    /** @summary Checks whether playback is paused. */
    bool IsPaused() const noexcept;

private:
    void Render(AudioBuffer& buffer) noexcept;

    IAudioBackend& backend_;
    std::unique_ptr<IAudioReader> reader_;
    std::atomic<std::uint64_t> pendingSeek_{0};
    std::atomic<std::uint64_t> position_{0};
    std::atomic<std::uint64_t> requestedPosition_{0};
    std::atomic<bool> seekPending_{false};
    std::atomic<bool> endOfFile_{false};
    AudioEffectChain* effectChain_ = nullptr;
    bool paused_ = false;
    bool open_ = false;
};

}