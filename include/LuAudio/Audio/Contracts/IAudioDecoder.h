#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Sources/AudioFile.h>

namespace LuAudio::Audio {

/**
 * @summary Describes a decoder's opened stream.
 */
struct DecoderInfo {
    AudioFormat format;
    std::uint64_t frameCount = 0;
};

/**
 * @summary Incrementally decodes an audio file into the core sample representation.
 */
class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;

    /**
     * @summary Opens a stream and reads its metadata without decoding the whole file.
     * @param file File description to open.
    * @param destination Destination for stream metadata.
     * @returns Operation result.
     */
    virtual Result Open(const AudioFile& file, DecoderInfo& destination) = 0;

    /**
     * @summary Decodes up to the requested number of frames.
     * @param destination Destination for interleaved float32 samples.
     * @param maxFrames Maximum number of frames to decode.
     * @param framesRead Receives the number of decoded frames.
     * @returns Operation result.
     */
    virtual Result Read(std::vector<float>& destination, std::size_t maxFrames,
        std::size_t& framesRead) = 0;

    /**
     * @summary Seeks to an absolute decoded frame.
     * @param frame Position of the next frame to decode.
     * @returns Operation result.
     */
    virtual Result Seek(std::uint64_t frame) = 0;

    /**
     * @summary Checks whether the decoder has reached the end of its stream.
     * @returns True when no decoded frames remain.
     */
    virtual bool EndOfFile() const noexcept = 0;
};

}