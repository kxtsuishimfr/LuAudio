#include <LuAudio/Audio/Contracts/AudioBackendFactory.h>

#include <LuAudio/Providers/Android/AAudioDecoder.h>
#include <LuAudio/Providers/Android/Oboe/OboeBackend.h>

namespace LuAudio::Audio {

std::unique_ptr<IAudioBackend> CreateBackend()
{
    return std::make_unique<Providers::Android::Oboe::OboeBackend>();
}

std::unique_ptr<IAudioDecoder> CreateDecoder()
{
    return std::make_unique<Providers::Android::AAudioDecoder>();
}

}