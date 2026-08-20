#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include <LuAudio/Audio/Playback/AudioMixer.h>

namespace {

using namespace LuAudio::Audio;

class TestReader final : public IAudioReader {
public:
    explicit TestReader(AudioFormat format = {})
        : format_(format)
    {
    }

    Result Read(AudioBuffer& destination) override
    {
        ++readCount_;
        if (readFailure_) {
            return Result::Failure(ResultCode::ProcessingFailed, "Test reader read failed");
        }
        for (std::size_t index = 0; index < destination.SampleCount(); ++index) {
            destination.Data()[index] = sampleValue_;
        }
        return Result::Success();
    }

    Result Seek(std::uint64_t frame) override
    {
        ++seekCount_;
        position_ = frame;
        return Result::Success();
    }

    Result Rewind() override
    {
        position_ = 0;
        return Result::Success();
    }

    std::uint64_t Position() const noexcept override
    {
        return position_;
    }

    bool EndOfFile() const noexcept override
    {
        return endOfFile_;
    }

    bool IsOpen() const noexcept override
    {
        return open_;
    }

    const AudioFormat& Format() const noexcept override
    {
        return format_;
    }

    std::uint64_t FrameCount() const noexcept override
    {
        return frameCount_;
    }

    std::uint64_t FramesRemaining() const noexcept override
    {
        return position_ < frameCount_ ? frameCount_ - position_ : 0;
    }

    bool CanSeek() const noexcept override
    {
        return canSeek_;
    }

    void SetCanSeek(bool canSeek) noexcept
    {
        canSeek_ = canSeek;
    }

    void SetEndOfFile(bool endOfFile) noexcept
    {
        endOfFile_ = endOfFile;
    }

    void SetSampleValue(float sampleValue) noexcept
    {
        sampleValue_ = sampleValue;
    }

    std::size_t ReadCount() const noexcept
    {
        return readCount_;
    }

    std::size_t SeekCount() const noexcept
    {
        return seekCount_;
    }

private:
    AudioFormat format_;
    std::uint64_t position_ = 0;
    std::uint64_t frameCount_ = 64;
    bool canSeek_ = true;
    bool endOfFile_ = false;
    bool open_ = true;
    bool readFailure_ = false;
    float sampleValue_ = 0.0F;
    std::size_t readCount_ = 0;
    std::size_t seekCount_ = 0;
};

class MultiplyEffect final : public IAudioEffect {
public:
    explicit MultiplyEffect(float multiplier)
        : multiplier_(multiplier)
    {
    }

    bool Process(AudioBuffer& buffer) noexcept override
    {
        for (std::size_t index = 0; index < buffer.SampleCount(); ++index) {
            buffer.Data()[index] *= multiplier_;
        }
        return true;
    }

private:
    float multiplier_;
};

class TestBackend final : public IAudioBackend {
public:
    Result Open(const AudioStreamConfig& requestedConfig) override
    {
        if (!requestedConfig.IsValid()) {
            return Result::Failure(ResultCode::InvalidArgument, "Invalid configuration");
        }
        actualConfig_ = requestedConfig;
        open_ = true;
        return Result::Success();
    }

    Result Start() override
    {
        if (!open_) {
            return Result::Failure(ResultCode::InvalidState, "Backend is not open");
        }
        started_ = true;
        return Result::Success();
    }

    Result Stop() override
    {
        started_ = false;
        return Result::Success();
    }

    void Close() noexcept override
    {
        started_ = false;
        open_ = false;
    }

    void SetCallback(AudioCallback callback) override
    {
        callback_ = std::move(callback);
    }

    const AudioStreamConfig& ActualConfig() const noexcept override
    {
        return actualConfig_;
    }

    void Render(AudioBuffer& buffer)
    {
        ASSERT_TRUE(static_cast<bool>(callback_));
        callback_(buffer);
    }

    bool HasCallback() const noexcept
    {
        return static_cast<bool>(callback_);
    }

    bool IsStarted() const noexcept
    {
        return started_;
    }

private:
    AudioStreamConfig actualConfig_;
    AudioCallback callback_;
    bool open_ = false;
    bool started_ = false;
};

std::unique_ptr<TestReader> MakeReader(AudioFormat format = {})
{
    return std::make_unique<TestReader>(format);
}

}

