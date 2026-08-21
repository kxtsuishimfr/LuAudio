#include <LuAudio/Plugins/Contracts/PluginAbi.h>

#include <cstring>

namespace {

struct Instance {
    float gain = 1.0F;
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
    "FixturePlugin",
    &Create,
    &Destroy,
    &Process,
    &SetParameter,
    &GetParameter,
    nullptr};

}

extern "C" __declspec(dllexport)
const LuAudio::Plugins::PluginDescriptor* LuAudio_GetPluginDescriptor()
{
    return &Descriptor;
}