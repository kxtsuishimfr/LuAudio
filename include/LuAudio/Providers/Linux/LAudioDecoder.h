#pragma once

#include <LuAudio/Audio/Contracts/IAudioDecoder.h>

namespace LuAudio::Providers::Linux {

/**
 * @summary Decodes supported audio files using the Linux media stack.
 */
class LAudioDecoder final : public Audio::IAudioDecoder {
public:
    /** @summary Creates a closed Linux audio decoder. */
    LAudioDecoder();
    /** @summary Releases the decoder and its media resources. */
    ~LAudioDecoder() override;

    /**
    * @summary Opens an audio stream and reads its metadata.
     * @param file File description to open.
     * @param destination Destination for stream metadata.
     * @returns Operation result.
     */
    Audio::Result Open(const Audio::AudioFile& file, Audio::DecoderInfo& destination) override;
    /**
     * @summary Decodes up to the requested number of frames as interleaved float32 samples.
     * @param destination Destination for decoded samples.
     * @param maxFrames Maximum number of frames to decode.
     * @param framesRead Receives the number of decoded frames.
     * @returns Operation result.
     */
    Audio::Result Read(std::vector<float>& destination, std::size_t maxFrames,
        std::size_t& framesRead) override;
    /**
     * @summary Seeks to an absolute decoded frame.
     * @param frame Position of the next frame to decode.
     * @returns Operation result.
     */
    Audio::Result Seek(std::uint64_t frame) override;
    /** @summary Checks whether the decoder has reached the end of its stream. */
    bool EndOfFile() const noexcept override;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}
