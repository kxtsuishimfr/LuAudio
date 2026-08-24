#include <LuAudio/Automation/AutomationSet.h>
#include <LuAudio/Automation/AutomationTrack.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace LuAudio::Automation {

class AutomationTrack::Implementation final {
public:
	explicit Implementation(TrackDescription description)
		: description_(description)
	{
	}

	TrackDescription description_;
	std::vector<Point> points_;
};

AutomationTrack::AutomationTrack(TrackDescription description)
	: implementation_(std::make_unique<Implementation>(description))
{
}

AutomationTrack::~AutomationTrack() = default;

AutomationTrack::AutomationTrack(AutomationTrack&&) noexcept = default;

AutomationTrack& AutomationTrack::operator=(AutomationTrack&&) noexcept = default;

bool AutomationTrack::SetPoint(std::uint64_t frame, float value)
{
	if (!std::isfinite(value) ||
		value < implementation_->description_.minimum ||
		value > implementation_->description_.maximum) {
		return false;
	}

	auto& points = implementation_->points_;
	const auto iterator = std::lower_bound(
		points.begin(),
		points.end(),
		frame,
		[](const Point& point, std::uint64_t requestedFrame) {
			return point.frame < requestedFrame;
		});
	if (iterator != points.end() && iterator->frame == frame) {
		iterator->value = value;
	} else {
		points.insert(iterator, {frame, value});
	}
	return true;
}

bool AutomationTrack::RemovePoint(std::uint64_t frame) noexcept
{
	auto& points = implementation_->points_;
	const auto iterator = std::lower_bound(
		points.begin(),
		points.end(),
		frame,
		[](const Point& point, std::uint64_t requestedFrame) {
			return point.frame < requestedFrame;
		});
	if (iterator == points.end() || iterator->frame != frame) {
		return false;
	}
	points.erase(iterator);
	return true;
}

void AutomationTrack::Clear() noexcept
{
	implementation_->points_.clear();
}

std::size_t AutomationTrack::PointCount() const noexcept
{
	return implementation_->points_.size();
}

const Point* AutomationTrack::Points() const noexcept
{
	return implementation_->points_.data();
}

float AutomationTrack::Evaluate(std::uint64_t frame) const noexcept
{
	const auto& points = implementation_->points_;
	if (points.empty()) {
		return 0.0F;
	}
	if (frame <= points.front().frame) {
		return points.front().value;
	}
	if (frame >= points.back().frame) {
		return points.back().value;
	}

	const auto upper = std::upper_bound(
		points.begin(),
		points.end(),
		frame,
		[](std::uint64_t requestedFrame, const Point& point) {
			return requestedFrame < point.frame;
		});
	const Point& right = *upper;
	const Point& left = *(upper - 1);
	if (implementation_->description_.interpolation == Interpolation::Step ||
		implementation_->description_.valueKind == ValueKind::Discrete) {
		return left.value;
	}

	const double span = static_cast<double>(right.frame - left.frame);
	const double progress = static_cast<double>(frame - left.frame) / span;
	return static_cast<float>(left.value + (right.value - left.value) * progress);
}

std::size_t AutomationTrack::EvaluateBlock(
	std::uint64_t startFrame,
	std::uint32_t frameCount,
	TargetHandle target,
	ControlEvent* output,
	std::size_t capacity) const noexcept
{
	if (frameCount == 0 || output == nullptr || capacity == 0 ||
		startFrame > std::numeric_limits<std::uint64_t>::max() - frameCount) {
		return 0;
	}

	const std::uint64_t endFrame = startFrame + frameCount;
	std::size_t required = 1;
	for (const Point& point : implementation_->points_) {
		if (point.frame > startFrame && point.frame < endFrame) {
			++required;
		}
	}

	const std::size_t written = std::min(required, capacity);
	output[0] = {0, target, Evaluate(startFrame)};
	if (written == 1) {
		return written;
	}

	std::size_t outputIndex = 1;
	for (const Point& point : implementation_->points_) {
		if (point.frame <= startFrame || point.frame >= endFrame) {
			continue;
		}
		output[outputIndex++] = {
			static_cast<std::uint32_t>(point.frame - startFrame),
			target,
			Evaluate(point.frame)};
		if (outputIndex == written) {
			break;
		}
	}
	return written;
}

bool AutomationSet::Set(
	TargetHandle target,
	std::shared_ptr<const IAutomationTrack> track)
{
	if (target == 0 || !track) {
		return false;
	}

	const auto iterator = std::find_if(
		entries_.begin(),
		entries_.end(),
		[target](const Entry& entry) { return entry.target == target; });
	if (iterator != entries_.end()) {
		iterator->track = std::move(track);
	} else {
		entries_.push_back({target, std::move(track)});
	}
	return true;
}

bool AutomationSet::Remove(TargetHandle target) noexcept
{
	const auto iterator = std::find_if(
		entries_.begin(),
		entries_.end(),
		[target](const Entry& entry) { return entry.target == target; });
	if (iterator == entries_.end()) {
		return false;
	}
	entries_.erase(iterator);
	return true;
}

void AutomationSet::Clear() noexcept
{
	entries_.clear();
}

std::size_t AutomationSet::Size() const noexcept
{
	return entries_.size();
}

std::size_t AutomationSet::EvaluateBlock(
	std::uint64_t startFrame,
	std::uint32_t frameCount,
	ControlEvent* output,
	std::size_t capacity) const noexcept
{
	if (frameCount == 0 || output == nullptr || capacity == 0) {
		return 0;
	}

	std::size_t written = 0;
	for (const Entry& entry : entries_) {
		if (written == capacity) {
			break;
		}

		written += entry.track->EvaluateBlock(
			startFrame,
			frameCount,
			entry.target,
			output + written,
			capacity - written);
	}
	std::sort(
		output,
		output + written,
		[](const ControlEvent& left, const ControlEvent& right) {
			if (left.frameOffset != right.frameOffset) {
				return left.frameOffset < right.frameOffset;
			}
			return left.target < right.target;
		});
	return written;
}

}