#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

#include <LuAudio/Audio/Rendering/OfflineRenderer.h>
#include <LuAudio/Utils/Diagnostics/Log.h>

namespace LuAudio::Audio {

namespace {

void LimitPeak(AudioBuffer& buffer) noexcept
{
    float peak = 0.0F;
    for (std::size_t index = 0; index < buffer.SampleCount(); ++index) {
        peak = std::max(peak, std::abs(buffer.Data()[index]));
    }
    if (peak <= 1.0F) {
        return;
    }

    const float scale = 1.0F / peak;
    for (std::size_t index = 0; index < buffer.SampleCount(); ++index) {
        buffer.Data()[index] *= scale;
    }
}

bool HasOnlyFiniteSamples(const AudioBuffer& buffer) noexcept
{
    for (std::size_t index = 0; index < buffer.SampleCount(); ++index) {
        if (!std::isfinite(buffer.Data()[index])) {
            return false;
        }
    }
    return true;
}

const OfflineRenderer::Source::ControlBinding* FindBinding(
    const OfflineRenderer::Source& source,
    Automation::TargetHandle target) noexcept
{
    const auto iterator = std::find_if(
        source.controlBindings.begin(),
        source.controlBindings.end(),
        [target](const auto& binding) { return binding.target == target; });
    return iterator == source.controlBindings.end() ? nullptr : &*iterator;
}

}

Result OfflineRenderer::Render(
    IAudioReader& reader,
    IAudioSink& sink,
    AudioEffectChain* effects,
    std::size_t framesPerBlock)
{
    if (!reader.IsOpen()) {
        return Result::Failure(ResultCode::InvalidState, "Audio reader is not open");
    }
    if (!reader.Format().IsValid()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio reader format is invalid");
    }
    if (framesPerBlock == 0) {
        return Result::Failure(ResultCode::InvalidArgument, "Offline render block size must be greater than zero");
    }

    const Result openResult = sink.Open(reader.Format());
    if (!openResult.Succeeded()) {
        return openResult;
    }

    AudioBuffer block(reader.Format(), framesPerBlock);
    while (reader.FramesRemaining() > 0) {
        const std::uint64_t framesRemaining = reader.FramesRemaining();
        const std::size_t frameCount = static_cast<std::size_t>(std::min<std::uint64_t>(
            framesRemaining,
            static_cast<std::uint64_t>(framesPerBlock)));
        block.Resize(frameCount);

        const std::uint64_t positionBeforeRead = reader.Position();
        const Result readResult = reader.ReadFully(block);
        if (!readResult.Succeeded()) {
            sink.Abort();
            return readResult;
        }
        const std::uint64_t framesRead = reader.Position() - positionBeforeRead;
        if (framesRead == 0) {
            if (reader.EndOfFile()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (framesRead > block.FrameCount()) {
            sink.Abort();
            return Result::Failure(ResultCode::ProcessingFailed, "Audio reader advanced beyond its block");
        }
        block.Resize(static_cast<std::size_t>(framesRead));
        if (effects != nullptr && !effects->Process(block)) {
            sink.Abort();
            return Result::Failure(ResultCode::ProcessingFailed, "Offline effect processing failed");
        }

        const Result writeResult = sink.Write(block);
        if (!writeResult.Succeeded()) {
            sink.Abort();
            return writeResult;
        }
    }

    const Result finalizeResult = sink.Finalize();
    if (!finalizeResult.Succeeded()) {
        sink.Abort();
    } else {
        Utils::Log::Info("Offline render finished");
    }
    return finalizeResult;
}

Result OfflineRenderer::RenderSources(
    std::vector<Source> sources,
    const AudioFormat& outputFormat,
    IAudioSink& sink,
    std::shared_ptr<const AudioEffectChain> masterEffects,
    std::size_t framesPerBlock)
{
    if (!outputFormat.IsValid()) {
        return Result::Failure(ResultCode::InvalidArgument, "Offline output format is invalid");
    }
    if (framesPerBlock == 0) {
        return Result::Failure(ResultCode::InvalidArgument, "Offline render block size must be greater than zero");
    }
    if (sources.empty()) {
        return Result::Failure(ResultCode::InvalidArgument, "Offline render requires at least one source");
    }

    std::uint64_t totalFrames = 0;
    for (const auto& source : sources) {
        if (!source.reader || !source.reader->IsOpen()) {
            return Result::Failure(ResultCode::InvalidArgument, "Offline source reader is not open");
        }
        if (!source.reader->Format().IsValid() ||
            !source.reader->Format().CanMixInto(outputFormat)) {
            return Result::Failure(ResultCode::InvalidArgument, "Offline source format is not supported");
        }
        if (!std::isfinite(source.gain)) {
            return Result::Failure(ResultCode::InvalidArgument, "Offline source gain must be finite");
        }
        totalFrames = std::max(totalFrames, source.reader->FramesRemaining());
    }

    const Result openResult = sink.Open(outputFormat);
    if (!openResult.Succeeded()) {
        return openResult;
    }

    AudioBuffer master(outputFormat, framesPerBlock);
    std::vector<AudioBuffer> sourceBuffers;
    sourceBuffers.reserve(sources.size());
    for (const auto& source : sources) {
        sourceBuffers.emplace_back(source.reader->Format(), framesPerBlock);
    }

    std::uint64_t renderedFrames = 0;
    while (renderedFrames < totalFrames) {
        const auto frameCount = static_cast<std::size_t>(std::min<std::uint64_t>(
            totalFrames - renderedFrames,
            static_cast<std::uint64_t>(framesPerBlock)));
        master.Resize(frameCount);
        master.Clear();

        for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
            auto& source = sources[sourceIndex];
            if (source.reader->FramesRemaining() == 0) {
                continue;
            }

            auto& sourceBuffer = sourceBuffers[sourceIndex];
            sourceBuffer.Resize(std::min<std::size_t>(
                frameCount,
                static_cast<std::size_t>(source.reader->FramesRemaining())));
            sourceBuffer.Clear();
            const Result readResult = source.reader->ReadFully(sourceBuffer);
            if (!readResult.Succeeded()) {
                sink.Abort();
                return readResult;
            }
            const auto framesRead = sourceBuffer.FrameCount();
            if (framesRead == 0) {
                if (!source.reader->EndOfFile()) {
                    sink.Abort();
                    return Result::Failure(ResultCode::ProcessingFailed,
                        "Offline source made no progress while remaining frames were reported");
                }
                continue;
            }
            AudioBuffer remapped(outputFormat, sourceBuffer.FrameCount());
            const auto sourceChannelCount = sourceBuffer.Format().channelCount;
            for (std::size_t frame = 0; frame < sourceBuffer.FrameCount(); ++frame) {
                if (sourceChannelCount == 1) {
                    for (std::size_t channel = 0; channel < outputFormat.channelCount; ++channel) {
                        remapped.Data()[frame * outputFormat.channelCount + channel] =
                            sourceBuffer.Data()[frame];
                    }
                } else {
                    for (std::size_t channel = 0; channel < outputFormat.channelCount; ++channel) {
                        remapped.Data()[frame * outputFormat.channelCount + channel] =
                            sourceBuffer.Data()[frame * sourceChannelCount + channel];
                    }
                }
            }

            if (source.effects && !source.effects->Process(remapped)) {
                sink.Abort();
                return Result::Failure(ResultCode::ProcessingFailed,
                    "Offline source effect processing failed");
            }
            if (!HasOnlyFiniteSamples(remapped)) {
                sink.Abort();
                return Result::Failure(ResultCode::ProcessingFailed,
                    "Offline source effect produced a non-finite sample");
            }

            std::vector<Automation::ControlEvent> controls;
            if (source.controls) {
                controls.resize(std::max<std::size_t>(
                    source.controls->Size(),
                    1) * (frameCount + 1));
                controls.resize(source.controls->EvaluateBlock(
                    renderedFrames,
                    static_cast<std::uint32_t>(framesRead),
                    controls.data(),
                    controls.size()));
            }

            std::size_t controlIndex = 0;
            for (std::size_t frame = 0; frame < framesRead; ++frame) {
                while (controlIndex < controls.size() &&
                    controls[controlIndex].frameOffset <= frame) {
                    const auto* binding = FindBinding(source, controls[controlIndex].target);
                    if (binding != nullptr) {
                        binding->control->Apply(
                            controls[controlIndex].frameOffset,
                            controls[controlIndex].value);
                    }
                    ++controlIndex;
                }

                for (std::size_t channel = 0; channel < outputFormat.channelCount; ++channel) {
                    const std::size_t sample = frame * outputFormat.channelCount + channel;
                    master.Data()[sample] += remapped.Data()[sample] * source.gain;
                }
            }
        }

        if (masterEffects && !masterEffects->Process(master)) {
            sink.Abort();
            return Result::Failure(ResultCode::ProcessingFailed,
                "Offline master effect processing failed");
        }
        if (!HasOnlyFiniteSamples(master)) {
            sink.Abort();
            return Result::Failure(ResultCode::ProcessingFailed,
                "Offline master processing produced a non-finite sample");
        }
        LimitPeak(master);

        const Result writeResult = sink.Write(master);
        if (!writeResult.Succeeded()) {
            sink.Abort();
            return writeResult;
        }
        renderedFrames += frameCount;
    }

    const Result finalizeResult = sink.Finalize();
    if (!finalizeResult.Succeeded()) {
        sink.Abort();
    }
    return finalizeResult;
}

}
