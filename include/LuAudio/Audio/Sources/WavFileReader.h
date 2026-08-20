#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>
#include <LuAudio/Audio/Sources/AudioFile.h>
#include <LuAudio/Audio/Sources/IAudioReader.h>

namespace LuAudio::Audio {

/**
 * @summary Reads WAV audio into float buffers.
 */
class WavFileReader final : public IAudioReader {
public:
    /** @summary Creates a closed reader. */
    WavFileReader() = default;

    /**
     * @summary Opens a described audio file.
     * @param file File description.
     * @returns Operation result.
     */
    Result Open(const AudioFile& file);
    /**
     * @summary Opens a WAV file by path.
     * @param path WAV file path.
     * @returns Operation result.
     */
    Result Open(const std::string& path);
    /**
     * @summary Reads frames into a matching buffer.
     * @param destination Buffer to fill.
     * @returns Operation result.
     */
    Result Read(AudioBuffer& destination) override;
    /**
     * @summary Moves the read position back to the first frame.
     * @returns Operation result.
     */
    Result Rewind() override;
    /**
     * @summary Seeks to an absolute WAV frame.
     * @param frame Position of the next frame to read.
     * @returns Operation result.
     */
    Result Seek(std::uint64_t frame) override;

    /**
     * @summary Checks whether a file is open.
     * @returns True when the reader is open.
     */
    bool IsOpen() const noexcept override;
    /**
     * @summary Checks whether all frames have been read.
     * @returns True at end of file.
     */
    bool EndOfFile() const noexcept override;
    /**
     * @summary Gets the next frame that will be returned by Read.
     * @returns Current source position in frames.
     */
    std::uint64_t Position() const noexcept override;
    /**
     * @summary Gets the file audio format.
     * @returns The decoded format.
     */
    const AudioFormat& Format() const noexcept override;
    /**
     * @summary Gets the total frame count.
     * @returns Total frames.
     */
    std::uint64_t FrameCount() const noexcept override;
    /**
     * @summary Gets the unread frame count.
     * @returns Remaining frames.
     */
    std::uint64_t FramesRemaining() const noexcept override;

    /** @summary Reports that resident samples support absolute seeking. */
    bool CanSeek() const noexcept override;

private:
    AudioFormat format_;
    std::vector<float> samples_;
    std::size_t readFrame_ = 0;
    bool open_ = false;
};

}
