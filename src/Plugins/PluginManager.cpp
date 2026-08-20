#include <LuAudio/Plugins/PluginManager.h>

namespace LuAudio::Plugins {

PluginManager::PluginManager(Providers::IPluginProvider& provider) noexcept
    : provider_(provider)
{
}

Audio::Result PluginManager::Load(
    const std::filesystem::path& path,
    std::unique_ptr<PluginHandle>& plugin)
{
    std::unique_ptr<Providers::IPluginLibrary> library;
    Audio::Result result = provider_.Open(path, library);
    if (!result.Succeeded()) {
        return result;
    }

    void* symbol = library->Resolve(PluginDescriptorSymbol);
    if (symbol == nullptr) {
        return Audio::Result::Failure(
            Audio::ResultCode::InvalidArgument,
            "Plugin descriptor symbol is missing");
    }

    auto getDescriptor = reinterpret_cast<GetPluginDescriptor>(symbol);
    const PluginDescriptor* descriptor = getDescriptor();
    if (descriptor == nullptr || descriptor->abi_version != PluginAbiVersion
        || descriptor->name == nullptr || descriptor->create == nullptr
        || descriptor->destroy == nullptr || descriptor->process == nullptr) {
        return Audio::Result::Failure(
            Audio::ResultCode::InvalidArgument,
            "Plugin descriptor is invalid");
    }

    plugin = std::make_unique<PluginHandle>(std::move(library), descriptor);
    return Audio::Result::Success();
}

}