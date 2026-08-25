#include <LuAudio/Audio/Sources/Mp3FileReader.h>

namespace LuAudio::Audio {

Mp3FileReader::Mp3FileReader(std::unique_ptr<IAudioDecoder> decoder)
    : reader_(std::move(decoder))
{
}

Mp3FileReader::~Mp3FileReader()
{
}

Result Mp3FileReader::Open(const AudioFile& file)
{
    return reader_.Open(file, AudioFileType::Mp3);
}

Result Mp3FileReader::Read(AudioBuffer& destination)
{
    return reader_.Read(destination);
}

Result Mp3FileReader::Rewind()
{
    return reader_.Rewind();
}

Result Mp3FileReader::Seek(std::uint64_t frame)
{
    return reader_.Seek(frame);
}

bool Mp3FileReader::IsOpen() const noexcept
{
    return reader_.IsOpen();
}

bool Mp3FileReader::EndOfFile() const noexcept
{
    return reader_.EndOfFile();
}

std::uint64_t Mp3FileReader::Position() const noexcept
{
    return reader_.Position();
}

const AudioFormat& Mp3FileReader::Format() const noexcept
{
    return reader_.Format();
}

std::uint64_t Mp3FileReader::FrameCount() const noexcept
{
    return reader_.FrameCount();
}

std::uint64_t Mp3FileReader::FramesRemaining() const noexcept
{
    return reader_.FramesRemaining();
}

bool Mp3FileReader::CanSeek() const noexcept
{
    return reader_.CanSeek();
}

}
