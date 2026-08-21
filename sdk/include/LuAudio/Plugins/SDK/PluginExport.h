#pragma once

#include <LuAudio/Plugins/SDK/PluginTrampoline.h>

#if defined(_WIN32)
#define LUAUDIO_PLUGIN_ABI extern "C" __declspec(dllexport)
#else
#define LUAUDIO_PLUGIN_ABI extern "C" __attribute__((visibility("default")))
#endif

#define LUAUDIO_DECLARE_PLUGIN(PluginClass, displayName) \
    LUAUDIO_PLUGIN_ABI const LuAudio::Plugins::PluginDescriptor* LuAudio_GetPluginDescriptor() \
    { \
        return LuAudio::Plugins::SDK::PluginTrampoline<PluginClass>::Descriptor(displayName); \
    }