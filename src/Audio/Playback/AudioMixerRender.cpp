#include <algorithm>
#include <cmath>

#include <LuAudio/Audio/Playback/AudioMixer.h>

namespace LuAudio::Audio {

void AudioMixer::Render(AudioBuffer& masterBuffer) noexcept
{
    struct SourceSnapshot {
        std::shared_ptr<Entry> entry;
        std::shared_ptr<const AudioEffectChain> effects;
        float gain = 1.0F;
        bool paused = false;
        bool seekPending = false;
        std::uint64_t pendingSeekFrame = 0;
    };

    std::vector<SourceSnapshot> sources;
    std::shared_ptr<const AudioEffectChain> masterEffects;
    AudioFormat masterFormat;
    {
        std::lock_guard lock(mutex_);
        sources.reserve(sources_.size());
        for (const auto& entry : sources_) {
            sources.push_back({
                entry,
                entry->effects,
                entry->gain,
                entry->paused,
                entry->seekPending,
                entry->pendingSeekFrame});
            entry->seekPending = false;
        }
        masterEffects = masterEffects_;
        masterFormat = masterConfig_.format;
    }

    masterBuffer.Clear();

    const std::size_t frameCount = masterBuffer.FrameCount();
    const std::size_t masterChannelCount = masterFormat.channelCount;
    if (masterChannelCount == 0) {
        return;
    }

    for (const auto& sourceSnapshot : sources) {
        const auto& source = sourceSnapshot.entry;
        if (sourceSnapshot.seekPending) {
            if (!source->reader->Seek(sourceSnapshot.pendingSeekFrame).Succeeded()) {
                continue;
            }
            source->renderedPosition.store(source->reader->Position(), std::memory_order_release);
        }

        if (sourceSnapshot.paused) {
            continue;
        }

        source->sourceScratch.Resize(frameCount);
        source->masterScratch.Resize(frameCount);
        source->masterScratch.Clear();

        if (!source->reader->Read(source->sourceScratch).Succeeded()) {
            continue;
        }
        source->renderedPosition.store(source->reader->Position(), std::memory_order_release);

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

        if (sourceSnapshot.effects && !sourceSnapshot.effects->Process(source->masterScratch)) {
            continue;
        }

        const std::size_t sampleCount = sourceFrameCount * masterChannelCount;
        float* masterSamples = masterBuffer.Data();
        for (std::size_t sample = 0; sample < sampleCount; ++sample) {
            masterSamples[sample] += remappedSamples[sample] * sourceSnapshot.gain;
        }
    }

    if (masterEffects && !masterEffects->Process(masterBuffer)) {
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