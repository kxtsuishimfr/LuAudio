#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Plugins/PluginHandle.h>
#include <LuAudio/Providers/Contracts/IPluginProvider.h>

namespace LuAudio::Plugins {

/** @summary Loads and validates plugins using an injected platform provider. */
class PluginManager final {
public:
    explicit PluginManager(Providers::IPluginProvider& provider) noexcept;

    /** @summary Loads a plugin library and validates its descriptor. */
    Audio::Result Load(
        const std::filesystem::path& path,
        std::unique_ptr<PluginHandle>& plugin);

private:
    Providers::IPluginProvider& provider_;
};

}