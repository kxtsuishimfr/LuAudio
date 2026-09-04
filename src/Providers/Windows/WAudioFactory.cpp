#include <LuAudio/Audio/Contracts/AudioBackendFactory.h>

#include <LuAudio/Providers/Windows/WAudioDecoder.h>
#include <LuAudio/Providers/Windows/Wasapi/WasapiBackend.h>

namespace LuAudio::Audio {

std::unique_ptr<IAudioBackend> CreateBackend()
{
    return std::make_unique<Providers::Windows::Wasapi::WasapiBackend>();
}

std::unique_ptr<IAudioDecoder> CreateDecoder()
{
    return std::make_unique<Providers::Windows::WAudioDecoder>();
}

}