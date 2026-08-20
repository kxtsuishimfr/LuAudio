#include <cstdio>
#include <cstring>
#include <limits>

#include <LuAudio/Audio/Sinks/WavFileWriter.h>

namespace LuAudio::Audio {

namespace {

void WriteUInt16(char* destination, std::uint16_t value)
{
    destination[0] = static_cast<char>(value & 0xFFU);
    destination[1] = static_cast<char>((value >> 8) & 0xFFU);
}

void WriteUInt32(char* destination, std::uint32_t value)
{
    destination[0] = static_cast<char>(value & 0xFFU);
    destination[1] = static_cast<char>((value >> 8) & 0xFFU);
    destination[2] = static_cast<char>((value >> 16) & 0xFFU);
    destination[3] = static_cast<char>((value >> 24) & 0xFFU);
}

Result InvalidFormat(const char* message)
{
    return Result::Failure(ResultCode::InvalidArgument, message);
}

}

WavFileWriter::WavFileWriter(std::string path)
    : path_(std::move(path))
{
}

WavFileWriter::~WavFileWriter()
{
    Abort();
}

Result WavFileWriter::Open(const AudioFormat& format)
{
    Abort();
    if (path_.empty()) {
        return InvalidFormat("WAV output path is empty");
    }
    if (!format.IsValid() || format.sampleType != SampleType::Float32 ||
        format.channelLayout != ChannelLayout::Interleaved) {
        return InvalidFormat("WAV writer requires interleaved Float32 audio");
    }
    if (format.channelCount > std::numeric_limits<std::uint16_t>::max()) {
        return InvalidFormat("WAV channel count is too large");
    }
    const std::uint64_t blockAlignment = static_cast<std::uint64_t>(format.channelCount) * sizeof(float);
    const std::uint64_t byteRate = static_cast<std::uint64_t>(format.sampleRate) * blockAlignment;
    if (blockAlignment > std::numeric_limits<std::uint16_t>::max() ||
        byteRate > std::numeric_limits<std::uint32_t>::max()) {
        return InvalidFormat("WAV format fields are too large");
    }

    file_.open(path_, std::ios::binary | std::ios::trunc | std::ios::out);
    if (!file_) {
        return Result::Failure(ResultCode::ProcessingFailed, "Unable to open WAV output file");
    }

    format_ = format;
    dataBytes_ = 0;
    finalized_ = false;
    open_ = true;
    const Result headerResult = WriteHeader();
    if (!headerResult.Succeeded()) {
        Abort();
    }
    return headerResult;
}

Result WavFileWriter::Write(const AudioBuffer& buffer)
{
    if (!open_ || finalized_) {
        return Result::Failure(ResultCode::InvalidState, "WAV output is not open");
    }
    if (buffer.Format().sampleRate != format_.sampleRate ||
        buffer.Format().channelCount != format_.channelCount ||
        buffer.Format().sampleType != format_.sampleType ||
        buffer.Format().channelLayout != format_.channelLayout) {
        return InvalidFormat("Audio buffer format does not match WAV output");
    }

    const std::uint64_t byteCount = static_cast<std::uint64_t>(buffer.SampleCount()) * sizeof(float);
    if (byteCount > std::numeric_limits<std::uint32_t>::max() - dataBytes_) {
        return Result::Failure(ResultCode::ProcessingFailed, "WAV output is too large");
    }

    for (std::size_t index = 0; index < buffer.SampleCount(); ++index) {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(float));
        std::memcpy(&bits, buffer.Data() + index, sizeof(bits));
        char bytes[4];
        WriteUInt32(bytes, bits);
        file_.write(bytes, sizeof(bytes));
    }
    if (!file_) {
        return Result::Failure(ResultCode::ProcessingFailed, "Unable to write WAV samples");
    }

    dataBytes_ += byteCount;
    return Result::Success();
}

Result WavFileWriter::Finalize()
{
    if (!open_ || finalized_) {
        return Result::Failure(ResultCode::InvalidState, "WAV output is not open");
    }

    const Result patchResult = PatchHeader();
    if (!patchResult.Succeeded()) {
        return patchResult;
    }

    file_.close();
    open_ = false;
    finalized_ = true;
    return Result::Success();
}

void WavFileWriter::Abort() noexcept
{
    if (file_.is_open()) {
        file_.close();
    }
    if (open_ && !path_.empty()) {
        std::remove(path_.c_str());
    }
    open_ = false;
    finalized_ = false;
    dataBytes_ = 0;
}

const AudioFormat& WavFileWriter::Format() const noexcept
{
    return format_;
}

bool WavFileWriter::IsOpen() const noexcept
{
    return open_ && !finalized_;
}

Result WavFileWriter::WriteHeader()
{
    char header[44] = {};
    std::memcpy(header, "RIFF", 4);
    std::memcpy(header + 8, "WAVE", 4);
    std::memcpy(header + 12, "fmt ", 4);
    WriteUInt32(header + 16, 16);
    WriteUInt16(header + 20, 3);
    WriteUInt16(header + 22, static_cast<std::uint16_t>(format_.channelCount));
    WriteUInt32(header + 24, format_.sampleRate);
    const auto blockAlignment = static_cast<std::uint16_t>(format_.channelCount * sizeof(float));
    const auto byteRate = static_cast<std::uint32_t>(format_.sampleRate * blockAlignment);
    WriteUInt32(header + 28, byteRate);
    WriteUInt16(header + 32, blockAlignment);
    WriteUInt16(header + 34, 32);
    std::memcpy(header + 36, "data", 4);

    file_.write(header, sizeof(header));
    if (!file_) {
        return Result::Failure(ResultCode::ProcessingFailed, "Unable to write WAV header");
    }
    return Result::Success();
}

Result WavFileWriter::PatchHeader()
{
    if (dataBytes_ > std::numeric_limits<std::uint32_t>::max() - 36U) {
        return Result::Failure(ResultCode::ProcessingFailed, "WAV output is too large");
    }

    char value[4];
    file_.seekp(4, std::ios::beg);
    WriteUInt32(value, static_cast<std::uint32_t>(36 + dataBytes_));
    file_.write(value, sizeof(value));
    file_.seekp(40, std::ios::beg);
    WriteUInt32(value, static_cast<std::uint32_t>(dataBytes_));
    file_.write(value, sizeof(value));
    file_.flush();
    if (!file_) {
        return Result::Failure(ResultCode::ProcessingFailed, "Unable to finalize WAV header");
    }
    return Result::Success();
}

}
