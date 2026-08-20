#pragma once

#include <LuAudio/Audio/Contracts/IAudioDecoder.h>

namespace LuAudio::Providers::Windows {

/**
 * @summary Decodes supported audio files using the Windows media stack.
 */
class WAudioDecoder final : public Audio::IAudioDecoder {
public:
    ~WAudioDecoder() override;

    Audio::Result Open(const Audio::AudioFile& file, Audio::DecoderInfo& destination) override;
    Audio::Result Read(std::vector<float>& destination, std::size_t maxFrames,
        std::size_t& framesRead) override;
    Audio::Result Seek(std::uint64_t frame) override;
    bool EndOfFile() const noexcept override;

private:
    void* reader_ = nullptr;
    std::uint32_t sampleRate_ = 0;
    std::uint32_t channelCount_ = 0;
    std::uint64_t frameCount_ = 0;
    std::uint64_t position_ = 0;
    std::vector<float> pending_;
    bool open_ = false;
};

}