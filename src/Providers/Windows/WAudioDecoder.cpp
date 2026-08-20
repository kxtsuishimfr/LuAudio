#include <LuAudio/Common.h>

#if defined(_WIN32)

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <mutex>

#include <LuAudio/Providers/Windows/WAudioDecoder.h>

namespace LuAudio::Providers::Windows {

namespace {

template <typename T>
void Release(T*& value)
{
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

Audio::Result Failure(Audio::ResultCode code, const char* message)
{
    return Audio::Result::Failure(code, message);
}

class MediaFoundationRuntime final {
public:
    static Audio::Result Acquire()
    {
        std::lock_guard lock(Mutex());
        if (ReferenceCount() == 0) {
            const HRESULT result = MFStartup(MF_VERSION);
            if (FAILED(result)) {
                return Failure(Audio::ResultCode::BackendUnavailable,
                    "Windows media framework startup failed");
            }
        }
        ++ReferenceCount();
        return Audio::Result::Success();
    }

    static void Release() noexcept
    {
        std::lock_guard lock(Mutex());
        if (ReferenceCount() == 0) {
            return;
        }
        --ReferenceCount();
        if (ReferenceCount() == 0) {
            MFShutdown();
        }
    }

private:
    static std::mutex& Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    static std::size_t& ReferenceCount()
    {
        static std::size_t references = 0;
        return references;
    }
};

}

WAudioDecoder::~WAudioDecoder()
{
    auto* reader = static_cast<IMFSourceReader*>(reader_);
    Release(reader);
    if (open_) {
        MediaFoundationRuntime::Release();
        open_ = false;
    }
}

Audio::Result WAudioDecoder::Open(const Audio::AudioFile& file, Audio::DecoderInfo& destination)
{
    destination = {};
    auto* previousReader = static_cast<IMFSourceReader*>(reader_);
    Release(previousReader);
    reader_ = nullptr;
    if (open_) {
        MediaFoundationRuntime::Release();
    }
    open_ = false;
    position_ = 0;
    frameCount_ = 0;
    pending_.clear();

    if (!file.IsValid()) {
        return Failure(Audio::ResultCode::InvalidArgument, "Audio file path is empty");
    }
    const auto runtimeResult = MediaFoundationRuntime::Acquire();
    if (!runtimeResult.Succeeded()) {
        return runtimeResult;
    }
    open_ = true;

    IMFSourceReader* reader = nullptr;
    IMFMediaType* outputType = nullptr;
    HRESULT result = MFCreateMediaType(&outputType);
    if (SUCCEEDED(result)) {
        result = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    }
    if (SUCCEEDED(result)) {
        result = outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    }
    if (SUCCEEDED(result)) {
        const std::wstring path(file.Path().begin(), file.Path().end());
        result = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    }
    if (SUCCEEDED(result)) {
        result = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, outputType);
    }
    Release(outputType);
    if (FAILED(result)) {
        Release(reader);
        MediaFoundationRuntime::Release();
        open_ = false;
        return Failure(Audio::ResultCode::ProcessingFailed, "Unable to configure Windows audio decoder");
    }

    IMFMediaType* currentType = nullptr;
    UINT32 sampleRate = 0;
    UINT32 channelCount = 0;
    result = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &currentType);
    if (SUCCEEDED(result)) {
        result = currentType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
    }
    if (SUCCEEDED(result)) {
        result = currentType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channelCount);
    }
    Release(currentType);
    if (FAILED(result) || sampleRate == 0 || channelCount == 0) {
        Release(reader);
        MediaFoundationRuntime::Release();
        open_ = false;
        return Failure(Audio::ResultCode::ProcessingFailed, "Windows decoder returned invalid audio metadata");
    }

    PROPVARIANT duration;
    PropVariantInit(&duration);
    if (SUCCEEDED(reader->GetPresentationAttribute(
            MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration)) && duration.vt == VT_UI8) {
        frameCount_ = static_cast<std::uint64_t>(
            (duration.uhVal.QuadPart * sampleRate) / 10000000ULL);
    }
    PropVariantClear(&duration);

    reader_ = reader;
    sampleRate_ = sampleRate;
    channelCount_ = channelCount;
    destination.format.sampleRate = sampleRate_;
    destination.format.channelCount = channelCount_;
    destination.format.sampleType = Audio::SampleType::Float32;
    destination.format.channelLayout = Audio::ChannelLayout::Interleaved;
    destination.frameCount = frameCount_;
    return Audio::Result::Success();
}

