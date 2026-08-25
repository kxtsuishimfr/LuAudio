#include <LuAudio/Common.h>

#include <LuAudio/Audio/Sources/WavFileReader.h>

#include <array>
#include <limits>

namespace LuAudio::Audio {

namespace {

std::uint16_t ReadUInt16(const std::uint8_t* bytes)
{
	return static_cast<std::uint16_t>(bytes[0]) |
		static_cast<std::uint16_t>(bytes[1] << 8);
}

std::uint32_t ReadUInt32(const std::uint8_t* bytes)
{
	return static_cast<std::uint32_t>(bytes[0]) |
		static_cast<std::uint32_t>(bytes[1] << 8) |
		static_cast<std::uint32_t>(bytes[2] << 16) |
		static_cast<std::uint32_t>(bytes[3] << 24);
}

Result InvalidFile(const char* message)
{
	return Result::Failure(ResultCode::InvalidArgument, message);
}

class WavDecoder final : public IAudioDecoder {
public:
	~WavDecoder() override
	{
		Close();
	}

	Result Open(const AudioFile& file, DecoderInfo& destination) override
	{
		Close();
		if (!file.IsValid() || file.Type() != AudioFileType::Wav) {
			return InvalidFile("Audio file is not WAV");
		}

		stream_.open(file.Path(), std::ios::binary);
		if (!stream_) {
			return InvalidFile("Unable to open WAV file");
		}
		stream_.seekg(0, std::ios::end);
		const std::streamoff fileSize = stream_.tellg();
		if (fileSize < 12) {
			Close();
			return InvalidFile("Invalid WAV file size");
		}

		const std::uint64_t totalBytes = static_cast<std::uint64_t>(fileSize);
		std::array<std::uint8_t, 12> header{};
		if (!ReadBytes(0, header.data(), header.size()) ||
			std::memcmp(header.data(), "RIFF", 4) != 0 ||
			std::memcmp(header.data() + 8, "WAVE", 4) != 0) {
			Close();
			return InvalidFile("WAV file is missing RIFF/WAVE headers");
		}

		const std::uint64_t riffSize = ReadUInt32(header.data() + 4);
		if (riffSize > totalBytes - 8) {
			Close();
			return InvalidFile("WAV RIFF chunk exceeds file size");
		}
		const std::uint64_t riffEnd = riffSize + 8;
		std::uint64_t cursor = 12;
		std::uint16_t audioFormat = 0;
		std::uint16_t channelCount = 0;
		std::uint32_t sampleRate = 0;
		std::uint16_t blockAlignment = 0;
		std::uint16_t bitsPerSample = 0;
		bool foundFormat = false;
		bool foundData = false;

		while (cursor <= riffEnd && riffEnd - cursor >= 8) {
			std::array<std::uint8_t, 8> chunk{};
			if (!ReadBytes(cursor, chunk.data(), chunk.size())) {
				Close();
				return InvalidFile("Unable to read WAV chunk header");
			}
			const std::uint32_t chunkSize = ReadUInt32(chunk.data() + 4);
			const std::uint64_t chunkData = cursor + 8;
			if (chunkSize > riffEnd - chunkData || chunkSize > totalBytes - chunkData) {
				Close();
				return InvalidFile("WAV chunk exceeds file size");
			}

			if (std::memcmp(chunk.data(), "fmt ", 4) == 0) {
				if (chunkSize < 16) {
					Close();
					return InvalidFile("WAV format chunk is too small");
				}
				std::array<std::uint8_t, 16> format{};
				if (!ReadBytes(chunkData, format.data(), format.size())) {
					Close();
					return InvalidFile("Unable to read WAV format chunk");
				}
				audioFormat = ReadUInt16(format.data());
				channelCount = ReadUInt16(format.data() + 2);
				sampleRate = ReadUInt32(format.data() + 4);
				blockAlignment = ReadUInt16(format.data() + 12);
				bitsPerSample = ReadUInt16(format.data() + 14);
				foundFormat = true;
			} else if (std::memcmp(chunk.data(), "data", 4) == 0) {
				dataOffset_ = chunkData;
				dataSize_ = chunkSize;
				foundData = true;
			}

			const std::uint64_t next = chunkData + chunkSize + (chunkSize & 1U);
			if (next < cursor || next > riffEnd) {
				break;
			}
			cursor = next;
		}

		if (!foundFormat || !foundData || channelCount == 0 || sampleRate == 0) {
			Close();
			return InvalidFile("WAV file is missing format or data");
		}
		if (audioFormat != 1 && audioFormat != 3) {
			Close();
			return InvalidFile("WAV format is not PCM or IEEE float");
		}
		if (audioFormat == 3 && bitsPerSample != 32) {
			Close();
			return InvalidFile("WAV IEEE float format must be 32-bit");
		}
		if (audioFormat == 1 && bitsPerSample != 8 && bitsPerSample != 16 &&
			bitsPerSample != 24 && bitsPerSample != 32) {
			Close();
			return InvalidFile("WAV PCM format must be 8, 16, 24, or 32-bit");
		}

		bytesPerSample_ = static_cast<std::uint16_t>((bitsPerSample + 7U) / 8U);
		const std::uint32_t expectedAlignment =
			static_cast<std::uint32_t>(channelCount) * bytesPerSample_;
		if (blockAlignment == 0 || blockAlignment != expectedAlignment ||
			dataSize_ % blockAlignment != 0) {
			Close();
			return InvalidFile("WAV block alignment is invalid");
		}

		format_ = AudioFormat{sampleRate, channelCount, SampleType::Float32,
			ChannelLayout::Interleaved};
		audioFormat_ = audioFormat;
		bitsPerSample_ = bitsPerSample;
		blockAlignment_ = blockAlignment;
		position_ = 0;
		open_ = true;
		destination.format = format_;
		destination.frameCount = FrameCount();
		return Result::Success();
	}

