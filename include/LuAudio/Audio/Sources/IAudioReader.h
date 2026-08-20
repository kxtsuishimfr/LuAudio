#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>

namespace LuAudio::Audio {

/**
 * @summary Reads decoded audio frames from a source.
 *
 * Position is the frame that the next successful read will return. Readers
 * may reject seeking when their underlying format does not support it.
 */
class IAudioReader {
public:
    virtual ~IAudioReader() = default;

    /**
     * @summary Reads frames into a matching destination buffer.
     * @param destination Buffer to fill.
     * @returns Operation result.
     */
    virtual Result Read(AudioBuffer& destination) = 0;

    /**
     * @summary Reads a complete offline block, waiting through temporary underflow.
     * @param destination Buffer to fill.
     * @returns Operation result.
     */
    Result ReadFully(AudioBuffer& destination)
    {
        const std::size_t requestedFrames = destination.FrameCount();
        std::size_t framesRead = 0;
        while (framesRead < requestedFrames && !EndOfFile()) {
            AudioBuffer chunk(destination.Format(), requestedFrames - framesRead);
            const std::uint64_t positionBefore = Position();
            const Result result = Read(chunk);
            if (!result.Succeeded()) {
                return result;
            }

            const std::uint64_t advanced = Position() - positionBefore;
            if (advanced > chunk.FrameCount()) {
                return Result::Failure(
                    ResultCode::ProcessingFailed,
                    "Audio reader advanced beyond its requested block");
            }
            if (advanced == 0) {
                if (!EndOfFile()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                continue;
            }

            const std::size_t samples = static_cast<std::size_t>(advanced) *
                destination.Format().channelCount;
            std::copy_n(chunk.Data(), samples,
                destination.Data() + framesRead * destination.Format().channelCount);
            framesRead += static_cast<std::size_t>(advanced);
        }
        destination.Resize(framesRead);
        return Result::Success();
    }

    /**
     * @summary Seeks to an absolute source frame.
     * @param frame Position of the next frame to read.
     * @returns Operation result.
     */
    virtual Result Seek(std::uint64_t frame) = 0;

    /**
     * @summary Moves the read position to the first frame.
     * @returns Operation result.
     */
    virtual Result Rewind() = 0;

    /**
     * @summary Gets the next frame that will be returned by Read.
     * @returns Current source position in frames.
     */
    virtual std::uint64_t Position() const noexcept = 0;

    /**
     * @summary Checks whether the source has no more frames to read.
     * @returns True when the reader is open and positioned at or beyond EOF.
     */
    virtual bool EndOfFile() const noexcept = 0;

    /**
     * @summary Checks whether the source is open.
     * @returns True when the reader can be read.
     */
    virtual bool IsOpen() const noexcept = 0;

    /**
     * @summary Gets the decoded audio format.
     * @returns Source format.
     */
    virtual const AudioFormat& Format() const noexcept = 0;

    /**
     * @summary Gets the total number of source frames.
     * @returns Total frame count.
     */
    virtual std::uint64_t FrameCount() const noexcept = 0;

    /**
     * @summary Gets the number of frames remaining from the current position.
     * @returns Remaining frame count.
     */
    virtual std::uint64_t FramesRemaining() const noexcept = 0;

    /**
     * @summary Checks whether absolute seeking is supported.
     * @returns True when Seek can change the source position.
     */
    virtual bool CanSeek() const noexcept = 0;
};

}