#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <chrono>
#include <thread>
#include <vector>

#include <LuAudio/Audio/Sources/StreamingAudioReader.h>

namespace {

using namespace LuAudio::Audio;

constexpr AudioFormat TestFormat{48000, 2};
constexpr std::size_t TestFrames = 16384;

class StressDecoder final : public IAudioDecoder {
public:
    explicit StressDecoder(std::size_t frameCount = TestFrames)
        : frameCount_(frameCount)
    {
    }

    Result Open(const AudioFile&, DecoderInfo& destination) override
    {
        if (failOpen_.load()) {
            return Result::Failure(ResultCode::ProcessingFailed, "injected open failure");
        }
        position_ = 0;
        destination.format = TestFormat;
        destination.frameCount = frameCount_;
        return Result::Success();
    }

    Result Read(std::vector<float>& destination, std::size_t maxFrames,
        std::size_t& framesRead) override
    {
        if (failRead_.load()) {
            framesRead = 0;
            return Result::Failure(ResultCode::ProcessingFailed, "injected read failure");
        }
        framesRead = std::min(maxFrames, frameCount_ - position_);
        destination.resize(framesRead * TestFormat.channelCount);
        for (std::size_t frame = 0; frame < framesRead; ++frame) {
            destination[frame * 2] = static_cast<float>(position_ + frame);
            destination[frame * 2 + 1] = static_cast<float>(-
                static_cast<std::int64_t>(position_ + frame));
        }
        position_ += framesRead;
        return Result::Success();
    }

    Result Seek(std::uint64_t frame) override
    {
        if (failSeek_.load()) {
            return Result::Failure(ResultCode::ProcessingFailed, "injected seek failure");
        }
        if (frame > frameCount_) {
            return Result::Failure(ResultCode::InvalidArgument, "seek outside stream");
        }
        position_ = static_cast<std::size_t>(frame);
        return Result::Success();
    }

    bool EndOfFile() const noexcept override
    {
        return position_ >= frameCount_;
    }

