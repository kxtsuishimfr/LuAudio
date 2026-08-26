#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <LuAudio/Audio/Rendering/OfflineRenderer.h>

namespace {

using namespace LuAudio::Audio;

class VectorReader final : public IAudioReader {
public:
    VectorReader(AudioFormat format, std::size_t frameCount, float value)
        : format_(format), frameCount_(frameCount), value_(value)
    {
    }

    Result Read(AudioBuffer& destination) override
    {
        const auto frames = std::min<std::size_t>(
            destination.FrameCount(), frameCount_ - position_);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < format_.channelCount; ++channel) {
                destination.Data()[frame * format_.channelCount + channel] = value_;
            }
        }
        position_ += frames;
        destination.Resize(frames);
        return Result::Success();
    }

    Result Seek(std::uint64_t frame) override
    {
        if (frame > frameCount_) {
            return Result::Failure(ResultCode::InvalidArgument, "Seek outside test reader");
        }
        position_ = static_cast<std::size_t>(frame);
        return Result::Success();
    }

    Result Rewind() override { return Seek(0); }
    std::uint64_t Position() const noexcept override { return position_; }
    bool EndOfFile() const noexcept override { return position_ >= frameCount_; }
    bool IsOpen() const noexcept override { return true; }
    const AudioFormat& Format() const noexcept override { return format_; }
    std::uint64_t FrameCount() const noexcept override { return frameCount_; }
    std::uint64_t FramesRemaining() const noexcept override
    {
        return position_ < frameCount_ ? frameCount_ - position_ : 0;
    }
    bool CanSeek() const noexcept override { return true; }

private:
    AudioFormat format_;
    std::size_t frameCount_;
    float value_;
    std::size_t position_ = 0;
};

class TestSink final : public IAudioSink {
public:
    Result Open(const AudioFormat& format) override
    {
        format_ = format;
        open_ = true;
        return openFailure_ ? Result::Failure(ResultCode::InvalidState, "Test sink open failed")
                            : Result::Success();
    }

    Result Write(const AudioBuffer& buffer) override
    {
        if (!open_) {
            return Result::Failure(ResultCode::InvalidState, "Test sink is closed");
        }
        if (writeFailure_) {
            return Result::Failure(ResultCode::ProcessingFailed, "Test sink write failed");
        }
        samples_.insert(samples_.end(), buffer.Data(), buffer.Data() + buffer.SampleCount());
        return Result::Success();
    }

    Result Finalize() override
    {
        open_ = false;
        finalized_ = !finalizeFailure_;
        return finalizeFailure_
            ? Result::Failure(ResultCode::ProcessingFailed, "Test sink finalize failed")
            : Result::Success();
    }

    void Abort() noexcept override
    {
        open_ = false;
        aborted_ = true;
        samples_.clear();
    }

    const AudioFormat& Format() const noexcept override { return format_; }
    bool IsOpen() const noexcept override { return open_; }
    void FailOpen() noexcept { openFailure_ = true; }
    void FailWrite() noexcept { writeFailure_ = true; }
    void FailFinalize() noexcept { finalizeFailure_ = true; }
    bool Aborted() const noexcept { return aborted_; }
    bool Finalized() const noexcept { return finalized_; }
    const std::vector<float>& Samples() const noexcept { return samples_; }

private:
    AudioFormat format_;
    std::vector<float> samples_;
    bool open_ = false;
    bool openFailure_ = false;
    bool writeFailure_ = false;
    bool finalizeFailure_ = false;
    bool aborted_ = false;
    bool finalized_ = false;
};

class FailingEffect final : public IAudioEffect {
public:
    bool Process(AudioBuffer&) noexcept override { return false; }
};

class NonFiniteEffect final : public IAudioEffect {
public:
    bool Process(AudioBuffer& buffer) noexcept override
    {
        buffer.Data()[0] = std::numeric_limits<float>::quiet_NaN();
        return true;
    }
};

std::unique_ptr<IAudioReader> MakeReader(AudioFormat format, std::size_t frames, float value)
{
    return std::make_unique<VectorReader>(format, frames, value);
}

std::vector<OfflineRenderer::Source> SingleSource(OfflineRenderer::Source source)
{
    std::vector<OfflineRenderer::Source> sources;
    sources.push_back(std::move(source));
    return sources;
}

}

