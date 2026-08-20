#include <LuAudio/Audio/Processing/AudioEffectChain.h>

namespace LuAudio::Audio {

AudioEffectChain::EffectId AudioEffectChain::Add(std::unique_ptr<IAudioEffect> effect)
{
    if (!effect) {
        return static_cast<EffectId>(-1);
    }

    const EffectId id = nextId_++;
    effects_.push_back({id, std::move(effect), true, false});
    return id;
}

bool AudioEffectChain::Remove(EffectId id) noexcept
{
    const auto iterator = std::find_if(
        effects_.begin(),
        effects_.end(),
        [id](const Entry& entry) { return entry.id == id; });
    if (iterator == effects_.end()) {
        return false;
    }

    effects_.erase(iterator);
    return true;
}

bool AudioEffectChain::SetActive(EffectId id, bool active) noexcept
{
    const auto iterator = std::find_if(
        effects_.begin(),
        effects_.end(),
        [id](const Entry& entry) { return entry.id == id; });
    if (iterator == effects_.end()) {
        return false;
    }

    iterator->active = active;
    return true;
}

bool AudioEffectChain::SetBypassed(EffectId id, bool bypassed) noexcept
{
    const auto iterator = std::find_if(
        effects_.begin(),
        effects_.end(),
        [id](const Entry& entry) { return entry.id == id; });
    if (iterator == effects_.end()) {
        return false;
    }

    iterator->bypassed = bypassed;
    return true;
}

bool AudioEffectChain::Process(AudioBuffer& buffer) noexcept
{
    for (Entry& entry : effects_) {
        if (entry.active && !entry.bypassed && !entry.effect->Process(buffer)) {
            return false;
        }
    }

    return true;
}

void AudioEffectChain::Reset() noexcept
{
    for (Entry& entry : effects_) {
        entry.effect->Reset();
    }
}

std::size_t AudioEffectChain::Size() const noexcept
{
    return effects_.size();
}

}