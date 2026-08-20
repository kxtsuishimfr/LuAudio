#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>
#include <LuAudio/Audio/Sources/AudioFile.h>

namespace LuAudio::Audio {

/**
 * @summary Reads WAV audio into float buffers.
 */
class WavFileReader {
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
    Result Read(AudioBuffer& destination);
    /**
     * @summary Moves the read position back to the first frame.
     * @returns Operation result.
     */
    Result Rewind();

    /**
     * @summary Checks whether a file is open.
     * @returns True when the reader is open.
     */
    bool IsOpen() const noexcept;
    /**
     * @summary Checks whether all frames have been read.
     * @returns True at end of file.
     */
    bool EndOfFile() const noexcept;
    /**
     * @summary Gets the file audio format.
     * @returns The decoded format.
     */
    const AudioFormat& Format() const noexcept;
    /**
     * @summary Gets the total frame count.
     * @returns Total frames.
     */
    std::size_t FrameCount() const noexcept;
    /**
     * @summary Gets the unread frame count.
     * @returns Remaining frames.
     */
    std::size_t FramesRemaining() const noexcept;

private:
    AudioFormat format_;
    std::vector<float> samples_;
    std::size_t readFrame_ = 0;
    bool open_ = false;
};

}
