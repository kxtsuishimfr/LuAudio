#pragma once

#include <cstdint>

namespace LuAudio::Plugins {

inline constexpr std::uint32_t PluginAbiVersion = 1;
inline constexpr char PluginDescriptorSymbol[] = "LuAudio_GetPluginDescriptor";

extern "C" {

struct PluginAudioBuffer {
    float* data;
    std::uint32_t frame_count;
    std::uint32_t channel_count;
    std::uint32_t sample_rate;
};

struct PluginInstanceConfig {
    std::uint32_t sample_rate;
    std::uint32_t channel_count;
    std::uint32_t max_frame_count;
};

using PluginCreate = void* (*)(const PluginInstanceConfig* config);
using PluginDestroy = void (*)(void* instance);
using PluginProcess = bool (*)(void* instance, PluginAudioBuffer* buffer);
using PluginSetParameter = bool (*)(void* instance, const char* name, float value);
using PluginGetParameter = bool (*)(void* instance, const char* name, float* value);
using PluginReset = void (*)(void* instance);

struct PluginDescriptor {
    std::uint32_t abi_version;
    const char* name;
    PluginCreate create;
    PluginDestroy destroy;
    PluginProcess process;
    PluginSetParameter set_parameter;
    PluginGetParameter get_parameter;
    PluginReset reset;
};

using GetPluginDescriptor = const PluginDescriptor* (*)();

}

}