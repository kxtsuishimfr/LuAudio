#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Plugins/PluginAbi.h>
#include <LuAudio/Providers/Contracts/IPluginLibrary.h>

namespace LuAudio::Plugins {

/** @summary Owns a loaded plugin library and its plugin instance. */
class PluginHandle final {
public:
    PluginHandle(
        std::unique_ptr<Providers::IPluginLibrary> library,
        const PluginDescriptor* descriptor);
    ~PluginHandle();

    PluginHandle(const PluginHandle&) = delete;
    PluginHandle& operator=(const PluginHandle&) = delete;

    /** @summary Creates the plugin instance. */
    Audio::Result Create(const PluginInstanceConfig& config);
    /** @summary Processes one buffer. */
    bool Process(PluginAudioBuffer& buffer) noexcept;
    /** @summary Resets the plugin instance. */
    void Reset() noexcept;
    /** @summary Sets a plugin parameter. */
    bool SetParameter(std::string_view name, float value) noexcept;
    /** @summary Gets a plugin parameter. */
    bool GetParameter(std::string_view name, float& value) noexcept;
    /** @summary Gets the plugin name. */
    const char* Name() const noexcept;

private:
    std::unique_ptr<Providers::IPluginLibrary> library_;
    const PluginDescriptor* descriptor_;
    void* instance_ = nullptr;
};

}