#include <LuAudio/Plugins/SDK/PluginExport.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace {

class BitcrusherPlugin final : public LuAudio::Plugins::SDK::PluginBase {
public:
    bool Process(LuAudio::Plugins::PluginAudioBuffer& buffer) override
    {
        if (buffer.data == nullptr || buffer.channel_count == 0) {
            return false;
        }

        const auto levels = std::ldexp(1.0F, bits_ - 1);
        for (std::uint32_t frame = 0; frame < buffer.frame_count; ++frame) {
            const auto frame_index = frame * buffer.channel_count;
            const bool capture = hold_phase_ == 0;
            if (capture && buffer.channel_count <= 2) {
                for (std::uint32_t channel = 0; channel < buffer.channel_count; ++channel) {
                    held_samples_[channel] = buffer.data[frame_index + channel];
                }
            }

            for (std::uint32_t channel = 0; channel < buffer.channel_count; ++channel) {
                const auto input = buffer.channel_count <= 2 && hold_count_ > 1
                    ? held_samples_[channel]
                    : buffer.data[frame_index + channel];
                const auto driven = std::tanh(input * drive_);
                const auto quantized = std::round(driven * levels) / levels;
                buffer.data[frame_index + channel] = std::clamp(quantized, -1.0F, 1.0F);
            }

            hold_phase_ = (hold_phase_ + 1) % hold_count_;
        }
        return true;
    }

    void Reset() override
    {
        bits_ = 6;
        hold_count_ = 4;
        drive_ = 1.35F;
        hold_phase_ = 0;
        held_samples_[0] = 0.0F;
        held_samples_[1] = 0.0F;
    }

    bool SetParameter(const char* name, float value) override
    {
        if (name == nullptr) {
            return false;
        }
        if (std::string_view(name) == "bits") {
            bits_ = std::clamp(static_cast<int>(std::lround(value)), 1, 16);
            return true;
        }
        if (std::string_view(name) == "hold") {
            hold_count_ = std::clamp(static_cast<int>(std::lround(value)), 1, 32);
            hold_phase_ = 0;
            return true;
        }
        if (std::string_view(name) == "drive") {
            drive_ = std::clamp(value, 1.0F, 4.0F);
            return true;
        }
        return false;
    }

    bool GetParameter(const char* name, float& value) override
    {
        if (name == nullptr) {
            return false;
        }
        if (std::string_view(name) == "bits") {
            value = static_cast<float>(bits_);
            return true;
        }
        if (std::string_view(name) == "hold") {
            value = static_cast<float>(hold_count_);
            return true;
        }
        if (std::string_view(name) == "drive") {
            value = drive_;
            return true;
        }
        return false;
    }

private:
    int bits_ = 8;
    int hold_count_ = 4;
    float drive_ = 1.35F;
    int hold_phase_ = 0;
    float held_samples_[2] = {0.0F, 0.0F};
};

}

const LuAudio::Plugins::PluginParameterDescriptor BitcrusherParameters[]{
    {"bits", "Bits", LuAudio::Plugins::PluginParameterType::Float, 6.0F, 1.0F, 16.0F, 1.0F, "bits", "", 0, nullptr},
    {"hold", "Hold", LuAudio::Plugins::PluginParameterType::Float, 4.0F, 1.0F, 32.0F, 1.0F, "samples", "", 0, nullptr},
    {"drive", "Drive", LuAudio::Plugins::PluginParameterType::Float, 1.35F, 1.0F, 4.0F, 0.01F, "", "", 0, nullptr}};

LUAUDIO_DECLARE_PLUGIN(
    BitcrusherPlugin,
    "Bitcrusher",
    "com.example.bitcrusher",
    "1.0.0",
    nullptr,
    nullptr,
    3,
    BitcrusherParameters)