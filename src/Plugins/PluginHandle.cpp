#include <LuAudio/Plugins/PluginHandle.h>

namespace LuAudio::Plugins {

PluginHandle::PluginHandle(
    std::unique_ptr<Providers::IPluginLibrary> library,
    const PluginDescriptor* descriptor)
    : library_(std::move(library)), descriptor_(descriptor)
{
}

PluginHandle::~PluginHandle()
{
    if (instance_ != nullptr && descriptor_->destroy != nullptr) {
        descriptor_->destroy(instance_);
    }
}

Audio::Result PluginHandle::Create(const PluginInstanceConfig& config)
{
    if (instance_ != nullptr) {
        return Audio::Result::Failure(
            Audio::ResultCode::InvalidState,
            "Plugin instance already exists");
    }

    instance_ = descriptor_->create(&config);
    if (instance_ == nullptr) {
        return Audio::Result::Failure(
            Audio::ResultCode::ProcessingFailed,
            "Plugin instance creation failed");
    }

    return Audio::Result::Success();
}

bool PluginHandle::Process(PluginAudioBuffer& buffer) noexcept
{
    return instance_ != nullptr && descriptor_->process != nullptr
        && descriptor_->process(instance_, &buffer);
}

void PluginHandle::Reset() noexcept
{
    if (instance_ != nullptr && descriptor_->reset != nullptr) {
        descriptor_->reset(instance_);
    }
}

bool PluginHandle::SetParameter(std::string_view name, float value) noexcept
{
    return instance_ != nullptr && descriptor_->set_parameter != nullptr
        && descriptor_->set_parameter(instance_, name.data(), value);
}

bool PluginHandle::GetParameter(std::string_view name, float& value) noexcept
{
    return instance_ != nullptr && descriptor_->get_parameter != nullptr
        && descriptor_->get_parameter(instance_, name.data(), &value);
}

const char* PluginHandle::Name() const noexcept
{
    return descriptor_->name;
}

}