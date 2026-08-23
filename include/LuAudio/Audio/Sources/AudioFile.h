#pragma once

#include <LuAudio/Common.h>

namespace LuAudio::Audio {

/**
 * @summary Identifies a supported audio file format.
 */
enum class AudioFileType {
    /** @summary RIFF/WAVE audio file. */
    Wav,
    /** @summary MPEG Layer III audio file. */
    Mp3,
    /** @summary Ogg container with a currently supported audio codec. */
    Ogg
};

/**
 * @summary Describes an audio file supplied to a reader.
 */
class AudioFile {
public:
    /** @summary Creates an empty file description. */
    AudioFile() = default;
    /**
     * @summary Creates a file description.
     * @param path File path.
     * @param type File format.
     */
    AudioFile(std::string path, AudioFileType type);

    /**
     * @summary Gets the file path.
     * @returns The file path.
     */
    const std::string& Path() const noexcept;
    /**
     * @summary Gets the file format.
     * @returns The file type.
     */
    AudioFileType Type() const noexcept;
    /**
     * @summary Checks whether a path was supplied.
     * @returns True when the description can be opened.
     */
    bool IsValid() const noexcept;

private:
    std::string path_;
    AudioFileType type_ = AudioFileType::Wav;
};

}
