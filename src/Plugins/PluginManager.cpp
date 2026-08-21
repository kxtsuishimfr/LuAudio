#include <LuAudio/Plugins/PluginManager.h>

#include <cstring>

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
    bool valid_parameters = descriptor != nullptr && descriptor->parameters != nullptr;
    if (valid_parameters) {
        for (std::uint32_t index = 0; index < descriptor->parameter_count; ++index) {
            const auto& parameter = descriptor->parameters[index];
            if (parameter.key == nullptr || parameter.name == nullptr
                || parameter.minimum > parameter.maximum
                || parameter.default_value < parameter.minimum
                || parameter.default_value > parameter.maximum
                || parameter.step <= 0.0F
                || (parameter.type != PluginParameterType::Float
                    && parameter.type != PluginParameterType::Enum)
                || (parameter.type == PluginParameterType::Enum
                    && (parameter.choice_count == 0 || parameter.choices == nullptr))) {
                valid_parameters = false;
                break;
            }
            for (std::uint32_t other = 0; other < index; ++other) {
                if (std::strcmp(parameter.key, descriptor->parameters[other].key) == 0) {
                    valid_parameters = false;
                    break;
                }
            }
            if (!valid_parameters) {
                break;
            }
        }
    }

    if (descriptor == nullptr || descriptor->abi_version != PluginAbiVersion
        || descriptor->name == nullptr || descriptor->id == nullptr
        || descriptor->version == nullptr
        || !valid_parameters
        || descriptor->create == nullptr
        || descriptor->destroy == nullptr || descriptor->process == nullptr) {
        return Audio::Result::Failure(
            Audio::ResultCode::InvalidArgument,
            "Plugin descriptor is invalid");
    }

    plugin = std::make_unique<PluginHandle>(std::move(library), descriptor);
    return Audio::Result::Success();
}

}