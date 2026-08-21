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

enum class PluginParameterType : std::uint32_t {
    Float = 0,
    Enum = 1
};

struct PluginParameterChoice {
    const char* name;
    float value;
};

struct PluginParameterDescriptor {
    const char* key;
    const char* name;
    PluginParameterType type;
    float default_value;
    float minimum;
    float maximum;
    float step;
    const char* unit;
    const char* group;
    std::uint32_t choice_count;
    const PluginParameterChoice* choices;
};

struct PluginDescriptor {
    std::uint32_t abi_version;
    const char* name;
    const char* id;
    const char* version;
    const char* description;
    const char* vendor;
    std::uint32_t parameter_count;
    const PluginParameterDescriptor* parameters;
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