#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <LuAudio/Audio/Rendering/OfflineRenderer.h>
#include <LuAudio/Audio/Sinks/WavFileWriter.h>
#include <LuAudio/Audio/Sources/OggFileReader.h>

namespace {

using namespace LuAudio::Audio;

class VectorReader final : public IAudioReader {
public:
    VectorReader(AudioFormat format, std::vector<float> samples)
        : format_(format), samples_(std::move(samples))
    {
    }

    Result Read(AudioBuffer& destination) override
    {
        if (!open_) {
            return Result::Failure(ResultCode::InvalidState, "Reader is closed");
        }
        if (destination.Format().sampleRate != format_.sampleRate ||
            destination.Format().channelCount != format_.channelCount) {
            return Result::Failure(ResultCode::InvalidArgument, "Format mismatch");
        }

        const std::size_t sampleOffset = position_ * format_.channelCount;
        const std::size_t samplesToCopy = std::min(
            destination.SampleCount(), samples_.size() - sampleOffset);
        std::copy_n(samples_.data() + sampleOffset, samplesToCopy, destination.Data());
        std::fill(destination.Data() + samplesToCopy, destination.Data() + destination.SampleCount(), 0.0F);
        position_ += samplesToCopy / format_.channelCount;
        return Result::Success();
    }

    Result Seek(std::uint64_t frame) override
    {
        if (frame > FrameCount()) {
            return Result::Failure(ResultCode::InvalidArgument, "Seek outside reader");
        }
        position_ = static_cast<std::size_t>(frame);
        return Result::Success();
    }

    Result Rewind() override { return Seek(0); }
    std::uint64_t Position() const noexcept override { return position_; }
    bool EndOfFile() const noexcept override { return open_ && position_ >= FrameCount(); }
    bool IsOpen() const noexcept override { return open_; }
    const AudioFormat& Format() const noexcept override { return format_; }
    std::uint64_t FrameCount() const noexcept override
    {
        return samples_.size() / format_.channelCount;
    }
    std::uint64_t FramesRemaining() const noexcept override
    {
        return position_ < FrameCount() ? FrameCount() - position_ : 0;
    }
    bool CanSeek() const noexcept override { return true; }

private:
    AudioFormat format_;
    std::vector<float> samples_;
    std::size_t position_ = 0;
    bool open_ = true;
};

class VectorSink final : public IAudioSink {
public:
    Result Open(const AudioFormat& format) override
    {
        format_ = format;
        open_ = true;
        return Result::Success();
    }

    Result Write(const AudioBuffer& buffer) override
    {
        if (!open_ || buffer.Format().channelCount != format_.channelCount) {
            return Result::Failure(ResultCode::InvalidState, "Sink is not open for this format");
        }
        samples_.insert(samples_.end(), buffer.Data(), buffer.Data() + buffer.SampleCount());
        return Result::Success();
    }

    Result Finalize() override
    {
        open_ = false;
        finalized_ = true;
        return Result::Success();
    }

    void Abort() noexcept override
    {
        open_ = false;
        aborted_ = true;
        samples_.clear();
    }

    const AudioFormat& Format() const noexcept override { return format_; }
    bool IsOpen() const noexcept override { return open_; }
    const std::vector<float>& Samples() const noexcept { return samples_; }
    bool Finalized() const noexcept { return finalized_; }
    bool Aborted() const noexcept { return aborted_; }

private:
    AudioFormat format_;
    std::vector<float> samples_;
    bool open_ = false;
    bool finalized_ = false;
    bool aborted_ = false;
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

std::uint32_t ReadUInt32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::filesystem::path TestPath()
{
    return std::filesystem::temp_directory_path() / "LuAudioOfflineRenderingTest.wav";
}

std::filesystem::path SampleOggPath()
{
    return std::filesystem::path(LUAUDIO_TEST_PROJECT_ROOT) /
        "tests" / "Audios" / "sample_4.ogg";
}

}

TEST(OfflineRendererTests, RendersReaderThroughSinkInBlocks)
{
    const AudioFormat format{48000, 1};
    VectorReader reader(format, {0.25F, -0.5F, 0.75F});
    const auto path = TestPath();
    std::filesystem::remove(path);
    WavFileWriter writer(path.string());

    EXPECT_TRUE(OfflineRenderer::Render(reader, writer, nullptr, 2).Succeeded());
    EXPECT_FALSE(writer.IsOpen());

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(file);
    ASSERT_EQ(file.tellg(), 56);
    file.seekg(0);
    std::vector<std::uint8_t> bytes(56);
    ASSERT_TRUE(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
    EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 4), "RIFF");
    EXPECT_EQ(std::string(bytes.begin() + 8, bytes.begin() + 12), "WAVE");
    EXPECT_EQ(ReadUInt32(bytes, 4), 48U);
    EXPECT_EQ(ReadUInt32(bytes, 40), 12U);
    file.close();
    std::filesystem::remove(path);
}