    void FailOpen() noexcept { failOpen_.store(true); }
    void FailRead() noexcept { failRead_.store(true); }
    void FailSeek() noexcept { failSeek_.store(true); }

private:
    const std::size_t frameCount_;
    std::size_t position_ = 0;
    std::atomic<bool> failOpen_ = false;
    std::atomic<bool> failRead_ = false;
    std::atomic<bool> failSeek_ = false;
};

AudioFile TestFile()
{
    return AudioFile("streaming-stress.ogg", AudioFileType::Ogg);
}

bool WaitForPosition(StreamingAudioReader& reader, std::uint64_t target)
{
    AudioBuffer buffer(TestFormat, 1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!reader.Read(buffer).Succeeded()) {
            return false;
        }
        if (reader.Position() > target) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

bool WaitForEnd(StreamingAudioReader& reader)
{
    AudioBuffer buffer(TestFormat, 257);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!reader.Read(buffer).Succeeded()) {
            return false;
        }
        if (reader.EndOfFile()) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

}

TEST(StreamingAudioReaderStressTests, ReadsCompleteStreamInBoundedChunks)
{
    StreamingAudioReader reader(std::make_unique<StressDecoder>());
    ASSERT_TRUE(reader.Open(TestFile(), AudioFileType::Ogg).Succeeded());
    ASSERT_TRUE(reader.IsOpen());
    ASSERT_EQ(reader.FrameCount(), TestFrames);

    AudioBuffer buffer(TestFormat, 257);
    std::uint64_t expectedFrame = 0;
    for (std::size_t attempt = 0; attempt < 1000 && !reader.EndOfFile(); ++attempt) {
        const std::uint64_t positionBefore = reader.Position();
        ASSERT_TRUE(reader.Read(buffer).Succeeded());
        const std::uint64_t framesRead = reader.Position() - positionBefore;
        ASSERT_LE(framesRead, buffer.FrameCount());
        for (std::size_t frame = 0; frame < framesRead; ++frame) {
            EXPECT_FLOAT_EQ(buffer.Data()[frame * 2], static_cast<float>(expectedFrame));
            EXPECT_FLOAT_EQ(buffer.Data()[frame * 2 + 1], static_cast<float>(-
                static_cast<std::int64_t>(expectedFrame)));
            ++expectedFrame;
        }
        std::this_thread::yield();
    }

    EXPECT_EQ(expectedFrame, TestFrames);
    EXPECT_TRUE(reader.EndOfFile());
}

TEST(StreamingAudioReaderStressTests, OneHundredSeekTargetsReturnOnlyCurrentStreamData)
{
    StreamingAudioReader reader(std::make_unique<StressDecoder>());
    ASSERT_TRUE(reader.Open(TestFile(), AudioFileType::Ogg).Succeeded());
    AudioBuffer buffer(TestFormat, 73);

    for (std::size_t iteration = 0; iteration < 100; ++iteration) {
        const std::uint64_t target = (iteration * 6151) % TestFrames;
        ASSERT_TRUE(reader.Seek(target).Succeeded()) << iteration;
        bool received = false;
        for (std::size_t attempt = 0; attempt < 1000 && !received; ++attempt) {
            ASSERT_TRUE(reader.Read(buffer).Succeeded());
            const std::uint64_t advanced = reader.Position() - target;
            if (advanced != 0) {
                ASSERT_LE(advanced, buffer.FrameCount());
                EXPECT_FLOAT_EQ(buffer.Data()[0], static_cast<float>(target)) << iteration;
                EXPECT_FLOAT_EQ(buffer.Data()[1], static_cast<float>(-static_cast<std::int64_t>(target)));
                received = true;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(received) << iteration;
    }
}

TEST(StreamingAudioReaderStressTests, OneHundredDestinationSizesPreserveBoundsAndZeroFill)
{
    StreamingAudioReader reader(std::make_unique<StressDecoder>());
    ASSERT_TRUE(reader.Open(TestFile(), AudioFileType::Ogg).Succeeded());

    for (std::size_t iteration = 0; iteration < 100; ++iteration) {
        ASSERT_TRUE(reader.Rewind().Succeeded()) << iteration;
        const std::size_t frameCount = iteration == 99 ? TestFrames + 4096 : iteration * 257;
        AudioBuffer buffer(TestFormat, frameCount);
        const std::uint64_t before = reader.Position();
        ASSERT_TRUE(reader.Read(buffer).Succeeded()) << iteration;
        const std::uint64_t advanced = reader.Position() - before;
        ASSERT_LE(advanced, frameCount);
        for (std::size_t frame = 0; frame < advanced; ++frame) {
            EXPECT_FLOAT_EQ(buffer.Data()[frame * 2], static_cast<float>(before + frame));
            EXPECT_FLOAT_EQ(buffer.Data()[frame * 2 + 1], static_cast<float>(-
                static_cast<std::int64_t>(before + frame)));
        }
        for (std::size_t frame = static_cast<std::size_t>(advanced); frame < frameCount; ++frame) {
            EXPECT_FLOAT_EQ(buffer.Data()[frame * 2], 0.0F);
            EXPECT_FLOAT_EQ(buffer.Data()[frame * 2 + 1], 0.0F);
        }
    }
}

TEST(StreamingAudioReaderStressTests, OneHundredReopensStopThePreviousWorker)
{
    StreamingAudioReader reader(std::make_unique<StressDecoder>());
    AudioBuffer buffer(TestFormat, 1);
    for (std::size_t iteration = 0; iteration < 100; ++iteration) {
        ASSERT_TRUE(reader.Open(TestFile(), AudioFileType::Ogg).Succeeded()) << iteration;
        ASSERT_TRUE(reader.Read(buffer).Succeeded()) << iteration;
        EXPECT_TRUE(reader.IsOpen());
        EXPECT_LE(reader.Position(), TestFrames);
    }
}

TEST(StreamingAudioReaderStressTests, OneHundredSeekBurstsUseTheLatestTarget)
{
    StreamingAudioReader reader(std::make_unique<StressDecoder>());
    ASSERT_TRUE(reader.Open(TestFile(), AudioFileType::Ogg).Succeeded());
    AudioBuffer buffer(TestFormat, 1);

    for (std::size_t iteration = 0; iteration < 100; ++iteration) {
        std::uint64_t target = 0;
        for (std::size_t seek = 0; seek < 100; ++seek) {
            target = (iteration * 313 + seek * 7919) % TestFrames;
            ASSERT_TRUE(reader.Seek(target).Succeeded());
        }
        bool received = false;
        for (std::size_t attempt = 0; attempt < 1000 && !received; ++attempt) {
            ASSERT_TRUE(reader.Read(buffer).Succeeded());
            if (reader.Position() > target) {
                EXPECT_FLOAT_EQ(buffer.Data()[0], static_cast<float>(target));
                received = true;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(received) << iteration;
    }
}

TEST(StreamingAudioReaderStressTests, HandlesExtremeFrameCounts)
{
    const std::array<std::size_t, 7> frameCounts{
        0, 1, 2, 4095, 4096, 4097, TestFrames};
    for (const std::size_t frameCount : frameCounts) {
        for (std::size_t iteration = 0; iteration < 100; ++iteration) {
            StreamingAudioReader reader(std::make_unique<StressDecoder>(frameCount));
            ASSERT_TRUE(reader.Open(TestFile(), AudioFileType::Ogg).Succeeded());
            ASSERT_TRUE(WaitForEnd(reader)) << frameCount << "/" << iteration;
            EXPECT_EQ(reader.Position(), frameCount);
            EXPECT_EQ(reader.FramesRemaining(), 0U);
        }
    }
}

TEST(StreamingAudioReaderStressTests, RejectsEveryInvalidOperationState)
{
    for (std::size_t iteration = 0; iteration < 100; ++iteration) {
        StreamingAudioReader reader(std::make_unique<StressDecoder>());
        AudioBuffer buffer(TestFormat, 1);
        EXPECT_EQ(reader.Read(buffer).Code(), ResultCode::InvalidState);
        EXPECT_EQ(reader.Seek(0).Code(), ResultCode::InvalidState);
        EXPECT_FALSE(reader.EndOfFile());
        EXPECT_EQ(reader.Open(AudioFile("", AudioFileType::Ogg), AudioFileType::Ogg).Code(),
            ResultCode::InvalidArgument);
        EXPECT_FALSE(reader.IsOpen());
        EXPECT_EQ(reader.Open(AudioFile("wrong.wav", AudioFileType::Wav), AudioFileType::Ogg).Code(),
            ResultCode::InvalidArgument);
    }
}

TEST(StreamingAudioReaderStressTests, ConvertsDecoderFailuresToSafeEndOfFile)
{
    for (std::size_t iteration = 0; iteration < 100; ++iteration) {
        auto decoder = std::make_unique<StressDecoder>();
        decoder->FailRead();
        StreamingAudioReader reader(std::move(decoder));
        ASSERT_TRUE(reader.Open(TestFile(), AudioFileType::Ogg).Succeeded()) << iteration;
        AudioBuffer buffer(TestFormat, 128);
        ASSERT_TRUE(WaitForEnd(reader)) << iteration;
        EXPECT_TRUE(reader.EndOfFile());
        EXPECT_LE(reader.Position(), reader.FrameCount());
    }
}

TEST(StreamingAudioReaderStressTests, ConvertsSeekFailuresToSafeEndOfFile)
{
    for (std::size_t iteration = 0; iteration < 100; ++iteration) {
        auto decoder = std::make_unique<StressDecoder>();
        decoder->FailSeek();
        StreamingAudioReader reader(std::move(decoder));
        ASSERT_TRUE(reader.Open(TestFile(), AudioFileType::Ogg).Succeeded()) << iteration;
        ASSERT_TRUE(reader.Seek(iteration % TestFrames).Succeeded());
        AudioBuffer buffer(TestFormat, 128);
        ASSERT_TRUE(WaitForEnd(reader)) << iteration;
        EXPECT_TRUE(reader.EndOfFile());
    }
}
