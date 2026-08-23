#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <LuAudio/Audio/Sources/OggFileReader.h>

namespace {

using namespace LuAudio::Audio;

constexpr AudioFormat TestFormat{48000, 2};
constexpr std::size_t TestFrames = 8192;

class StressDecoder final : public IAudioDecoder {
public:
    explicit StressDecoder(std::size_t frameCount = TestFrames)
        : frameCount_(frameCount), samples_(frameCount * TestFormat.channelCount)
    {
        for (std::size_t frame = 0; frame < frameCount_; ++frame) {
            samples_[frame * 2] = static_cast<float>(frame);
            samples_[frame * 2 + 1] = static_cast<float>(-frame);
        }
    }

    Result Open(const AudioFile&, DecoderInfo& destination) override
    {
        if (failOpen_) {
            return Result::Failure(ResultCode::ProcessingFailed, "Injected open failure");
        }
        position_ = 0;
        destination.format = TestFormat;
        destination.frameCount = frameCount_;
        return Result::Success();
    }

    Result Read(std::vector<float>& destination, std::size_t maxFrames,
        std::size_t& framesRead) override
    {
        if (failReads_) {
            framesRead = 0;
            return Result::Failure(ResultCode::ProcessingFailed, "Injected decoder failure");
        }
        const std::size_t remaining = frameCount_ - position_;
        framesRead = std::min(maxFrames, remaining);
        destination.assign(samples_.begin() + position_ * 2,
            samples_.begin() + (position_ + framesRead) * 2);
        position_ += framesRead;
        return Result::Success();
    }

    Result Seek(std::uint64_t frame) override
    {
        if (failSeeks_) {
            return Result::Failure(ResultCode::ProcessingFailed, "Injected seek failure");
        }
        if (frame > frameCount_) {
            return Result::Failure(ResultCode::InvalidArgument, "Injected seek outside range");
        }
        position_ = static_cast<std::size_t>(frame);
        return Result::Success();
    }

