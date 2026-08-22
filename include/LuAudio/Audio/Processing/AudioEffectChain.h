#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioEffect.h>

namespace LuAudio::Audio {

/**
 * @summary Processes active effects in deterministic insertion order.
 */
class AudioEffectChain final {
public:
    using EffectId = std::size_t;

    /**
     * @summary Adds an effect to the end of the chain.
     * @param effect Effect owned by the chain.
     * @returns Stable effect identifier.
     */
    EffectId Add(std::unique_ptr<IAudioEffect> effect);
    /**
     * @summary Removes an effect by identifier.
     * @param id Effect identifier.
     * @returns True when an effect was removed.
     */
    bool Remove(EffectId id) noexcept;
    /**
     * @summary Enables or disables an effect.
     * @param id Effect identifier.
     * @param active Whether the effect participates in processing.
     * @returns True when the identifier exists.
     */
    bool SetActive(EffectId id, bool active) noexcept;
    /**
     * @summary Enables or disables bypass for an effect.
     * @param id Effect identifier.
     * @param bypassed Whether processing is bypassed.
     * @returns True when the identifier exists.
     */
    bool SetBypassed(EffectId id, bool bypassed) noexcept;
    /**
     * @summary Sets the processing order for the chain based on a sequence of effect IDs.
     * @param orderedIds Sequence of effect identifiers in the desired processing order.
     * @returns True when all effects are present exactly once in the supplied order.
     */
    bool SetOrder(std::vector<EffectId> orderedIds) noexcept;
    /**
     * @summary Processes the buffer through active, non-bypassed effects.
     * @param buffer Buffer to process in place.
     * @returns False when an effect fails; later effects are not called.
     */
    bool Process(AudioBuffer& buffer) const noexcept;
    /** @summary Resets every effect in insertion order. */
    void Reset() noexcept;
    /** @summary Gets the number of effects currently in the chain. */
    std::size_t Size() const noexcept;

private:
    struct Entry {
        EffectId id;
        std::unique_ptr<IAudioEffect> effect;
        bool active = true;
        bool bypassed = false;
    };

    std::vector<Entry> effects_;
    EffectId nextId_ = 0;
};

}