TEST(OfflineRendererStressTests, RejectsInvalidInputsBeforeOpeningSink)
{
    TestSink sink;
    EXPECT_EQ(OfflineRenderer::RenderSources({}, AudioFormat{}, sink).Code(),
        ResultCode::InvalidArgument);
    EXPECT_FALSE(sink.IsOpen());

    OfflineRenderer::Source nullSource;
    EXPECT_EQ(OfflineRenderer::RenderSources(
        SingleSource(std::move(nullSource)), AudioFormat{}, sink).Code(),
        ResultCode::InvalidArgument);
    EXPECT_FALSE(sink.IsOpen());

    OfflineRenderer::Source invalidGain;
    invalidGain.reader = MakeReader(AudioFormat{}, 1, 0.0F);
    invalidGain.gain = std::numeric_limits<float>::infinity();
    EXPECT_EQ(OfflineRenderer::RenderSources(
        SingleSource(std::move(invalidGain)), AudioFormat{}, sink).Code(),
        ResultCode::InvalidArgument);
    EXPECT_FALSE(sink.IsOpen());
}

TEST(OfflineRendererStressTests, HandlesExtremeSourceCountAndGainWithLimiter)
{
    const AudioFormat format{48000, 2};
    std::vector<OfflineRenderer::Source> sources;
    sources.reserve(256);
    for (std::size_t index = 0; index < 256; ++index) {
        OfflineRenderer::Source source;
        source.reader = MakeReader(format, 17, 1000.0F);
        source.gain = 1000.0F;
        sources.push_back(std::move(source));
    }

    TestSink sink;
    ASSERT_TRUE(OfflineRenderer::RenderSources(
        std::move(sources), format, sink, nullptr, 1).Succeeded());
    ASSERT_EQ(sink.Samples().size(), 17U * format.channelCount);
    for (const float sample : sink.Samples()) {
        EXPECT_TRUE(std::isfinite(sample));
        EXPECT_FLOAT_EQ(sample, 1.0F);
    }
}

TEST(OfflineRendererStressTests, AbortsWhenSourceEffectFails)
{
    OfflineRenderer::Source source;
    source.reader = MakeReader(AudioFormat{}, 4, 0.25F);
    auto effects = std::make_shared<AudioEffectChain>();
    effects->Add(std::make_unique<FailingEffect>());
    source.effects = effects;

    TestSink sink;
    const auto result = OfflineRenderer::RenderSources(
        SingleSource(std::move(source)), AudioFormat{}, sink, nullptr, 2);
    EXPECT_EQ(result.Code(), ResultCode::ProcessingFailed);
    EXPECT_TRUE(sink.Aborted());
    EXPECT_TRUE(sink.Samples().empty());
}

TEST(OfflineRendererStressTests, AbortsWhenEffectsProduceNonFiniteSamples)
{
    OfflineRenderer::Source source;
    source.reader = MakeReader(AudioFormat{}, 4, 0.25F);
    auto effects = std::make_shared<AudioEffectChain>();
    effects->Add(std::make_unique<NonFiniteEffect>());
    source.effects = effects;

    TestSink sink;
    const auto result = OfflineRenderer::RenderSources(
        SingleSource(std::move(source)), AudioFormat{}, sink, nullptr, 2);
    EXPECT_EQ(result.Code(), ResultCode::ProcessingFailed);
    EXPECT_TRUE(sink.Aborted());
}

TEST(OfflineRendererStressTests, AbortsWhenSourceContainsNonFiniteSamples)
{
    OfflineRenderer::Source source;
    source.reader = MakeReader(
        AudioFormat{}, 2, std::numeric_limits<float>::quiet_NaN());

    TestSink sink;
    const auto result = OfflineRenderer::RenderSources(
        SingleSource(std::move(source)), AudioFormat{}, sink, nullptr, 2);
    EXPECT_EQ(result.Code(), ResultCode::ProcessingFailed);
    EXPECT_TRUE(sink.Aborted());
}

TEST(OfflineRendererStressTests, PropagatesSinkFailuresAndAborts)
{
    OfflineRenderer::Source source;
    source.reader = MakeReader(AudioFormat{}, 4, 0.25F);

    TestSink writeSink;
    writeSink.FailWrite();
    EXPECT_EQ(OfflineRenderer::RenderSources(
        SingleSource(std::move(source)), AudioFormat{}, writeSink, nullptr, 2).Code(),
        ResultCode::ProcessingFailed);
    EXPECT_TRUE(writeSink.Aborted());

    OfflineRenderer::Source finalizeSource;
    finalizeSource.reader = MakeReader(AudioFormat{}, 4, 0.25F);
    TestSink finalizeSink;
    finalizeSink.FailFinalize();
    EXPECT_EQ(OfflineRenderer::RenderSources(
        SingleSource(std::move(finalizeSource)), AudioFormat{}, finalizeSink, nullptr, 2).Code(),
        ResultCode::ProcessingFailed);
    EXPECT_TRUE(finalizeSink.Aborted());
}
