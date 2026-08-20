#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioEffect.h>
#include <LuAudio/Plugins/PluginHandle.h>

namespace LuAudio::Plugins {

/** @summary Adapts one loaded plugin instance to the core effect contract. */
class PluginEffectAdapter final : public Audio::IAudioEffect {
public:
    PluginEffectAdapter(
        std::unique_ptr<PluginHandle> plugin,
        const PluginInstanceConfig& config);

    /** @summary Processes a core audio buffer through the plugin. */
    bool Process(Audio::AudioBuffer& buffer) noexcept override;
    /** @summary Resets the plugin instance. */
    void Reset() noexcept override;
    /** @summary Sets a plugin parameter. */
    bool SetParameter(std::string_view name, float value) noexcept;
    /** @summary Gets a plugin parameter. */
    bool GetParameter(std::string_view name, float& value) noexcept;
    /** @summary Checks whether plugin instance creation succeeded. */
    bool IsReady() const noexcept;

private:
    std::unique_ptr<PluginHandle> plugin_;
    bool ready_ = false;
};

}