#include <LuAudio/Common.h>

#if defined(__ANDROID__)

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <LuAudio/Providers/Android/AAudioDecoder.h>

namespace LuAudio::Providers::Android {

namespace {

constexpr int32_t kPcmEncodingFloat = 4;
constexpr const char* kPcmEncodingKey = "pcm-encoding";

struct DecoderState {
    AMediaExtractor* extractor = nullptr;
    AMediaCodec* codec = nullptr;
    int32_t sampleRate = 0;
    int32_t channelCount = 0;
    int32_t pcmEncoding = 2;
    bool inputEnd = false;
    bool outputEnd = false;
    std::uint64_t position = 0;
    std::uint64_t frameCount = 0;
    std::vector<float> pending;
};

Audio::Result Failure(Audio::ResultCode code, const char* message)
{
    return Audio::Result::Failure(code, message);
}

void Destroy(DecoderState* state)
{
    if (state == nullptr) {
        return;
    }
    if (state->codec != nullptr) {
        AMediaCodec_stop(state->codec);
        AMediaCodec_delete(state->codec);
    }
    if (state->extractor != nullptr) {
        AMediaExtractor_delete(state->extractor);
    }
    delete state;
}

}

AAudioDecoder::~AAudioDecoder()
{
    Destroy(static_cast<DecoderState*>(state_));
}

Audio::Result AAudioDecoder::Open(const Audio::AudioFile& file, Audio::DecoderInfo& destination)
{
    destination = {};
    Destroy(static_cast<DecoderState*>(state_));
    state_ = nullptr;
    if (!file.IsValid()) {
        return Failure(Audio::ResultCode::InvalidArgument, "Audio file path is empty");
    }

    const int fileDescriptor = open(file.Path().c_str(), O_RDONLY | O_CLOEXEC);
    struct stat fileInfo{};
    if (fileDescriptor < 0 || fstat(fileDescriptor, &fileInfo) != 0 || fileInfo.st_size <= 0) {
        if (fileDescriptor >= 0) {
            close(fileDescriptor);
        }
        return Failure(Audio::ResultCode::InvalidArgument, "Unable to open audio file on Android");
    }

    auto* state = new DecoderState();
    state->extractor = AMediaExtractor_new();
    const media_status_t extractorStatus = state->extractor == nullptr
        ? AMEDIA_ERROR_UNKNOWN
        : AMediaExtractor_setDataSourceFd(state->extractor, fileDescriptor, 0, fileInfo.st_size);
    close(fileDescriptor);
    if (extractorStatus != AMEDIA_OK) {
        Destroy(state);
        return Failure(Audio::ResultCode::InvalidArgument, "Unable to open audio file on Android");
    }

    AMediaFormat* trackFormat = nullptr;
    const char* mime = nullptr;
    size_t selectedTrack = 0;
    for (; selectedTrack < AMediaExtractor_getTrackCount(state->extractor); ++selectedTrack) {
        trackFormat = AMediaExtractor_getTrackFormat(state->extractor, selectedTrack);
        if (trackFormat != nullptr && AMediaFormat_getString(trackFormat, AMEDIAFORMAT_KEY_MIME, &mime) &&
            mime != nullptr && std::strncmp(mime, "audio/", 6) == 0) {
            break;
        }
        if (trackFormat != nullptr) {
            AMediaFormat_delete(trackFormat);
            trackFormat = nullptr;
        }
    }
    if (trackFormat == nullptr || mime == nullptr ||
        AMediaExtractor_selectTrack(state->extractor, selectedTrack) != AMEDIA_OK) {
        if (trackFormat != nullptr) {
            AMediaFormat_delete(trackFormat);
        }
        Destroy(state);
        return Failure(Audio::ResultCode::InvalidArgument, "Audio track was not found");
    }

    AMediaFormat_getInt32(trackFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE, &state->sampleRate);
    AMediaFormat_getInt32(trackFormat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &state->channelCount);
    int64_t durationUs = 0;
    AMediaFormat_getInt64(trackFormat, AMEDIAFORMAT_KEY_DURATION, &durationUs);
    if (state->sampleRate <= 0 || state->channelCount <= 0) {
        AMediaFormat_delete(trackFormat);
        Destroy(state);
        return Failure(Audio::ResultCode::ProcessingFailed, "Android audio metadata is invalid");
    }
    if (durationUs > 0) {
        state->frameCount = static_cast<std::uint64_t>(durationUs) * state->sampleRate / 1000000ULL;
    }

    state->codec = AMediaCodec_createDecoderByType(mime);
    const media_status_t configureStatus = state->codec == nullptr
        ? AMEDIA_ERROR_UNKNOWN
        : AMediaCodec_configure(state->codec, trackFormat, nullptr, nullptr, 0);
    const media_status_t startStatus = configureStatus == AMEDIA_OK
        ? AMediaCodec_start(state->codec)
        : configureStatus;
    AMediaFormat_delete(trackFormat);
    if (startStatus != AMEDIA_OK) {
        Destroy(state);
        return Failure(Audio::ResultCode::BackendUnavailable, "Unable to start Android audio decoder");
    }

    state_ = state;
    destination.format.sampleRate = static_cast<std::uint32_t>(state->sampleRate);
    destination.format.channelCount = static_cast<std::uint32_t>(state->channelCount);
    destination.format.sampleType = Audio::SampleType::Float32;
    destination.format.channelLayout = Audio::ChannelLayout::Interleaved;
    destination.frameCount = state->frameCount;
    return Audio::Result::Success();
}

Audio::Result AAudioDecoder::Read(
    std::vector<float>& destination, std::size_t maxFrames, std::size_t& framesRead)
{
    destination.clear();
    framesRead = 0;
    auto* state = static_cast<DecoderState*>(state_);
    if (state == nullptr || state->codec == nullptr) {
        return Failure(Audio::ResultCode::InvalidState, "Android audio decoder is not open");
    }
    destination.reserve(maxFrames * static_cast<std::size_t>(state->channelCount));

    const std::size_t pendingFrames = state->pending.size() /
        static_cast<std::size_t>(state->channelCount);
    const std::size_t pendingFramesToCopy = std::min(pendingFrames, maxFrames);
    const std::size_t pendingSamplesToCopy = pendingFramesToCopy * state->channelCount;
    destination.insert(destination.end(), state->pending.begin(),
        state->pending.begin() + pendingSamplesToCopy);
    state->pending.erase(state->pending.begin(), state->pending.begin() + pendingSamplesToCopy);
    framesRead = pendingFramesToCopy;
    state->position += pendingFramesToCopy;

    while (!state->outputEnd && framesRead < maxFrames) {
        if (!state->inputEnd) {
            const ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(state->codec, 10000);
            if (inputIndex >= 0) {
                size_t inputCapacity = 0;
                uint8_t* inputBuffer = AMediaCodec_getInputBuffer(state->codec, inputIndex, &inputCapacity);
                const ssize_t sampleSize = inputBuffer == nullptr
                    ? -1
                    : AMediaExtractor_readSampleData(state->extractor, inputBuffer, inputCapacity);
                if (sampleSize <= 0) {
                    AMediaCodec_queueInputBuffer(state->codec, inputIndex, 0, 0, 0,
                        AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    state->inputEnd = true;
                } else {
                    const int64_t presentationTime = AMediaExtractor_getSampleTime(state->extractor);
                    AMediaCodec_queueInputBuffer(state->codec, inputIndex, 0,
                        static_cast<size_t>(sampleSize), presentationTime, 0);
                    AMediaExtractor_advance(state->extractor);
                }
            }
        }

        AMediaCodecBufferInfo info{};
        const ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(state->codec, &info, 10000);
        if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* outputFormat = AMediaCodec_getOutputFormat(state->codec);
            if (outputFormat != nullptr) {
                AMediaFormat_getInt32(outputFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE, &state->sampleRate);
                AMediaFormat_getInt32(outputFormat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &state->channelCount);
                AMediaFormat_getInt32(outputFormat, kPcmEncodingKey, &state->pcmEncoding);
                AMediaFormat_delete(outputFormat);
            }
            continue;
        }
        if (outputIndex < 0) {
            continue;
        }

        size_t outputSize = 0;
        const uint8_t* outputBuffer = AMediaCodec_getOutputBuffer(state->codec, outputIndex, &outputSize);
        if (outputBuffer != nullptr && info.size > 0) {
            const std::size_t offset = static_cast<std::size_t>(info.offset);
            const std::size_t byteCount = static_cast<std::size_t>(info.size);
            if (offset > outputSize || byteCount > outputSize - offset) {
                AMediaCodec_releaseOutputBuffer(state->codec, outputIndex, false);
                return Failure(Audio::ResultCode::ProcessingFailed, "Android decoder returned an invalid output buffer");
            }
            const uint8_t* samples = outputBuffer + offset;
            const std::size_t sampleCount = state->pcmEncoding == kPcmEncodingFloat
                ? byteCount / sizeof(float) : byteCount / sizeof(int16_t);
            const std::size_t remainingSamples = (maxFrames - framesRead) * state->channelCount;
            const std::size_t samplesToCopy = std::min(sampleCount, remainingSamples);
            if (state->pcmEncoding == kPcmEncodingFloat) {
                const auto* values = reinterpret_cast<const float*>(samples);
                destination.insert(destination.end(), values, values + samplesToCopy);
            } else {
                const auto* values = reinterpret_cast<const int16_t*>(samples);
                for (std::size_t index = 0; index < samplesToCopy; ++index) {
                    destination.push_back(static_cast<float>(values[index]) / 32768.0F);
                }
            }
            if (samplesToCopy < sampleCount) {
                if (state->pcmEncoding == kPcmEncodingFloat) {
                    const auto* values = reinterpret_cast<const float*>(samples);
                    state->pending.insert(state->pending.end(), values + samplesToCopy,
                        values + sampleCount);
                } else {
                    const auto* values = reinterpret_cast<const int16_t*>(samples);
                    for (std::size_t index = samplesToCopy; index < sampleCount; ++index) {
                        state->pending.push_back(static_cast<float>(values[index]) / 32768.0F);
                    }
                }
            }
            framesRead += samplesToCopy / state->channelCount;
            state->position += samplesToCopy / state->channelCount;
        }
        if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
            state->outputEnd = true;
        }
        AMediaCodec_releaseOutputBuffer(state->codec, outputIndex, false);
    }
    return Audio::Result::Success();
}

