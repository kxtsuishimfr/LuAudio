#include <LuAudio/Common.h>

#include <LuAudio/Audio/Sources/AudioFile.h>

namespace LuAudio::Audio {

AudioFile::AudioFile(std::string path, AudioFileType type)
    : path_(std::move(path)), type_(type)
{
}

const std::string& AudioFile::Path() const noexcept
{
    return path_;
}

AudioFileType AudioFile::Type() const noexcept
{
    return type_;
}

bool AudioFile::IsValid() const noexcept
{
    return !path_.empty();
}

}
