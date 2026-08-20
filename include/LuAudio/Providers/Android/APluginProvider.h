#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Providers/Contracts/IPluginProvider.h>

#include <filesystem>

namespace LuAudio::Providers::Android {

/** @summary Loads Android plugin libraries through the provider boundary. */
class APluginProvider final : public Providers::IPluginProvider {
public:
    /** @summary Opens an Android plugin library. */
    Audio::Result Open(
        const std::filesystem::path& path,
        std::unique_ptr<Providers::IPluginLibrary>& library) override;
};

}