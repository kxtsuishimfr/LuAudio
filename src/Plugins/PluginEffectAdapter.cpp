#include <LuAudio/Plugins/PluginEffectAdapter.h>

namespace LuAudio::Plugins {

PluginEffectAdapter::PluginEffectAdapter(
    std::unique_ptr<PluginHandle> plugin,
    const PluginInstanceConfig& config)
    : plugin_(std::move(plugin))
{
    if (plugin_) {
        ready_ = plugin_->Create(config).Succeeded();
    }
}

bool PluginEffectAdapter::Process(Audio::AudioBuffer& buffer) noexcept
{
    if (!ready_) {
        return false;
    }

    PluginAudioBuffer pluginBuffer{
        buffer.Data(),
        static_cast<std::uint32_t>(buffer.FrameCount()),
        buffer.Format().channelCount,
        buffer.Format().sampleRate};
    return plugin_->Process(pluginBuffer);
}

void PluginEffectAdapter::Reset() noexcept
{
    if (ready_) {
        plugin_->Reset();
    }
}

bool PluginEffectAdapter::SetParameter(std::string_view name, float value) noexcept
{
    return ready_ && plugin_->SetParameter(name, value);
}

bool PluginEffectAdapter::GetParameter(std::string_view name, float& value) noexcept
{
    return ready_ && plugin_->GetParameter(name, value);
}

bool PluginEffectAdapter::IsReady() const noexcept
{
    return ready_;
}

}