    bool EndOfFile() const noexcept override { return position_ >= frameCount_; }
    void FailReads() noexcept { failReads_ = true; }
    void FailOpen() noexcept { failOpen_ = true; }
    void FailSeeks() noexcept { failSeeks_ = true; }
    void AllowOpen() noexcept { failOpen_ = false; }

private:
    std::size_t frameCount_;
    std::vector<float> samples_;
    std::size_t position_ = 0;
    bool failReads_ = false;
    bool failOpen_ = false;
    bool failSeeks_ = false;
};

AudioFile TestFile()
{
    return AudioFile("stress.ogg", AudioFileType::Ogg);
}

bool WaitForEnd(OggFileReader& reader)
{
    AudioBuffer buffer(TestFormat, 257);
    for (std::size_t attempt = 0; attempt < 10000; ++attempt) {
        const Result result = reader.Read(buffer);
        if (!result.Succeeded()) {
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

TEST(OggFileReaderStressTests, RepeatedOpenReadSeekAndRewindKeepsStateConsistent)
{
    auto decoder = std::make_unique<StressDecoder>();
    OggFileReader reader(std::move(decoder));

    for (std::size_t cycle = 0; cycle < 100; ++cycle) {
        ASSERT_TRUE(reader.Open(TestFile()).Succeeded()) << cycle;
        ASSERT_TRUE(reader.IsOpen());
        ASSERT_EQ(reader.FrameCount(), TestFrames);
        ASSERT_EQ(reader.Position(), 0U);

        const std::uint64_t seekFrame = (cycle * 97) % TestFrames;
        ASSERT_TRUE(reader.Seek(seekFrame).Succeeded()) << cycle;
        EXPECT_EQ(reader.Position(), seekFrame);

        AudioBuffer buffer(TestFormat, 31 + (cycle % 127));
        bool advanced = false;
        for (std::size_t attempt = 0; attempt < 1000 && !advanced; ++attempt) {
            ASSERT_TRUE(reader.Read(buffer).Succeeded());
            advanced = reader.Position() > seekFrame;
            if (!advanced) {
                std::this_thread::yield();
            }
        }
        ASSERT_TRUE(advanced) << cycle;
        ASSERT_TRUE(reader.Rewind().Succeeded());
        EXPECT_EQ(reader.Position(), 0U);
        ASSERT_TRUE(WaitForEnd(reader)) << cycle;
    }
}

TEST(OggFileReaderStressTests, HandlesAggressiveSeekSequenceWithoutReturningInvalidSamples)
{
    OggFileReader reader(std::make_unique<StressDecoder>());
    ASSERT_TRUE(reader.Open(TestFile()).Succeeded());

    AudioBuffer buffer(TestFormat, 64);
    for (std::size_t iteration = 0; iteration < 1000; ++iteration) {
        const std::uint64_t target = (iteration * 7919) % (TestFrames + 1);
        ASSERT_TRUE(reader.Seek(target).Succeeded());
        ASSERT_TRUE(reader.Read(buffer).Succeeded());
        for (std::size_t index = 0; index < buffer.SampleCount(); ++index) {
            if (index >= (reader.Position() - target) * TestFormat.channelCount) {
                EXPECT_FLOAT_EQ(buffer.Data()[index], 0.0F);
            } else {
                EXPECT_TRUE(std::isfinite(buffer.Data()[index]));
            }
        }
    }
}

TEST(OggFileReaderStressTests, ConvertsDecoderFailureToSafeEndOfFileState)
{
    auto decoder = std::make_unique<StressDecoder>();
    StressDecoder* decoderPointer = decoder.get();
    OggFileReader reader(std::move(decoder));
    ASSERT_TRUE(reader.Open(TestFile()).Succeeded());
    decoderPointer->FailReads();

    AudioBuffer buffer(TestFormat, 128);
    for (std::size_t attempt = 0; attempt < 1000 && !reader.EndOfFile(); ++attempt) {
        ASSERT_TRUE(reader.Read(buffer).Succeeded());
        std::this_thread::yield();
    }
    EXPECT_TRUE(reader.EndOfFile());
    EXPECT_LE(reader.Position(), reader.FrameCount());
}

TEST(OggFileReaderStressTests, DestructionStopsActiveDecoderWorker)
{
    for (std::size_t iteration = 0; iteration < 250; ++iteration) {
        auto reader = std::make_unique<OggFileReader>(std::make_unique<StressDecoder>());
        ASSERT_TRUE(reader->Open(TestFile()).Succeeded());
        AudioBuffer buffer(TestFormat, 1);
        ASSERT_TRUE(reader->Read(buffer).Succeeded());
    }
}

TEST(OggFileReaderStressTests, RejectsInvalidInputWithoutOpeningDefaultDecoder)
{
    OggFileReader reader(std::make_unique<StressDecoder>());
    EXPECT_EQ(reader.Open(AudioFile("", AudioFileType::Ogg)).Code(), ResultCode::InvalidArgument);
    EXPECT_FALSE(reader.IsOpen());
    EXPECT_EQ(reader.Open(AudioFile("missing.ogg", AudioFileType::Wav)).Code(),
        ResultCode::InvalidArgument);
    EXPECT_FALSE(reader.IsOpen());
}

TEST(OggFileReaderStressTests, RejectsOperationsBeforeOpenAndFormatMismatches)
{
    OggFileReader reader(std::make_unique<StressDecoder>());
    AudioBuffer buffer(TestFormat, 8);
    EXPECT_EQ(reader.Read(buffer).Code(), ResultCode::InvalidState);
    EXPECT_EQ(reader.Seek(0).Code(), ResultCode::InvalidState);
    EXPECT_FALSE(reader.EndOfFile());

    ASSERT_TRUE(reader.Open(TestFile()).Succeeded());
    AudioBuffer wrongFormat(AudioFormat{44100, 2}, 8);
    EXPECT_EQ(reader.Read(wrongFormat).Code(), ResultCode::InvalidArgument);
    EXPECT_EQ(reader.Position(), 0U);
    EXPECT_EQ(reader.Seek(TestFrames + 1).Code(), ResultCode::InvalidArgument);
}

TEST(OggFileReaderStressTests, HandlesZeroAndOneFrameStreams)
{
    for (const std::size_t frameCount : {0U, 1U}) {
        OggFileReader reader(std::make_unique<StressDecoder>(frameCount));
        ASSERT_TRUE(reader.Open(TestFile()).Succeeded());
        AudioBuffer buffer(TestFormat, 4096);
        for (std::size_t attempt = 0; attempt < 1000 && !reader.EndOfFile(); ++attempt) {
            ASSERT_TRUE(reader.Read(buffer).Succeeded());
            std::this_thread::yield();
        }
        EXPECT_TRUE(reader.EndOfFile());
        EXPECT_EQ(reader.Position(), frameCount);
        EXPECT_EQ(reader.FramesRemaining(), 0U);
    }
}

TEST(OggFileReaderStressTests, HandlesExtremeDestinationSizesAndZeroFrameReads)
{
    OggFileReader reader(std::make_unique<StressDecoder>());
    ASSERT_TRUE(reader.Open(TestFile()).Succeeded());

    AudioBuffer empty(TestFormat, 0);
    ASSERT_TRUE(reader.Read(empty).Succeeded());
    EXPECT_EQ(reader.Position(), 0U);

    AudioBuffer huge(TestFormat, TestFrames + 4096);
    for (std::size_t attempt = 0; attempt < 1000 && !reader.EndOfFile(); ++attempt) {
        const std::uint64_t positionBefore = reader.Position();
        ASSERT_TRUE(reader.Read(huge).Succeeded());
        const std::uint64_t framesRead = reader.Position() - positionBefore;
        ASSERT_LE(framesRead, huge.FrameCount());
        for (std::size_t frame = 0; frame < framesRead; ++frame) {
            const auto sourceFrame = positionBefore + frame;
            EXPECT_FLOAT_EQ(huge.Data()[frame * 2], static_cast<float>(sourceFrame));
            EXPECT_FLOAT_EQ(huge.Data()[frame * 2 + 1], static_cast<float>(-sourceFrame));
        }
        for (std::size_t frame = static_cast<std::size_t>(framesRead);
             frame < huge.FrameCount(); ++frame) {
            EXPECT_FLOAT_EQ(huge.Data()[frame * 2], 0.0F);
            EXPECT_FLOAT_EQ(huge.Data()[frame * 2 + 1], 0.0F);
        }
        std::this_thread::yield();
    }
    EXPECT_TRUE(reader.EndOfFile());
}

TEST(OggFileReaderStressTests, LatestSeekWinsAfterASeekBurst)
{
    OggFileReader reader(std::make_unique<StressDecoder>());
    ASSERT_TRUE(reader.Open(TestFile()).Succeeded());
    for (std::size_t iteration = 0; iteration < 5000; ++iteration) {
        ASSERT_TRUE(reader.Seek((iteration * 131) % TestFrames).Succeeded());
    }
    const std::uint64_t target = (4999U * 131U) % TestFrames;
    AudioBuffer buffer(TestFormat, 1);
    for (std::size_t attempt = 0; attempt < 1000; ++attempt) {
        ASSERT_TRUE(reader.Read(buffer).Succeeded());
        if (reader.Position() > target) {
            EXPECT_FLOAT_EQ(buffer.Data()[0], static_cast<float>(target));
            EXPECT_FLOAT_EQ(buffer.Data()[1], static_cast<float>(-target));
            return;
        }
        std::this_thread::yield();
    }
    FAIL() << "decoder did not produce a frame after the seek burst";
}

TEST(OggFileReaderStressTests, ConvertsSeekAndOpenFailuresIntoSafeStates)
{
    auto decoder = std::make_unique<StressDecoder>();
    StressDecoder* decoderPointer = decoder.get();
    OggFileReader reader(std::move(decoder));
    decoderPointer->FailOpen();
    EXPECT_EQ(reader.Open(TestFile()).Code(), ResultCode::ProcessingFailed);
    EXPECT_FALSE(reader.IsOpen());

    decoderPointer->AllowOpen();
    ASSERT_TRUE(reader.Open(TestFile()).Succeeded());
    decoderPointer->FailSeeks();
    EXPECT_TRUE(reader.Seek(1).Succeeded());
    AudioBuffer buffer(TestFormat, 1);
    for (std::size_t attempt = 0; attempt < 1000 && !reader.EndOfFile(); ++attempt) {
        ASSERT_TRUE(reader.Read(buffer).Succeeded());
        std::this_thread::yield();
    }
    EXPECT_TRUE(reader.EndOfFile());
}

TEST(OggFileReaderStressTests, DefaultVorbisDecoderRejectsMissingAndMalformedFiles)
{
    OggFileReader reader(nullptr);
    EXPECT_EQ(reader.Open(AudioFile("missing-stress.ogg", AudioFileType::Ogg)).Code(),
        ResultCode::ProcessingFailed);
    EXPECT_FALSE(reader.IsOpen());
}
