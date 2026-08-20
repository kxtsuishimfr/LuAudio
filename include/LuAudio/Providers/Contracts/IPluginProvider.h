#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Providers/Contracts/IPluginLibrary.h>

#include <filesystem>

namespace LuAudio::Providers {

/**
 * @summary Opens plugin libraries for a platform without exposing its loader API.
 */
class IPluginProvider {
public:
    /** @summary Releases the provider. */
    virtual ~IPluginProvider() = default;

    /**
     * @summary Opens a plugin library.
     * @param path Library path.
     * @param library Receives the opened library on success.
     * @returns Operation result.
     */
    virtual Audio::Result Open(
        const std::filesystem::path& path,
        std::unique_ptr<IPluginLibrary>& library) = 0;
};

}