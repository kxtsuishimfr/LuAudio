#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>

#include <LuAudio/Audio/Rendering/OfflineRenderer.h>
#include <LuAudio/Utils/Diagnostics/Log.h>

namespace LuAudio::Audio {

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
        const Result readResult = reader.Read(block);
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

}
