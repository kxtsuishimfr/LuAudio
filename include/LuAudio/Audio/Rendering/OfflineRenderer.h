#pragma once

#include <cstddef>
#include <memory>
#include <vector>

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
    struct Source {
        std::unique_ptr<IAudioReader> reader;
        std::shared_ptr<const AudioEffectChain> effects;
        float gain = 1.0F;
    };

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

    /**
     * @summary Mixes multiple readers into a sequential output sink.
     * @param sources Sources with optional per-source effects and gain.
     * @param outputFormat Format used for the mixed output.
     * @param sink Output sink.
     * @param masterEffects Optional effect chain applied after mixing.
     * @param framesPerBlock Maximum frames processed per iteration.
     * @returns Operation result.
     */
    static Result RenderSources(
        std::vector<Source> sources,
        const AudioFormat& outputFormat,
        IAudioSink& sink,
        std::shared_ptr<const AudioEffectChain> masterEffects = nullptr,
        std::size_t framesPerBlock = 4096);
};

}
