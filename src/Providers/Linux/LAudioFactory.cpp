#include <LuAudio/Common.h>

#if defined(__linux__)

#include <LuAudio/Audio/Contracts/AudioBackendFactory.h>

#include <LuAudio/Providers/Linux/LAudioDecoder.h>
#include <LuAudio/Providers/Linux/PipeWire/PipeWireBackend.h>

namespace LuAudio::Audio {

std::unique_ptr<IAudioBackend> CreateBackend()
{
    return std::make_unique<Providers::Linux::PipeWire::PipeWireBackend>();
}

std::unique_ptr<IAudioDecoder> CreateDecoder()
{
    return std::make_unique<Providers::Linux::LAudioDecoder>();
}

}

#endif