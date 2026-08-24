#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <LuAudio/Audio/Contracts/Automation.h>

namespace LuAudio::Automation {

class AutomationSet final : public IAutomationSet {
public:
    AutomationSet() = default;
    ~AutomationSet() = default;

    AutomationSet(const AutomationSet&) = delete;
    AutomationSet& operator=(const AutomationSet&) = delete;
    AutomationSet(AutomationSet&&) noexcept = default;
    AutomationSet& operator=(AutomationSet&&) noexcept = default;

    bool Set(TargetHandle target, std::shared_ptr<const IAutomationTrack> track) override;
    bool Remove(TargetHandle target) noexcept override;
    void Clear() noexcept override;

    std::size_t Size() const noexcept override;
    std::size_t EvaluateBlock(
        std::uint64_t startFrame,
        std::uint32_t frameCount,
        ControlEvent* output,
        std::size_t capacity) const noexcept override;

private:
    struct Entry {
        TargetHandle target;
        std::shared_ptr<const IAutomationTrack> track;
    };

    std::vector<Entry> entries_;
};

}