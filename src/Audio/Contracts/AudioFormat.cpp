#include <LuAudio/Audio/Contracts/AudioFormat.h>

namespace LuAudio::Audio {

bool AudioFormat::IsValid() const noexcept
{
    return sampleRate > 0 && channelCount > 0;
}

bool AudioFormat::CanMixInto(const AudioFormat& output) const noexcept
{
    return sampleRate == output.sampleRate &&
        sampleType == output.sampleType &&
        channelLayout == output.channelLayout &&
        (channelCount == 1 || channelCount == output.channelCount);
}

}
