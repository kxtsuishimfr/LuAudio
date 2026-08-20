#pragma once

#include <cstddef>

#include <LuAudio/Audio/Contracts/IAudioSink.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioEffectChain.h>
#include <LuAudio/Audio/Sources/IAudioReader.h>

namespace LuAudio::Audio {

/**
 * @summary Renders decoded audio into a sequential output sink.
 */
class OfflineRenderer {
public:
    /**
     * @summary Renders one reader from its current position through EOF.
     * @param reader Decoded audio source.
     * @param sink Output sink.
    * @param effects Optional effect chain applied to each rendered block.
     * @param framesPerBlock Maximum frames processed per iteration.
     * @returns Operation result.
     */
    static Result Render(
        IAudioReader& reader,
        IAudioSink& sink,
        AudioEffectChain* effects = nullptr,
        std::size_t framesPerBlock = 4096);
};

}
