#include <LuAudio/LuAudio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace {

struct DelayLine {
    std::vector<float> samples;
    std::size_t index = 0;
    float filtered = 0.0F;

    explicit DelayLine(std::size_t length)
        : samples(length, 0.0F)
    {
    }

    float Comb(float input, float feedback, float damping) noexcept
    {
        const float output = samples[index];
        filtered = output * (1.0F - damping) + filtered * damping;
        samples[index] = input + filtered * feedback;
        index = (index + 1) % samples.size();
        return output;
    }

    float AllPass(float input, float feedback) noexcept
    {
        const float delayed = samples[index];
        const float output = delayed - feedback * input;
        samples[index] = input + feedback * delayed;
        index = (index + 1) % samples.size();
        return output;
    }

    void Reset() noexcept
    {
        std::fill(samples.begin(), samples.end(), 0.0F);
        index = 0;
        filtered = 0.0F;
    }
};

struct Instance {
    std::vector<DelayLine> combs[2];
    std::vector<DelayLine> allPasses[2];
    std::uint32_t channelCount = 0;
    float roomSize = 0.84F;
    float damping = 0.32F;
    float wet = 0.28F;
    float width = 0.85F;

    explicit Instance(const LuAudio::Plugins::PluginInstanceConfig& config)
        : channelCount(config.channel_count)
    {
        const auto samplesForMilliseconds = [sampleRate = config.sample_rate](float milliseconds) {
            return std::max<std::size_t>(1, static_cast<std::size_t>(
                static_cast<float>(sampleRate) * milliseconds / 1000.0F));
        };
        constexpr float combMilliseconds[] = {29.7F, 37.1F, 41.1F, 43.7F};
        constexpr float allPassMilliseconds[] = {5.0F, 1.7F};
        for (std::size_t channel = 0; channel < 2; ++channel) {
            for (const float milliseconds : combMilliseconds) {
                combs[channel].emplace_back(samplesForMilliseconds(milliseconds +
                    (channel == 1 ? 0.37F : 0.0F)));
            }
            for (const float milliseconds : allPassMilliseconds) {
                allPasses[channel].emplace_back(samplesForMilliseconds(milliseconds +
                    (channel == 1 ? 0.23F : 0.0F)));
            }
        }
    }

    void Reset() noexcept
    {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            for (auto& comb : combs[channel]) {
                comb.Reset();
            }
            for (auto& allPass : allPasses[channel]) {
                allPass.Reset();
            }
        }
    }

    float ProcessChannel(float input, std::size_t channel) noexcept
    {
        const float feedback = 0.72F + roomSize * 0.24F;
        float reverberated = 0.0F;
        for (auto& comb : combs[channel]) {
            reverberated += comb.Comb(input, feedback, damping);
        }
        reverberated *= 0.25F;
        for (auto& allPass : allPasses[channel]) {
            reverberated = allPass.AllPass(reverberated, 0.5F);
        }
        return reverberated;
    }
};

void* Create(const LuAudio::Plugins::PluginInstanceConfig* config)
{
    if (config == nullptr || config->sample_rate == 0 || config->channel_count == 0) {
        return nullptr;
    }
    try {
        return new Instance(*config);
    } catch (...) {
        return nullptr;
    }
}

void Destroy(void* instance)
{
    delete static_cast<Instance*>(instance);
}

bool Process(void* instancePointer, LuAudio::Plugins::PluginAudioBuffer* buffer)
{
    auto* instance = static_cast<Instance*>(instancePointer);
    if (instance == nullptr || buffer == nullptr || buffer->data == nullptr ||
        buffer->channel_count != instance->channelCount) {
        return false;
    }

    for (std::uint32_t frame = 0; frame < buffer->frame_count; ++frame) {
        float wetSamples[2] = {};
        for (std::uint32_t channel = 0; channel < buffer->channel_count; ++channel) {
            wetSamples[channel < 2 ? channel : 0] += instance->ProcessChannel(
                buffer->data[frame * buffer->channel_count + channel], channel < 2 ? channel : 0);
        }
        for (std::uint32_t channel = 0; channel < buffer->channel_count; ++channel) {
            const float input = buffer->data[frame * buffer->channel_count + channel];
            const float wetSample = wetSamples[channel < 2 ? channel : 0];
            const float cross = channel == 0 && buffer->channel_count > 1
                ? wetSamples[1] : channel == 1 ? wetSamples[0] : wetSample;
            const float stereoWet = wetSample * (1.0F - instance->width) + cross * instance->width;
            buffer->data[frame * buffer->channel_count + channel] =
                input * (1.0F - instance->wet) + stereoWet * instance->wet;
        }
    }
    return true;
}

bool SetParameter(void* instancePointer, const char* name, float value)
{
    auto* instance = static_cast<Instance*>(instancePointer);
    if (instance == nullptr || name == nullptr) {
        return false;
    }
    if (std::strcmp(name, "room_size") == 0) {
        instance->roomSize = std::clamp(value, 0.0F, 1.0F);
    } else if (std::strcmp(name, "damping") == 0) {
        instance->damping = std::clamp(value, 0.0F, 0.99F);
    } else if (std::strcmp(name, "wet") == 0) {
        instance->wet = std::clamp(value, 0.0F, 1.0F);
    } else if (std::strcmp(name, "width") == 0) {
        instance->width = std::clamp(value, 0.0F, 1.0F);
    } else {
        return false;
    }
    return true;
}

bool GetParameter(void* instancePointer, const char* name, float* value)
{
    auto* instance = static_cast<Instance*>(instancePointer);
    if (instance == nullptr || name == nullptr || value == nullptr) {
        return false;
    }
    if (std::strcmp(name, "room_size") == 0) {
        *value = instance->roomSize;
    } else if (std::strcmp(name, "damping") == 0) {
        *value = instance->damping;
    } else if (std::strcmp(name, "wet") == 0) {
        *value = instance->wet;
    } else if (std::strcmp(name, "width") == 0) {
        *value = instance->width;
    } else {
        return false;
    }
    return true;
}

const LuAudio::Plugins::PluginDescriptor Descriptor{
    LuAudio::Plugins::PluginAbiVersion,
    "ProfessionalHallReverb",
    &Create,
    &Destroy,
    &Process,
    &SetParameter,
    &GetParameter,
    [](void* instance) { static_cast<Instance*>(instance)->Reset(); }};

}

extern "C" __declspec(dllexport)
const LuAudio::Plugins::PluginDescriptor* LuAudio_GetPluginDescriptor()
{
    return &Descriptor;
}