TEST(AudioMixerTests, RequiresOpenBeforeOperations)
{
    TestBackend backend;
    AudioMixer mixer(backend, 2);
    AudioMixer::SourceId sourceId = 0;

    EXPECT_EQ(mixer.Start().Code(), ResultCode::InvalidState);
    EXPECT_EQ(mixer.Stop().Code(), ResultCode::InvalidState);
    EXPECT_EQ(mixer.AddSource(MakeReader(), sourceId).Code(), ResultCode::InvalidState);
    EXPECT_EQ(mixer.RemoveSource(1).Code(), ResultCode::InvalidArgument);
}

TEST(AudioMixerTests, OpensStartsAndClosesBackend)
{
    TestBackend backend;
    AudioMixer mixer(backend, 2);
    AudioBuffer output({}, 2);
    output.Data()[0] = 1.0F;

    ASSERT_TRUE(mixer.Open({}).Succeeded());
    EXPECT_TRUE(backend.HasCallback());
    ASSERT_TRUE(mixer.Start().Succeeded());
    EXPECT_TRUE(backend.IsStarted());

    backend.Render(output);
    EXPECT_FLOAT_EQ(output.Data()[0], 0.0F);

    ASSERT_TRUE(mixer.Stop().Succeeded());
    EXPECT_FALSE(backend.IsStarted());
    mixer.Close();
    EXPECT_FALSE(backend.HasCallback());
}

TEST(AudioMixerTests, MixesSourcesWithGainAndMonoRemapping)
{
    TestBackend backend;
    AudioMixer mixer(backend, 2);
    ASSERT_TRUE(mixer.Open({}).Succeeded());

    auto monoReader = MakeReader(AudioFormat{48000, 1});
    monoReader->SetSampleValue(0.25F);
    auto stereoReader = MakeReader();
    stereoReader->SetSampleValue(0.5F);
    AudioMixer::SourceId monoId = 0;
    AudioMixer::SourceId stereoId = 0;
    ASSERT_TRUE(mixer.AddSource(std::move(monoReader), monoId).Succeeded());
    ASSERT_TRUE(mixer.AddSource(std::move(stereoReader), stereoId).Succeeded());
    ASSERT_TRUE(mixer.SetSourceGain(stereoId, 0.5F).Succeeded());

    AudioBuffer output({}, 2);
    backend.Render(output);

    for (std::size_t index = 0; index < output.SampleCount(); ++index) {
        EXPECT_FLOAT_EQ(output.Data()[index], 0.5F);
    }
}

TEST(AudioMixerTests, PausedSourcesAreNotRead)
{
    TestBackend backend;
    AudioMixer mixer(backend, 1);
    ASSERT_TRUE(mixer.Open({}).Succeeded());
    auto reader = MakeReader();
    TestReader* readerPointer = reader.get();
    AudioMixer::SourceId sourceId = 0;
    ASSERT_TRUE(mixer.AddSource(std::move(reader), sourceId).Succeeded());
    ASSERT_TRUE(mixer.SetSourcePaused(sourceId, true).Succeeded());

    AudioBuffer output({}, 2);
    backend.Render(output);

    EXPECT_EQ(readerPointer->ReadCount(), 0U);
    for (std::size_t index = 0; index < output.SampleCount(); ++index) {
        EXPECT_FLOAT_EQ(output.Data()[index], 0.0F);
    }
}

TEST(AudioMixerTests, ProcessesSourceAndMasterEffects)
{
    TestBackend backend;
    AudioMixer mixer(backend, 1);
    ASSERT_TRUE(mixer.Open({}).Succeeded());

    auto reader = MakeReader();
    reader->SetSampleValue(0.25F);
    AudioMixer::SourceId sourceId = 0;
    ASSERT_TRUE(mixer.AddSource(std::move(reader), sourceId).Succeeded());

    auto sourceEffects = std::make_shared<AudioEffectChain>();
    sourceEffects->Add(std::make_unique<MultiplyEffect>(2.0F));
    ASSERT_TRUE(mixer.SetSourceEffects(sourceId, sourceEffects).Succeeded());

    auto masterEffects = std::make_shared<AudioEffectChain>();
    masterEffects->Add(std::make_unique<MultiplyEffect>(2.0F));
    mixer.SetMasterEffectChain(masterEffects);

    AudioBuffer output({}, 2);
    backend.Render(output);

    for (std::size_t index = 0; index < output.SampleCount(); ++index) {
        EXPECT_FLOAT_EQ(output.Data()[index], 1.0F);
    }
}

