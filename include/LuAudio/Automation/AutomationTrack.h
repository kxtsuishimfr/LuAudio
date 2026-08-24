#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <LuAudio/Audio/Contracts/Automation.h>

namespace LuAudio::Automation {

class AutomationTrack final : public IAutomationTrack {
public:
    explicit AutomationTrack(TrackDescription description);
    ~AutomationTrack();

    AutomationTrack(const AutomationTrack&) = delete;
    AutomationTrack& operator=(const AutomationTrack&) = delete;
    AutomationTrack(AutomationTrack&&) noexcept;
    AutomationTrack& operator=(AutomationTrack&&) noexcept;

    bool SetPoint(std::uint64_t frame, float value) override;
    bool RemovePoint(std::uint64_t frame) noexcept override;
    void Clear() noexcept override;

    std::size_t PointCount() const noexcept override;
    const Point* Points() const noexcept override;
    float Evaluate(std::uint64_t frame) const noexcept override;
    std::size_t EvaluateBlock(
        std::uint64_t startFrame,
        std::uint32_t frameCount,
        TargetHandle target,
        ControlEvent* output,
        std::size_t capacity) const noexcept override;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

}