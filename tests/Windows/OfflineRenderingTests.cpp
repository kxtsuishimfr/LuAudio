#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <LuAudio/Audio/Rendering/OfflineRenderer.h>
#include <LuAudio/Audio/Sinks/WavFileWriter.h>

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
