#pragma once

#include <LuAudio/Audio/Contracts/IAudioDecoder.h>

namespace LuAudio::Providers::Android {

/**
 * @summary Decodes supported audio files using the Android media stack.
 */
class AAudioDecoder final : public Audio::IAudioDecoder {
public:
    ~AAudioDecoder() override;

    Audio::Result Open(const Audio::AudioFile& file, Audio::DecoderInfo& destination) override;
    Audio::Result Read(std::vector<float>& destination, std::size_t maxFrames,
        std::size_t& framesRead) override;
    Audio::Result Seek(std::uint64_t frame) override;
    bool EndOfFile() const noexcept override;

private:
    void* state_ = nullptr;
};

}