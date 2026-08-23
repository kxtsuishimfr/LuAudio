#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/IAudioDecoder.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>
#include <LuAudio/Audio/Sources/AudioFile.h>
#include <LuAudio/Audio/Sources/IAudioReader.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

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
    void StopWorker() noexcept;
    void DecodeWorker();

    std::unique_ptr<IAudioDecoder> decoder_;
    AudioFormat format_;
    std::uint64_t frameCount_ = 0;
    std::uint64_t readFrame_ = 0;
    std::deque<float> bufferedSamples_;
    std::uint64_t pendingSeek_ = 0;
    bool seekPending_ = false;
    bool open_ = false;
    bool decoderEnd_ = false;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool stopRequested_ = false;
};

}
