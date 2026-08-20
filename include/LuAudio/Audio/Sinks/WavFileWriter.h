#pragma once

#include <fstream>
#include <string>

#include <LuAudio/Audio/Contracts/IAudioSink.h>

namespace LuAudio::Audio {

/**
 * @summary Streams interleaved Float32 audio into a WAV file.
 */
class WavFileWriter final : public IAudioSink {
public:
    /**
     * @summary Creates a writer for a filesystem path.
     * @param path Destination WAV path.
     */
    explicit WavFileWriter(std::string path);
    ~WavFileWriter() override;

    Result Open(const AudioFormat& format) override;
    Result Write(const AudioBuffer& buffer) override;
    Result Finalize() override;
    void Abort() noexcept override;
    const AudioFormat& Format() const noexcept override;
    bool IsOpen() const noexcept override;

private:
    Result WriteHeader();
    Result PatchHeader();

    std::string path_;
    std::ofstream file_;
    AudioFormat format_;
    std::uint64_t dataBytes_ = 0;
    bool open_ = false;
    bool finalized_ = false;
};

}
