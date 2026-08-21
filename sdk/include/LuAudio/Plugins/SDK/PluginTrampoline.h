#pragma once

#include <LuAudio/Plugins/SDK/PluginBase.h>

#include <new>
#include <type_traits>

namespace LuAudio::Plugins::SDK {

template <typename Plugin>
class PluginTrampoline final {
    static_assert(std::is_base_of_v<PluginBase, Plugin>,
        "LUAUDIO_DECLARE_PLUGIN requires a class derived from PluginBase");

public:
    static const PluginDescriptor* Descriptor(
        const char* display_name,
        const char* id,
        const char* version,
        const char* description,
        const char* vendor,
        std::uint32_t parameter_count,
        const PluginParameterDescriptor* parameters) noexcept
    {
        static const PluginDescriptor descriptor{
            PluginAbiVersion,
            display_name,
            id,
            version,
            description,
            vendor,
            parameter_count,
            parameters,
            &Create,
            &Destroy,
            &Process,
            &SetParameter,
            &GetParameter,
            &Reset};
        return &descriptor;
    }

private:
    static void* Create(const PluginInstanceConfig* config) noexcept
    {
        if (config == nullptr) {
            return nullptr;
        }

        auto* instance = new (std::nothrow) Plugin();
        if (instance == nullptr) {
            return nullptr;
        }

        try {
            if (!instance->Init(*config)) {
                delete instance;
                return nullptr;
            }
        } catch (...) {
            delete instance;
            return nullptr;
        }

        return instance;
    }

    static void Destroy(void* instance) noexcept
    {
        delete static_cast<Plugin*>(instance);
    }

    static bool Process(void* instance, PluginAudioBuffer* buffer) noexcept
    {
        if (instance == nullptr || buffer == nullptr) {
            return false;
        }

        try {
            return static_cast<Plugin*>(instance)->Process(*buffer);
        } catch (...) {
            return false;
        }
    }

    static void Reset(void* instance) noexcept
    {
        if (instance == nullptr) {
            return;
        }

        try {
            static_cast<Plugin*>(instance)->Reset();
        } catch (...) {
        }
    }

    static bool SetParameter(void* instance, const char* name, float value) noexcept
    {
        if (instance == nullptr || name == nullptr) {
            return false;
        }

        try {
            return static_cast<Plugin*>(instance)->SetParameter(name, value);
        } catch (...) {
            return false;
        }
    }

    static bool GetParameter(void* instance, const char* name, float* value) noexcept
    {
        if (instance == nullptr || name == nullptr || value == nullptr) {
            return false;
        }

        try {
            return static_cast<Plugin*>(instance)->GetParameter(name, *value);
        } catch (...) {
            return false;
        }
    }
};

}