TEST(OfflineRendererTests, RendersSampleOggThroughOfflineRenderer)
{
    OggFileReader reader(nullptr);
    ASSERT_TRUE(reader.Open(AudioFile(SampleOggPath().string(), AudioFileType::Ogg)).Succeeded());
    ASSERT_TRUE(reader.IsOpen());
    ASSERT_GT(reader.FrameCount(), 0U);

    const auto outputPath = std::filesystem::temp_directory_path() /
        "LuAudioOfflineRenderingSampleOgg.wav";
    std::filesystem::remove(outputPath);
    WavFileWriter writer(outputPath.string());

    const Result result = OfflineRenderer::Render(reader, writer, nullptr, 4096);
    EXPECT_TRUE(result.Succeeded()) << result.Message();
    EXPECT_FALSE(writer.IsOpen());
    EXPECT_TRUE(std::filesystem::exists(outputPath));
    EXPECT_GT(std::filesystem::file_size(outputPath), 44U);
    std::filesystem::remove(outputPath);
}

TEST(OfflineRendererTests, RejectsInvalidBlockSizeWithoutOpeningSink)
{
    const AudioFormat format{48000, 1};
    VectorReader reader(format, {0.25F});
    const auto path = TestPath();
    std::filesystem::remove(path);
    WavFileWriter writer(path.string());

    EXPECT_EQ(OfflineRenderer::Render(reader, writer, nullptr, 0).Code(), ResultCode::InvalidArgument);
    EXPECT_FALSE(writer.IsOpen());
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(OfflineRendererTests, MixesSourcesWithEffectsGainAndLongestDuration)
{
    const AudioFormat format{48000, 2};
    auto first = std::make_unique<VectorReader>(format,
        std::vector<float>{0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F});
    auto second = std::make_unique<VectorReader>(format,
        std::vector<float>{0.5F, -0.5F, 0.5F, -0.5F});

    auto sourceEffects = std::make_shared<AudioEffectChain>();
    sourceEffects->Add(std::make_unique<MultiplyEffect>(2.0F));
    auto masterEffects = std::make_shared<AudioEffectChain>();
    masterEffects->Add(std::make_unique<MultiplyEffect>(2.0F));

    OfflineRenderer::Source firstSource;
    firstSource.reader = std::move(first);
    firstSource.gain = 0.5F;
    OfflineRenderer::Source secondSource;
    secondSource.reader = std::move(second);
    secondSource.effects = sourceEffects;

    std::vector<OfflineRenderer::Source> sources;
    sources.push_back(std::move(firstSource));
    sources.push_back(std::move(secondSource));

    VectorSink sink;
    EXPECT_TRUE(OfflineRenderer::RenderSources(
        std::move(sources),
        format,
        sink,
        masterEffects,
        2).Succeeded());

    ASSERT_TRUE(sink.Finalized());
    ASSERT_EQ(sink.Samples().size(), 6U);
    EXPECT_FLOAT_EQ(sink.Samples()[0], 1.0F);
    EXPECT_FLOAT_EQ(sink.Samples()[1], -7.0F / 9.0F);
    EXPECT_FLOAT_EQ(sink.Samples()[2], 1.0F);
    EXPECT_FLOAT_EQ(sink.Samples()[3], -7.0F / 9.0F);
    EXPECT_FLOAT_EQ(sink.Samples()[4], 0.25F);
    EXPECT_FLOAT_EQ(sink.Samples()[5], 0.25F);
}

TEST(WavFileWriterTests, RejectsMismatchedBufferFormat)
{
    const auto path = TestPath();
    std::filesystem::remove(path);
    WavFileWriter writer(path.string());
    ASSERT_TRUE(writer.Open(AudioFormat{48000, 2}).Succeeded());

    AudioBuffer buffer(AudioFormat{44100, 2}, 1);
    EXPECT_EQ(writer.Write(buffer).Code(), ResultCode::InvalidArgument);
    writer.Abort();
    EXPECT_FALSE(std::filesystem::exists(path));
}
