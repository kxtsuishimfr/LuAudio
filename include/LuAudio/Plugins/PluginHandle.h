#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Plugins/Contracts/PluginAbi.h>
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
    /** @summary Gets the stable plugin identifier. */
    const char* Id() const noexcept;
    /** @summary Gets the plugin version. */
    const char* Version() const noexcept;
    /** @summary Gets the optional plugin description. */
    const char* Description() const noexcept;
    /** @summary Gets the optional plugin vendor. */
    const char* Vendor() const noexcept;
    /** @summary Gets the number of declared parameters. */
    std::uint32_t ParameterCount() const noexcept;
    /** @summary Gets the declared parameter metadata. */
    const PluginParameterDescriptor* Parameters() const noexcept;

private:
    std::unique_ptr<Providers::IPluginLibrary> library_;
    const PluginDescriptor* descriptor_;
    void* instance_ = nullptr;
};

}