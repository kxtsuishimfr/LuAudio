#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>
#include <LuAudio/Audio/Sources/AudioFile.h>

namespace LuAudio::Audio {

class WavFileReader {
public:
    WavFileReader() = default;

    Result Open(const AudioFile& file);
    Result Open(const std::string& path);
    Result Read(AudioBuffer& destination);
    Result Rewind();

    bool IsOpen() const noexcept;
    bool EndOfFile() const noexcept;
    const AudioFormat& Format() const noexcept;
    std::size_t FrameCount() const noexcept;
    std::size_t FramesRemaining() const noexcept;

private:
    AudioFormat format_;
    std::vector<float> samples_;
    std::size_t readFrame_ = 0;
    bool open_ = false;
};

}
