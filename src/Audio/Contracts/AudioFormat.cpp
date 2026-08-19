#include <LuAudio/Audio/Contracts/AudioFormat.h>

namespace LuAudio::Audio {

bool AudioFormat::IsValid() const noexcept
{
    return sampleRate > 0 && channelCount > 0;
}

}
