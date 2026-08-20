#include <LuAudio/Plugins/PluginAbi.h>

#include <cstring>

#if defined(__GNUC__) || defined(__clang__)
#define LUAUDIO_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define LUAUDIO_PLUGIN_EXPORT
#endif

namespace {

struct Instance {
    float gain = 2.0F;
};

void* Create(const LuAudio::Plugins::PluginInstanceConfig*)
{
    return new Instance();
}

void Destroy(void* instance)
{
    delete static_cast<Instance*>(instance);
}

bool Process(void* instance, LuAudio::Plugins::PluginAudioBuffer* buffer)
{
    const auto gain = static_cast<Instance*>(instance)->gain;
    for (std::uint32_t index = 0;
         index < buffer->frame_count * buffer->channel_count;
         ++index) {
        buffer->data[index] *= gain;
    }
    return true;
}

bool SetParameter(void* instance, const char* name, float value)
{
    if (std::strcmp(name, "gain") != 0) {
        return false;
    }
    static_cast<Instance*>(instance)->gain = value;
    return true;
}

bool GetParameter(void* instance, const char* name, float* value)
{
    if (std::strcmp(name, "gain") != 0 || value == nullptr) {
        return false;
    }
    *value = static_cast<Instance*>(instance)->gain;
    return true;
}

const LuAudio::Plugins::PluginDescriptor Descriptor{
    LuAudio::Plugins::PluginAbiVersion,
    "AndroidFixturePlugin",
    &Create,
    &Destroy,
    &Process,
    &SetParameter,
    &GetParameter,
    nullptr};

}

extern "C" LUAUDIO_PLUGIN_EXPORT
const LuAudio::Plugins::PluginDescriptor* LuAudio_GetPluginDescriptor()
{
    return &Descriptor;
}

#undef LUAUDIO_PLUGIN_EXPORT