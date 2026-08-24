#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace LuAudio::Automation {

using TargetHandle = std::uint64_t;

enum class ValueKind : std::uint32_t {
    Continuous = 0,
    Discrete = 1
};

enum class Interpolation : std::uint32_t {
    Linear = 0,
    Step = 1
};

struct Point {
    std::uint64_t frame;
    float value;
};

struct TrackDescription {
    ValueKind valueKind;
    Interpolation interpolation;
    float minimum;
    float maximum;
};

struct ControlEvent {
    std::uint32_t frameOffset;
    TargetHandle target;
    float value;
};

struct ControlBlock {
    std::uint64_t startFrame;
    std::uint32_t frameCount;
    const ControlEvent* events;
    std::size_t eventCount;
};

class IControlTarget {
public:
    virtual ~IControlTarget() = default;

    virtual void Apply(
        std::uint32_t frameOffset,
        float value) noexcept = 0;

    virtual bool TryGetValue(float& value) const noexcept
    {
        return false;
    }
};

class IAutomationTrack {
public:
    virtual ~IAutomationTrack() = default;

    virtual bool SetPoint(std::uint64_t frame, float value) = 0;
    virtual bool RemovePoint(std::uint64_t frame) noexcept = 0;
    virtual void Clear() noexcept = 0;
    virtual std::size_t PointCount() const noexcept = 0;
    virtual const Point* Points() const noexcept = 0;
    virtual float Evaluate(std::uint64_t frame) const noexcept = 0;
    virtual std::size_t EvaluateBlock(
        std::uint64_t startFrame,
        std::uint32_t frameCount,
        TargetHandle target,
        ControlEvent* output,
        std::size_t capacity) const noexcept = 0;
};

class IAutomationSet {
public:
    virtual ~IAutomationSet() = default;

    virtual bool Set(
        TargetHandle target,
        std::shared_ptr<const IAutomationTrack> track) = 0;
    virtual bool Remove(TargetHandle target) noexcept = 0;
    virtual void Clear() noexcept = 0;
    virtual std::size_t Size() const noexcept = 0;
    virtual std::size_t EvaluateBlock(
        std::uint64_t startFrame,
        std::uint32_t frameCount,
        ControlEvent* output,
        std::size_t capacity) const noexcept = 0;
};

}