TEST(AudioMixerTests, AppliesDeferredSeekBeforeReading)
{
    TestBackend backend;
    AudioMixer mixer(backend, 1);
    ASSERT_TRUE(mixer.Open({}).Succeeded());
    auto reader = MakeReader();
    TestReader* readerPointer = reader.get();
    AudioMixer::SourceId sourceId = 0;
    ASSERT_TRUE(mixer.AddSource(std::move(reader), sourceId).Succeeded());
    ASSERT_TRUE(mixer.SeekSource(sourceId, 12).Succeeded());

    AudioBuffer output({}, 2);
    backend.Render(output);

    EXPECT_EQ(readerPointer->SeekCount(), 1U);
    EXPECT_EQ(readerPointer->Position(), 12U);
    EXPECT_EQ(readerPointer->ReadCount(), 1U);
}

TEST(AudioMixerTests, RejectsUnsupportedReaderFormats)
{
    TestBackend backend;
    AudioMixer mixer(backend, 2);
    ASSERT_TRUE(mixer.Open({}).Succeeded());

    auto format = AudioFormat{};
    format.sampleRate = 44100;
    AudioMixer::SourceId sourceId = 0;

    EXPECT_EQ(mixer.AddSource(MakeReader(format), sourceId).Code(), ResultCode::InvalidArgument);
    EXPECT_EQ(mixer.ActiveSourceCount(), 0U);
}

TEST(AudioMixerTests, EnforcesCapacityAndAssignsDistinctIds)
{
    TestBackend backend;
    AudioMixer mixer(backend, 2);
    ASSERT_TRUE(mixer.Open({}).Succeeded());
    AudioMixer::SourceId firstId = 0;
    AudioMixer::SourceId secondId = 0;
    AudioMixer::SourceId thirdId = 0;

    ASSERT_TRUE(mixer.AddSource(MakeReader(), firstId).Succeeded());
    ASSERT_TRUE(mixer.AddSource(MakeReader(), secondId).Succeeded());
    EXPECT_NE(firstId, secondId);
    EXPECT_EQ(mixer.ActiveSourceCount(), 2U);
    EXPECT_EQ(mixer.AddSource(MakeReader(), thirdId).Code(), ResultCode::InvalidState);

    ASSERT_TRUE(mixer.RemoveSource(firstId).Succeeded());
    EXPECT_EQ(mixer.ActiveSourceCount(), 1U);
    ASSERT_TRUE(mixer.AddSource(MakeReader(), thirdId).Succeeded());
    EXPECT_NE(secondId, thirdId);
}

TEST(AudioMixerTests, ValidatesSourceOperations)
{
    TestBackend backend;
    AudioMixer mixer(backend, 2);
    ASSERT_TRUE(mixer.Open({}).Succeeded());
    AudioMixer::SourceId sourceId = 0;
    auto reader = MakeReader();
    TestReader* readerPointer = reader.get();
    ASSERT_TRUE(mixer.AddSource(std::move(reader), sourceId).Succeeded());

    EXPECT_TRUE(mixer.SetSourceGain(sourceId, 0.5F).Succeeded());
    EXPECT_EQ(mixer.SetSourceGain(sourceId, std::numeric_limits<float>::quiet_NaN()).Code(), ResultCode::InvalidArgument);
    EXPECT_TRUE(mixer.SetSourcePaused(sourceId, true).Succeeded());
    EXPECT_TRUE(mixer.SeekSource(sourceId, 12).Succeeded());
    EXPECT_FALSE(mixer.IsSourceFinished(sourceId));

    readerPointer->SetCanSeek(false);
    EXPECT_EQ(mixer.SeekSource(sourceId, 12).Code(), ResultCode::InvalidArgument);
    readerPointer->SetEndOfFile(true);
    EXPECT_TRUE(mixer.IsSourceFinished(sourceId));
    EXPECT_EQ(mixer.RemoveSource(sourceId).Code(), ResultCode::Success);
}

TEST(AudioMixerTests, CloseIsIdempotent)
{
    TestBackend backend;
    AudioMixer mixer(backend, 1);

    mixer.Close();
    ASSERT_TRUE(mixer.Open({}).Succeeded());
    mixer.Close();
    mixer.Close();
}
