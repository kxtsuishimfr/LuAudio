#include <LuAudio/Common.h>

#include <LuAudio/Audio/Sources/WavFileReader.h>

namespace LuAudio::Audio {

namespace {

std::uint16_t ReadUInt16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::uint32_t ReadUInt32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        static_cast<std::uint32_t>(bytes[offset + 1] << 8) |
        static_cast<std::uint32_t>(bytes[offset + 2] << 16) |
        static_cast<std::uint32_t>(bytes[offset + 3] << 24);
}

bool Matches(const std::vector<std::uint8_t>& bytes, std::size_t offset, const char (&value)[5])
{
    return std::memcmp(bytes.data() + offset, value, 4) == 0;
}

Result InvalidFile(const char* message)
{
    return Result::Failure(ResultCode::InvalidArgument, message);
}

}

Result WavFileReader::Open(const AudioFile& file)
{
    if (!file.IsValid()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio file path is empty");
    }
    if (file.Type() != AudioFileType::Wav) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio file type is not supported");
    }

    return Open(file.Path());
}

Result WavFileReader::Open(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return Result::Failure(ResultCode::InvalidArgument, "Unable to open WAV file");
    }

    const std::streamoff fileSize = file.tellg();
    if (fileSize < 12 || static_cast<std::uintmax_t>(fileSize) > std::numeric_limits<std::size_t>::max()) {
        return InvalidFile("Invalid WAV file size");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
        return InvalidFile("Unable to read WAV file");
    }

    if (!Matches(bytes, 0, "RIFF") || !Matches(bytes, 8, "WAVE")) {
        return InvalidFile("WAV file is missing RIFF/WAVE headers");
    }

    const std::uint32_t riffSize = ReadUInt32(bytes, 4);
    if (riffSize > bytes.size() - 8) {
        return InvalidFile("WAV RIFF chunk exceeds file size");
    }

    std::size_t cursor = 12;
    std::size_t dataOffset = 0;
    std::size_t dataSize = 0;
    std::uint16_t audioFormat = 0;
    std::uint16_t channelCount = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t blockAlignment = 0;
    std::uint16_t bitsPerSample = 0;
    bool foundFormat = false;
    bool foundData = false;

    while (cursor + 8 <= bytes.size()) {
        const std::uint32_t chunkSize = ReadUInt32(bytes, cursor + 4);
        const std::size_t chunkData = cursor + 8;
        if (chunkSize > bytes.size() - chunkData) {
            return InvalidFile("WAV chunk exceeds file size");
        }

        if (Matches(bytes, cursor, "fmt ")) {
            if (chunkSize < 16) {
                return InvalidFile("WAV format chunk is too small");
            }
            audioFormat = ReadUInt16(bytes, chunkData);
            channelCount = ReadUInt16(bytes, chunkData + 2);
            sampleRate = ReadUInt32(bytes, chunkData + 4);
            blockAlignment = ReadUInt16(bytes, chunkData + 12);
            bitsPerSample = ReadUInt16(bytes, chunkData + 14);
            foundFormat = true;
        } else if (Matches(bytes, cursor, "data")) {
            dataOffset = chunkData;
            dataSize = chunkSize;
            foundData = true;
        }

        cursor = chunkData + chunkSize + (chunkSize & 1U);
    }

    if (!foundFormat || !foundData || channelCount == 0 || sampleRate == 0) {
        return InvalidFile("WAV file is missing format or data");
    }
    if (audioFormat != 1 && audioFormat != 3) {
        return InvalidFile("WAV format is not PCM or IEEE float");
    }
    if (audioFormat == 3 && bitsPerSample != 32) {
        return InvalidFile("WAV IEEE float format must be 32-bit");
    }
    if (audioFormat == 1 && bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
        return InvalidFile("WAV PCM format must be 8, 16, 24, or 32-bit");
    }

    const std::size_t bytesPerSample = (bitsPerSample + 7U) / 8U;
    const std::size_t expectedAlignment = static_cast<std::size_t>(channelCount) * bytesPerSample;
    if (blockAlignment != expectedAlignment || dataSize % blockAlignment != 0) {
        return InvalidFile("WAV block alignment is invalid");
    }

    const std::size_t frameCount = dataSize / blockAlignment;
    if (frameCount > std::numeric_limits<std::size_t>::max() / channelCount) {
        return InvalidFile("WAV sample count is too large");
    }

    samples_.assign(frameCount * channelCount, 0.0F);
    for (std::size_t sampleIndex = 0; sampleIndex < samples_.size(); ++sampleIndex) {
        const std::size_t sampleOffset = dataOffset + sampleIndex * bytesPerSample;
        float sample = 0.0F;
        if (audioFormat == 3) {
            std::memcpy(&sample, bytes.data() + sampleOffset, sizeof(float));
        } else if (bitsPerSample == 8) {
            sample = (static_cast<float>(bytes[sampleOffset]) - 128.0F) / 128.0F;
        } else if (bitsPerSample == 16) {
            const auto value = static_cast<std::int16_t>(ReadUInt16(bytes, sampleOffset));
            sample = static_cast<float>(value) / 32768.0F;
        } else if (bitsPerSample == 24) {
            std::int32_t value = static_cast<std::int32_t>(bytes[sampleOffset]) |
                (static_cast<std::int32_t>(bytes[sampleOffset + 1]) << 8) |
                (static_cast<std::int32_t>(bytes[sampleOffset + 2]) << 16);
            if ((value & 0x00800000) != 0) {
                value |= 0xFF000000;
            }
            sample = static_cast<float>(value) / 8388608.0F;
        } else {
            const auto value = static_cast<std::int32_t>(ReadUInt32(bytes, sampleOffset));
            sample = static_cast<float>(value) / 2147483648.0F;
        }
        samples_[sampleIndex] = sample;
    }

    format_.sampleRate = sampleRate;
    format_.channelCount = channelCount;
    format_.sampleType = SampleType::Float32;
    format_.channelLayout = ChannelLayout::Interleaved;
    readFrame_ = 0;
    open_ = true;
    return Result::Success();
}

Result WavFileReader::Read(AudioBuffer& destination)
{
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "WAV file is not open");
    }
    if (destination.Format().sampleRate != format_.sampleRate ||
        destination.Format().channelCount != format_.channelCount) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio buffer format does not match WAV format");
    }

    const std::size_t framesToCopy = std::min(destination.FrameCount(), FramesRemaining());
    const std::size_t sampleCount = framesToCopy * format_.channelCount;
    std::copy_n(samples_.data() + readFrame_ * format_.channelCount, sampleCount, destination.Data());
    if (framesToCopy < destination.FrameCount()) {
        std::fill(destination.Data() + sampleCount, destination.Data() + destination.SampleCount(), 0.0F);
    }
    readFrame_ += framesToCopy;
    return Result::Success();
}

Result WavFileReader::Rewind()
{
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "WAV file is not open");
    }
    readFrame_ = 0;
    return Result::Success();
}

bool WavFileReader::IsOpen() const noexcept
{
    return open_;
}

bool WavFileReader::EndOfFile() const noexcept
{
    return open_ && readFrame_ >= FrameCount();
}

const AudioFormat& WavFileReader::Format() const noexcept
{
    return format_;
}

std::size_t WavFileReader::FrameCount() const noexcept
{
    if (format_.channelCount == 0) {
        return 0;
    }
    return samples_.size() / format_.channelCount;
}

std::size_t WavFileReader::FramesRemaining() const noexcept
{
    return readFrame_ < FrameCount() ? FrameCount() - readFrame_ : 0;
}

}
