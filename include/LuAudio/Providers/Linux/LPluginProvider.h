#pragma once

#include <LuAudio/Providers/Contracts/IPluginProvider.h>

namespace LuAudio::Providers::Linux {

/** @summary Loads Linux plugin libraries through the POSIX dynamic-loader API. */
class LPluginProvider final : public Providers::IPluginProvider {
public:
    /** @summary Opens a Linux plugin library. */
    Audio::Result Open(
        const std::filesystem::path& path,
        std::unique_ptr<Providers::IPluginLibrary>& library) override;
};

}
