#include <windows.h>

#include <filesystem>

#include <LuAudio/Providers/Windows/WPluginProvider.h>

namespace LuAudio::Providers::Windows {

namespace {

class WPluginLibrary final : public Providers::IPluginLibrary {
public:
    explicit WPluginLibrary(HMODULE module) noexcept
        : module_(module)
    {
    }

    ~WPluginLibrary() override
    {
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    void* Resolve(std::string_view name) noexcept override
    {
        return reinterpret_cast<void*>(GetProcAddress(module_, name.data()));
    }

private:
    HMODULE module_;
};

}

Audio::Result WPluginProvider::Open(
    const std::filesystem::path& path,
    std::unique_ptr<Providers::IPluginLibrary>& library)
{
    HMODULE module = LoadLibraryW(path.c_str());
    if (module == nullptr) {
        return Audio::Result::Failure(
            Audio::ResultCode::BackendUnavailable,
            "Unable to load Windows plugin library");
    }

    library = std::make_unique<WPluginLibrary>(module);
    return Audio::Result::Success();
}

}