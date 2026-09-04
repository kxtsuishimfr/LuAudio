#include <LuAudio/Common.h>

#if defined(__linux__)

#include <dlfcn.h>

#include <LuAudio/Providers/Linux/LPluginProvider.h>

namespace LuAudio::Providers::Linux {

namespace {

class LPluginLibrary final : public Providers::IPluginLibrary {
public:
    explicit LPluginLibrary(void* handle) noexcept
        : handle_(handle)
    {
    }

    ~LPluginLibrary() override
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

Audio::Result LPluginProvider::Open(
    const std::filesystem::path& path,
    std::unique_ptr<Providers::IPluginLibrary>& library)
{
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::string message = "Unable to load Linux plugin library: " + path.string();
        if (const char* loaderError = dlerror(); loaderError != nullptr) {
            message += " (";
            message += loaderError;
            message += ")";
        }
        return Audio::Result::Failure(
            Audio::ResultCode::BackendUnavailable,
            std::move(message));
    }

    library = std::make_unique<LPluginLibrary>(handle);
    return Audio::Result::Success();
}

#endif

}