Audio::Result WAudioDecoder::Read(
    std::vector<float>& destination, std::size_t maxFrames, std::size_t& framesRead)
{
    framesRead = 0;
    destination.clear();
    auto* reader = static_cast<IMFSourceReader*>(reader_);
    if (!open_ || reader == nullptr) {
        return Failure(Audio::ResultCode::InvalidState, "Windows audio decoder is not open");
    }
    destination.reserve(maxFrames * channelCount_);
    const std::size_t pendingFrames = pending_.size() / channelCount_;
    const std::size_t pendingFramesToCopy = pendingFrames < maxFrames ? pendingFrames : maxFrames;
    const std::size_t pendingSamplesToCopy = pendingFramesToCopy * channelCount_;
    destination.insert(destination.end(), pending_.begin(), pending_.begin() + pendingSamplesToCopy);
    pending_.erase(pending_.begin(), pending_.begin() + pendingSamplesToCopy);
    framesRead = pendingFramesToCopy;
    position_ += pendingFramesToCopy;
    while (framesRead < maxFrames) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        const HRESULT result = reader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(result)) {
            Release(sample);
            return Failure(Audio::ResultCode::ProcessingFailed, "Windows audio decoding failed");
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            Release(sample);
            break;
        }
        if (sample == nullptr) {
            continue;
        }

        IMFMediaBuffer* buffer = nullptr;
        HRESULT bufferResult = sample->ConvertToContiguousBuffer(&buffer);
        BYTE* data = nullptr;
        DWORD length = 0;
        if (SUCCEEDED(bufferResult)) {
            bufferResult = buffer->Lock(&data, nullptr, &length);
        }
        if (SUCCEEDED(bufferResult)) {
            const std::size_t availableSamples = length / sizeof(float);
            const std::size_t availableFrames = availableSamples / channelCount_;
            const std::size_t availableFramesRequested = maxFrames - framesRead;
            const std::size_t framesToCopy = availableFrames < availableFramesRequested
                ? availableFrames : availableFramesRequested;
            destination.insert(destination.end(), reinterpret_cast<const float*>(data),
                reinterpret_cast<const float*>(data) + framesToCopy * channelCount_);
            if (framesToCopy < availableFrames) {
                pending_.insert(pending_.end(),
                    reinterpret_cast<const float*>(data) + framesToCopy * channelCount_,
                    reinterpret_cast<const float*>(data) + availableFrames * channelCount_);
            }
            framesRead += framesToCopy;
            position_ += framesToCopy;
        }
        if (data != nullptr) {
            buffer->Unlock();
        }
        Release(buffer);
        Release(sample);
        if (FAILED(bufferResult)) {
            return Failure(Audio::ResultCode::ProcessingFailed, "Windows audio sample conversion failed");
        }
    }
    return Audio::Result::Success();
}

Audio::Result WAudioDecoder::Seek(std::uint64_t frame)
{
    auto* reader = static_cast<IMFSourceReader*>(reader_);
    if (!open_ || reader == nullptr) {
        return Failure(Audio::ResultCode::InvalidState, "Windows audio decoder is not open");
    }
    pending_.clear();
    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = static_cast<LONGLONG>((frame * 10000000ULL) / sampleRate_);
    const HRESULT result = reader->SetCurrentPosition(GUID_NULL, position);
    PropVariantClear(&position);
    if (FAILED(result)) {
        return Failure(Audio::ResultCode::InvalidArgument, "Windows audio seek failed");
    }
    position_ = frame;
    return Audio::Result::Success();
}

bool WAudioDecoder::EndOfFile() const noexcept
{
    return open_ && frameCount_ != 0 && position_ >= frameCount_;
}

}

#else

#include <LuAudio/Providers/Windows/WAudioDecoder.h>

namespace LuAudio::Providers::Windows {

WAudioDecoder::~WAudioDecoder() = default;
Audio::Result WAudioDecoder::Open(const Audio::AudioFile&, Audio::DecoderInfo&)
{
    return Audio::Result::Failure(Audio::ResultCode::BackendUnavailable, "Windows decoder is unavailable on this platform");
}
Audio::Result WAudioDecoder::Read(std::vector<float>&, std::size_t, std::size_t& framesRead)
{
    framesRead = 0;
    return Audio::Result::Failure(Audio::ResultCode::BackendUnavailable, "Windows decoder is unavailable on this platform");
}
Audio::Result WAudioDecoder::Seek(std::uint64_t)
{
    return Audio::Result::Failure(Audio::ResultCode::BackendUnavailable, "Windows decoder is unavailable on this platform");
}
bool WAudioDecoder::EndOfFile() const noexcept { return true; }

}

#endif
