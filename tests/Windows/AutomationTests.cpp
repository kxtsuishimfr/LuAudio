#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <random>
#include <vector>

#include <LuAudio/Automation/AutomationSet.h>
#include <LuAudio/Automation/AutomationTrack.h>

namespace {

using namespace LuAudio::Automation;

TrackDescription ContinuousDescription()
{
    return {ValueKind::Continuous, Interpolation::Linear, 0.0F, 4.0F};
}

TrackDescription DiscreteDescription()
{
    return {ValueKind::Discrete, Interpolation::Linear, 0.0F, 3.0F};
}

TEST(AutomationTests, MaintainsSortedPointsAndReplacesDuplicates)
{
    AutomationTrack track(ContinuousDescription());
    ASSERT_TRUE(track.SetPoint(20, 2.0F));
    ASSERT_TRUE(track.SetPoint(10, 1.0F));
    ASSERT_TRUE(track.SetPoint(20, 3.0F));

    ASSERT_EQ(track.PointCount(), 2U);
    EXPECT_EQ(track.Points()[0].frame, 10U);
    EXPECT_EQ(track.Points()[1].frame, 20U);
    EXPECT_FLOAT_EQ(track.Points()[1].value, 3.0F);
}

TEST(AutomationTests, RejectsNonFiniteAndOutOfRangeValues)
{
    AutomationTrack track(ContinuousDescription());

    EXPECT_FALSE(track.SetPoint(0, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(track.SetPoint(1, -1.0F));
    EXPECT_FALSE(track.SetPoint(2, 5.0F));
    EXPECT_EQ(track.PointCount(), 0U);
}

TEST(AutomationTests, EvaluatesLinearValuesAndClampsBoundaries)
{
    AutomationTrack track(ContinuousDescription());
    ASSERT_TRUE(track.SetPoint(10, 1.0F));
    ASSERT_TRUE(track.SetPoint(20, 3.0F));

    EXPECT_FLOAT_EQ(track.Evaluate(0), 1.0F);
    EXPECT_FLOAT_EQ(track.Evaluate(10), 1.0F);
    EXPECT_FLOAT_EQ(track.Evaluate(15), 2.0F);
    EXPECT_FLOAT_EQ(track.Evaluate(20), 3.0F);
    EXPECT_FLOAT_EQ(track.Evaluate(30), 3.0F);
}

TEST(AutomationTests, EvaluatesDiscreteValuesAsSteps)
{
    AutomationTrack track(DiscreteDescription());
    ASSERT_TRUE(track.SetPoint(10, 1.0F));
    ASSERT_TRUE(track.SetPoint(20, 3.0F));

    EXPECT_FLOAT_EQ(track.Evaluate(15), 1.0F);
    EXPECT_FLOAT_EQ(track.Evaluate(20), 3.0F);
}

TEST(AutomationTests, EmitsBlockStartAndInteriorPointEvents)
{
    AutomationTrack track(ContinuousDescription());
    ASSERT_TRUE(track.SetPoint(10, 1.0F));
    ASSERT_TRUE(track.SetPoint(20, 3.0F));
    ControlEvent events[2]{};

    const auto count = track.EvaluateBlock(5, 20, 42, events, 2);

    ASSERT_EQ(count, 2U);
    EXPECT_EQ(events[0].frameOffset, 0U);
    EXPECT_EQ(events[0].target, 42U);
    EXPECT_FLOAT_EQ(events[0].value, 1.0F);
    EXPECT_EQ(events[1].frameOffset, 5U);
    EXPECT_FLOAT_EQ(events[1].value, 1.0F);
}

TEST(AutomationTests, LimitsBlockOutputToCapacity)
{
    AutomationTrack track(ContinuousDescription());
    ASSERT_TRUE(track.SetPoint(10, 1.0F));
    ASSERT_TRUE(track.SetPoint(20, 2.0F));
    ASSERT_TRUE(track.SetPoint(30, 3.0F));
    ControlEvent event{};

    EXPECT_EQ(track.EvaluateBlock(0, 40, 1, &event, 1), 1U);
    EXPECT_EQ(event.frameOffset, 0U);
}

TEST(AutomationTests, RemovesAndClearsPoints)
{
    AutomationTrack track(ContinuousDescription());
    ASSERT_TRUE(track.SetPoint(10, 1.0F));
    ASSERT_TRUE(track.SetPoint(20, 2.0F));

    EXPECT_TRUE(track.RemovePoint(10));
    EXPECT_FALSE(track.RemovePoint(10));
    EXPECT_EQ(track.PointCount(), 1U);
    track.Clear();
    EXPECT_EQ(track.PointCount(), 0U);
}

TEST(AutomationTests, EmptyTrackEvaluatesToZeroAndEmitsNoEvents)
{
    AutomationTrack track(ContinuousDescription());
    ControlEvent event{1, 2, 3.0F};

    EXPECT_FLOAT_EQ(track.Evaluate(100), 0.0F);
    EXPECT_EQ(track.EvaluateBlock(0, 10, 2, &event, 1), 1U);
    EXPECT_FLOAT_EQ(event.value, 0.0F);
}

TEST(AutomationTests, RejectsInvalidTrackRanges)
{
    AutomationTrack track({ValueKind::Continuous, Interpolation::Linear, 2.0F, 1.0F});

    EXPECT_FALSE(track.SetPoint(0, 1.5F));
    EXPECT_EQ(track.PointCount(), 0U);
}

TEST(AutomationTests, UsesStepInterpolationForExplicitStepTracks)
{
    AutomationTrack track({ValueKind::Continuous, Interpolation::Step, 0.0F, 4.0F});
    ASSERT_TRUE(track.SetPoint(10, 1.0F));
    ASSERT_TRUE(track.SetPoint(20, 4.0F));

    EXPECT_FLOAT_EQ(track.Evaluate(19), 1.0F);
    EXPECT_FLOAT_EQ(track.Evaluate(20), 4.0F);
}

TEST(AutomationTests, IncludesStartValueAndExcludesEndBoundary)
{
    AutomationTrack track(ContinuousDescription());
    ASSERT_TRUE(track.SetPoint(10, 1.0F));
    ASSERT_TRUE(track.SetPoint(20, 2.0F));
    ControlEvent events[4]{};

    const auto count = track.EvaluateBlock(10, 10, 5, events, 4);

    ASSERT_EQ(count, 1U);
    EXPECT_EQ(events[0].frameOffset, 0U);
    EXPECT_FLOAT_EQ(events[0].value, 1.0F);
}

TEST(AutomationTests, RejectsBlockFrameOverflow)
{
    AutomationTrack track(ContinuousDescription());
    ASSERT_TRUE(track.SetPoint(0, 1.0F));
    ControlEvent event{};

    EXPECT_EQ(track.EvaluateBlock(
        std::numeric_limits<std::uint64_t>::max(), 2, 1, &event, 1), 0U);
}

TEST(AutomationTests, HandlesNullAndZeroCapacityBlockOutput)
{
    AutomationTrack track(ContinuousDescription());
    ASSERT_TRUE(track.SetPoint(0, 1.0F));

    EXPECT_EQ(track.EvaluateBlock(0, 1, 1, nullptr, 1), 0U);
    EXPECT_EQ(track.EvaluateBlock(0, 1, 1, nullptr, 0), 0U);
    EXPECT_EQ(track.EvaluateBlock(0, 0, 1, nullptr, 0), 0U);
}

TEST(AutomationTests, AutomationSetReplacesTargetsAndOrdersEvents)
{
    auto first = std::make_shared<AutomationTrack>(ContinuousDescription());
    auto second = std::make_shared<AutomationTrack>(ContinuousDescription());
    ASSERT_TRUE(first->SetPoint(20, 2.0F));
    ASSERT_TRUE(second->SetPoint(10, 3.0F));

    AutomationSet set;
    ASSERT_TRUE(set.Set(20, first));
    ASSERT_TRUE(set.Set(10, second));
    ASSERT_TRUE(set.Set(20, second));
    ASSERT_EQ(set.Size(), 2U);

    std::array<ControlEvent, 4> events{};
    const auto count = set.EvaluateBlock(0, 30, events.data(), events.size());

    ASSERT_EQ(count, 4U);
    EXPECT_EQ(events[0].frameOffset, 0U);
    EXPECT_EQ(events[0].target, 10U);
    EXPECT_EQ(events[1].frameOffset, 0U);
    EXPECT_EQ(events[1].target, 20U);
    EXPECT_EQ(events[2].frameOffset, 10U);
    EXPECT_EQ(events[2].target, 10U);
    EXPECT_EQ(events[3].frameOffset, 10U);
    EXPECT_EQ(events[3].target, 20U);
    EXPECT_TRUE(set.Remove(10));
    EXPECT_FALSE(set.Remove(10));
    set.Clear();
    EXPECT_EQ(set.Size(), 0U);
}

TEST(AutomationTests, AutomationSetRejectsInvalidRegistrations)
{
    AutomationSet set;
    EXPECT_FALSE(set.Set(0, nullptr));
    EXPECT_FALSE(set.Set(1, nullptr));
    EXPECT_EQ(set.EvaluateBlock(0, 1, nullptr, 0), 0U);
}

TEST(AutomationTests, AutomationSetHonorsOutputCapacity)
{
    auto track = std::make_shared<AutomationTrack>(ContinuousDescription());
    ASSERT_TRUE(track->SetPoint(0, 1.0F));
    ASSERT_TRUE(track->SetPoint(1, 2.0F));
    ASSERT_TRUE(track->SetPoint(2, 3.0F));
    AutomationSet set;
    ASSERT_TRUE(set.Set(1, track));
    ControlEvent event{};

    EXPECT_EQ(set.EvaluateBlock(0, 4, &event, 1), 1U);
    EXPECT_EQ(event.frameOffset, 0U);
}

TEST(AutomationTests, StressTestsRandomizedInsertionAndEvaluation)
{
    constexpr std::uint64_t frameCount = 100000;
    constexpr std::size_t pointCount = 5000;
    AutomationTrack track(ContinuousDescription());
    std::mt19937 generator(12345);
    std::uniform_int_distribution<std::uint64_t> frameDistribution(0, frameCount);
    std::uniform_real_distribution<float> valueDistribution(0.0F, 4.0F);
    std::vector<Point> expected;
    expected.reserve(pointCount);

    for (std::size_t index = 0; index < pointCount; ++index) {
        const auto frame = frameDistribution(generator);
        const auto value = valueDistribution(generator);
        ASSERT_TRUE(track.SetPoint(frame, value));
        const auto existing = std::find_if(expected.begin(), expected.end(),
            [frame](const Point& point) { return point.frame == frame; });
        if (existing == expected.end())
            expected.push_back({frame, value});
        else
            existing->value = value;
    }
    std::sort(expected.begin(), expected.end(),
        [](const Point& left, const Point& right) { return left.frame < right.frame; });

    ASSERT_EQ(track.PointCount(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(track.Points()[index].frame, expected[index].frame);
        EXPECT_FLOAT_EQ(track.Points()[index].value, expected[index].value);
    }
    for (std::uint64_t frame = 0; frame < frameCount; frame += 37)
        EXPECT_TRUE(std::isfinite(track.Evaluate(frame)));
}

TEST(AutomationTests, StressTestsRepeatedBlockEvaluation)
{
    AutomationTrack track(ContinuousDescription());
    for (std::uint64_t frame = 0; frame < 10000; frame += 7)
        ASSERT_TRUE(track.SetPoint(frame, static_cast<float>(frame % 5)));

    std::array<ControlEvent, 256> events{};
    for (std::uint64_t frame = 0; frame < 10000; frame += 64) {
        const auto count = track.EvaluateBlock(frame, 64, 99, events.data(), events.size());
        ASSERT_GE(count, 1U);
        ASSERT_LE(count, events.size());
        for (std::size_t index = 1; index < count; ++index)
            EXPECT_LE(events[index - 1].frameOffset, events[index].frameOffset);
    }
}

}