Audio::Result AAudioDecoder::Seek(std::uint64_t frame)
{
    auto* state = static_cast<DecoderState*>(state_);
    if (state == nullptr || state->codec == nullptr) {
        return Failure(Audio::ResultCode::InvalidState, "Android audio decoder is not open");
    }
    const auto timestamp = static_cast<int64_t>(frame) * 1000000LL / state->sampleRate;
    AMediaExtractor_seekTo(state->extractor, timestamp, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
    AMediaCodec_flush(state->codec);
    state->pending.clear();
    state->inputEnd = false;
    state->outputEnd = false;
    state->position = frame;
    return Audio::Result::Success();
}

bool AAudioDecoder::EndOfFile() const noexcept
{
    const auto* state = static_cast<const DecoderState*>(state_);
    return state != nullptr && state->outputEnd;
}

}

#else

#include <LuAudio/Providers/Android/AAudioDecoder.h>

namespace LuAudio::Providers::Android {

AAudioDecoder::~AAudioDecoder() = default;
Audio::Result AAudioDecoder::Open(const Audio::AudioFile&, Audio::DecoderInfo&)
{
    return Audio::Result::Failure(Audio::ResultCode::BackendUnavailable, "Android decoder is unavailable on this platform");
}
Audio::Result AAudioDecoder::Read(std::vector<float>&, std::size_t, std::size_t& framesRead)
{
    framesRead = 0;
    return Audio::Result::Failure(Audio::ResultCode::BackendUnavailable, "Android decoder is unavailable on this platform");
}
Audio::Result AAudioDecoder::Seek(std::uint64_t)
{
    return Audio::Result::Failure(Audio::ResultCode::BackendUnavailable, "Android decoder is unavailable on this platform");
}
bool AAudioDecoder::EndOfFile() const noexcept { return true; }

}

#endif