	Result Read(std::vector<float>& destination, std::size_t maxFrames,
		std::size_t& framesRead) override
	{
		framesRead = 0;
		if (!open_) {
			return Result::Failure(ResultCode::InvalidState, "WAV decoder is not open");
		}
		const std::size_t frames = static_cast<std::size_t>(std::min<std::uint64_t>(
			FrameCount() - position_, maxFrames));
		if (frames > std::numeric_limits<std::size_t>::max() / blockAlignment_) {
			return Result::Failure(ResultCode::ProcessingFailed, "WAV read size is too large");
		}

		const std::size_t byteCount = frames * blockAlignment_;
		std::vector<std::uint8_t> bytes(byteCount);
		if (byteCount != 0 && !ReadBytes(dataOffset_ + position_ * blockAlignment_,
			bytes.data(), byteCount)) {
			return Result::Failure(ResultCode::ProcessingFailed, "Unable to read WAV samples");
		}

		const std::size_t sampleCount = frames * format_.channelCount;
		destination.resize(sampleCount);
		for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
			const std::uint8_t* sampleBytes = bytes.data() + sampleIndex * bytesPerSample_;
			float sample = 0.0F;
			if (audioFormat_ == 3) {
				std::memcpy(&sample, sampleBytes, sizeof(float));
			} else if (bitsPerSample_ == 8) {
				sample = (static_cast<float>(sampleBytes[0]) - 128.0F) / 128.0F;
			} else if (bitsPerSample_ == 16) {
				sample = static_cast<float>(static_cast<std::int16_t>(ReadUInt16(sampleBytes))) /
					32768.0F;
			} else if (bitsPerSample_ == 24) {
				std::int32_t value = static_cast<std::int32_t>(sampleBytes[0]) |
					(static_cast<std::int32_t>(sampleBytes[1]) << 8) |
					(static_cast<std::int32_t>(sampleBytes[2]) << 16);
				if ((value & 0x00800000) != 0) {
					value |= 0xFF000000;
				}
				sample = static_cast<float>(value) / 8388608.0F;
			} else {
				sample = static_cast<float>(static_cast<std::int32_t>(ReadUInt32(sampleBytes))) /
					2147483648.0F;
			}
			destination[sampleIndex] = sample;
		}

		position_ += frames;
		framesRead = frames;
		return Result::Success();
	}

	Result Seek(std::uint64_t frame) override
	{
		if (!open_ || frame > FrameCount()) {
			return InvalidFile("WAV seek position is outside the file");
		}
		position_ = frame;
		return Result::Success();
	}

	bool EndOfFile() const noexcept override
	{
		return open_ && position_ >= FrameCount();
	}

private:
	bool ReadBytes(std::uint64_t offset, void* destination, std::size_t size)
	{
		stream_.clear();
		stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
		return static_cast<bool>(stream_.read(static_cast<char*>(destination),
			static_cast<std::streamsize>(size)));
	}

	std::uint64_t FrameCount() const noexcept
	{
		return blockAlignment_ == 0 ? 0 : dataSize_ / blockAlignment_;
	}

	void Close() noexcept
	{
		if (stream_.is_open()) {
			stream_.close();
		}
		format_ = {};
		dataOffset_ = 0;
		dataSize_ = 0;
		position_ = 0;
		blockAlignment_ = 0;
		bytesPerSample_ = 0;
		open_ = false;
	}

	std::ifstream stream_;
	AudioFormat format_;
	std::uint64_t dataOffset_ = 0;
	std::uint64_t dataSize_ = 0;
	std::uint64_t position_ = 0;
	std::uint16_t blockAlignment_ = 0;
	std::uint16_t bytesPerSample_ = 0;
	std::uint16_t audioFormat_ = 0;
	std::uint16_t bitsPerSample_ = 0;
	bool open_ = false;
};

}

WavFileReader::WavFileReader()
	: reader_(std::make_unique<StreamingAudioReader>(std::make_unique<WavDecoder>()))
{
}

WavFileReader::~WavFileReader() = default;

Result WavFileReader::Open(const AudioFile& file)
{
	return reader_->Open(file, AudioFileType::Wav);
}

Result WavFileReader::Open(const std::string& path)
{
	return Open(AudioFile(path, AudioFileType::Wav));
}

Result WavFileReader::Read(AudioBuffer& destination)
{
	return reader_->Read(destination);
}

Result WavFileReader::Rewind()
{
	return reader_->Rewind();
}

Result WavFileReader::Seek(std::uint64_t frame)
{
	return reader_->Seek(frame);
}

bool WavFileReader::IsOpen() const noexcept
{
	return reader_->IsOpen();
}

bool WavFileReader::EndOfFile() const noexcept
{
	return reader_->EndOfFile();
}

std::uint64_t WavFileReader::Position() const noexcept
{
	return reader_->Position();
}

const AudioFormat& WavFileReader::Format() const noexcept
{
	return reader_->Format();
}

std::uint64_t WavFileReader::FrameCount() const noexcept
{
	return reader_->FrameCount();
}

std::uint64_t WavFileReader::FramesRemaining() const noexcept
{
	return reader_->FramesRemaining();
}

bool WavFileReader::CanSeek() const noexcept
{
	return reader_->CanSeek();
}

}
