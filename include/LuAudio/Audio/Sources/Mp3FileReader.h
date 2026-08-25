#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/IAudioDecoder.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>
#include <LuAudio/Audio/Sources/AudioFile.h>
#include <LuAudio/Audio/Sources/IAudioReader.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <LuAudio/Audio/Sources/StreamingAudioReader.h>

namespace LuAudio::Audio {

/**
 * @summary Reads MP3 audio decoded by a provider-supplied decoder.
 */
class Mp3FileReader final : public IAudioReader {
public:
    /**
     * @summary Creates a closed reader using the supplied decoder.
     * @param decoder Decoder implementation supplied by the active provider.
     */
    explicit Mp3FileReader(std::unique_ptr<IAudioDecoder> decoder);
    ~Mp3FileReader() override;

    /**
     * @summary Opens a described MP3 file.
     * @param file File description to decode.
     * @returns Operation result.
     */
    Result Open(const AudioFile& file);
    /**
     * @summary Reads frames into a matching destination buffer.
     * @param destination Buffer to fill.
     * @returns Operation result.
     */
    Result Read(AudioBuffer& destination) override;
    /**
     * @summary Moves the read position to the first frame.
     * @returns Operation result.
     */
    Result Rewind() override;
    /**
     * @summary Seeks to an absolute MP3 frame.
     * @param frame Position of the next frame to read.
     * @returns Operation result.
     */
    Result Seek(std::uint64_t frame) override;

    /**
     * @summary Checks whether an MP3 file is open.
     * @returns True when the reader can be read.
     */
    bool IsOpen() const noexcept override;
    /**
     * @summary Checks whether no frames remain to be read.
     * @returns True when the reader is open and at EOF.
     */
    bool EndOfFile() const noexcept override;
    /**
     * @summary Gets the next frame that will be returned by Read.
     * @returns Current source position in frames.
     */
    std::uint64_t Position() const noexcept override;
    /**
     * @summary Gets the decoded MP3 format.
     * @returns Source format.
     */
    const AudioFormat& Format() const noexcept override;
    /**
     * @summary Gets the total number of decoded source frames.
     * @returns Total frame count.
     */
    std::uint64_t FrameCount() const noexcept override;
    /**
     * @summary Gets the number of frames remaining from the current position.
     * @returns Remaining frame count.
     */
    std::uint64_t FramesRemaining() const noexcept override;
    /**
     * @summary Reports whether absolute seeking is available.
     * @returns True while the reader is open.
     */
    bool CanSeek() const noexcept override;

private:
    StreamingAudioReader reader_;
};

}