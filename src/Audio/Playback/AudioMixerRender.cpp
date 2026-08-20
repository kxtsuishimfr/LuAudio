#include <algorithm>
#include <cmath>

#include <LuAudio/Audio/Playback/AudioMixer.h>

namespace LuAudio::Audio {

void AudioMixer::Render(AudioBuffer& masterBuffer) noexcept
{
    std::lock_guard lock(mutex_);
    masterBuffer.Clear();

    const std::size_t frameCount = masterBuffer.FrameCount();
    const std::size_t masterChannelCount = masterConfig_.format.channelCount;
    if (masterChannelCount == 0) {
        return;
    }

    for (const auto& source : sources_) {
        if (source->paused) {
            continue;
        }

        source->sourceScratch.Resize(frameCount);
        source->masterScratch.Resize(frameCount);
        source->masterScratch.Clear();

        if (source->seekPending) {
            if (!source->reader->Seek(source->pendingSeekFrame).Succeeded()) {
                source->seekPending = false;
                continue;
            }
            source->seekPending = false;
        }

        if (!source->reader->Read(source->sourceScratch).Succeeded()) {
            continue;
        }

        const std::size_t sourceChannelCount = source->sourceScratch.Format().channelCount;
        const std::size_t sourceFrameCount = std::min(frameCount, source->sourceScratch.FrameCount());
        float* remappedSamples = source->masterScratch.Data();
        const float* sourceSamples = source->sourceScratch.Data();

        for (std::size_t frame = 0; frame < sourceFrameCount; ++frame) {
            if (sourceChannelCount == 1) {
                const float sample = sourceSamples[frame];
                for (std::size_t channel = 0; channel < masterChannelCount; ++channel) {
                    remappedSamples[frame * masterChannelCount + channel] = sample;
                }
            } else {
                for (std::size_t channel = 0; channel < masterChannelCount; ++channel) {
                    remappedSamples[frame * masterChannelCount + channel] =
                        sourceSamples[frame * sourceChannelCount + channel];
                }
            }
        }

        if (source->effects && !source->effects->Process(source->masterScratch)) {
            continue;
        }

        const std::size_t sampleCount = sourceFrameCount * masterChannelCount;
        float* masterSamples = masterBuffer.Data();
        for (std::size_t sample = 0; sample < sampleCount; ++sample) {
            masterSamples[sample] += remappedSamples[sample] * source->gain;
        }
    }

    if (masterEffects_ && !masterEffects_->Process(masterBuffer)) {
        masterBuffer.Clear();
        return;
    }

    float peak = 0.0F;
    for (std::size_t sample = 0; sample < masterBuffer.SampleCount(); ++sample) {
        peak = std::max(peak, std::abs(masterBuffer.Data()[sample]));
    }
    if (peak > 1.0F) {
        const float scale = 1.0F / peak;
        for (std::size_t sample = 0; sample < masterBuffer.SampleCount(); ++sample) {
            masterBuffer.Data()[sample] *= scale;
        }
    }
}

}