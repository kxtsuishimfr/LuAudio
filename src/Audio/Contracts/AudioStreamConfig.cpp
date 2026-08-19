#include <LuAudio/Audio/Contracts/AudioStreamConfig.h>

namespace LuAudio::Audio {

bool AudioStreamConfig::IsValid() const noexcept
{
    return format.IsValid() && framesPerBuffer > 0;
}

}
