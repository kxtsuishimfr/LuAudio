#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Providers/Contracts/IPluginProvider.h>

#include <filesystem>

namespace LuAudio::Providers::Windows {

/** @summary Loads Windows plugin libraries through the provider boundary. */
class WPluginProvider final : public Providers::IPluginProvider {
public:
    /** @summary Opens a Windows plugin library. */
    Audio::Result Open(
        const std::filesystem::path& path,
        std::unique_ptr<Providers::IPluginLibrary>& library) override;
};

}