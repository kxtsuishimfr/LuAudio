#pragma once

#include <LuAudio/Common.h>

namespace LuAudio::Audio {

enum class AudioFileType {
    Wav
};

class AudioFile {
public:
    AudioFile() = default;
    AudioFile(std::string path, AudioFileType type);

    const std::string& Path() const noexcept;
    AudioFileType Type() const noexcept;
    bool IsValid() const noexcept;

private:
    std::string path_;
    AudioFileType type_ = AudioFileType::Wav;
};

}
