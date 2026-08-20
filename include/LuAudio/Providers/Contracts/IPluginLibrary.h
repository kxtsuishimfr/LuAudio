#pragma once

#include <LuAudio/Common.h>

namespace LuAudio::Providers {

/**
 * @summary Represents a loaded plugin library without exposing platform handles.
 */
class IPluginLibrary {
public:
    /** @summary Releases the library. */
    virtual ~IPluginLibrary() = default;

    /**
     * @summary Resolves an exported symbol.
     * @param name Exported symbol name.
     * @returns The symbol address, or nullptr when it is unavailable.
     */
    virtual void* Resolve(std::string_view name) noexcept = 0;
};

}