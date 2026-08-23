#pragma once

#include <LuAudio/Audio/Contracts/IAudioSink.h>

#include <memory>
#include <string>

namespace LuAudio::Audio {

/**
 * @summary Streams interleaved Float32 audio into an Ogg Vorbis file.
 */
class OggFileWriter final : public IAudioSink {
public:
    /**
     * @summary Creates a writer for a filesystem path.
     * @param path Destination Ogg path.
     */
    explicit OggFileWriter(std::string path);
    ~OggFileWriter() override;

    Result Open(const AudioFormat& format) override;
    Result Write(const AudioBuffer& buffer) override;
    Result Finalize() override;
    void Abort() noexcept override;
    const AudioFormat& Format() const noexcept override;
    bool IsOpen() const noexcept override;

private:
    struct Impl;

    std::string path_;
    std::unique_ptr<Impl> impl_;
    AudioFormat format_;
    bool open_ = false;
    bool finalized_ = false;
};

}
