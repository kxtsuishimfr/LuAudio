#include <dlfcn.h>

#include <filesystem>

#include <LuAudio/Providers/Android/APluginProvider.h>

namespace LuAudio::Providers::Android {

namespace {

class APluginLibrary final : public Providers::IPluginLibrary {
public:
    explicit APluginLibrary(void* handle) noexcept
        : handle_(handle)
    {
    }

    ~APluginLibrary() override
    {
        if (handle_ != nullptr) {
            dlclose(handle_);
        }
    }

    void* Resolve(std::string_view name) noexcept override
    {
        return dlsym(handle_, name.data());
    }

private:
    void* handle_;
};

}

Audio::Result APluginProvider::Open(
    const std::filesystem::path& path,
    std::unique_ptr<Providers::IPluginLibrary>& library)
{
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* loaderError = dlerror();
        std::string message = "Unable to load Android plugin library: " + path.string();
        if (loaderError != nullptr) {
            message += " (";
            message += loaderError;
            message += ")";
        }
        return Audio::Result::Failure(
            Audio::ResultCode::BackendUnavailable,
            std::move(message));
    }

    library = std::make_unique<APluginLibrary>(handle);
    return Audio::Result::Success();
}

}