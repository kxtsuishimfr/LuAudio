#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioDecoder.h>
#include <LuAudio/Audio/Sources/IAudioReader.h>
#include <LuAudio/Audio/Sources/StreamingAudioReader.h>

namespace LuAudio::Audio {
/**
 * @summary Reads Ogg audio through a background Vorbis decoder.
 */
class OggFileReader final : public IAudioReader {
public:
    /**
     * @summary Creates a closed reader using the supplied decoder.
     * @param decoder Decoder implementation for Ogg payloads.
     */
    explicit OggFileReader(std::unique_ptr<IAudioDecoder> decoder);
    ~OggFileReader() override;

    /**
     * @summary Opens an Ogg file.
     * @param file File description to decode.
     * @returns Operation result.
     */
    Result Open(const AudioFile& file);
    Result Read(AudioBuffer& destination) override;
    Result Rewind() override;
    Result Seek(std::uint64_t frame) override;
    bool IsOpen() const noexcept override;
    bool EndOfFile() const noexcept override;
    std::uint64_t Position() const noexcept override;
    const AudioFormat& Format() const noexcept override;
    std::uint64_t FrameCount() const noexcept override;
    std::uint64_t FramesRemaining() const noexcept override;
    bool CanSeek() const noexcept override;

private:
    StreamingAudioReader reader_;
};

}
