#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>

#include <LuAudio/Audio/Processing/AudioEffectChain.h>

namespace {

class TestEffect final : public LuAudio::Audio::IAudioEffect {
public:
    TestEffect(float multiplier, std::vector<int>& order, int orderValue)
        : multiplier_(multiplier), order_(order), orderValue_(orderValue)
    {
    }

    bool Process(LuAudio::Audio::AudioBuffer& buffer) noexcept override
    {
        order_.push_back(orderValue_);
        if (shouldFail_) {
            return false;
        }

        for (std::size_t index = 0; index < buffer.SampleCount(); ++index) {
            buffer.Data()[index] *= multiplier_;
        }
        return true;
    }

    void Reset() noexcept override
    {
        ++resetCount_;
    }

    void Fail() noexcept
    {
        shouldFail_ = true;
    }

    int ResetCount() const noexcept
    {
        return resetCount_;
    }

private:
    float multiplier_;
    std::vector<int>& order_;
    int orderValue_;
    bool shouldFail_ = false;
    int resetCount_ = 0;
};

LuAudio::Audio::AudioBuffer CreateBuffer()
{
    LuAudio::Audio::AudioBuffer buffer({}, 2);
    buffer.Data()[0] = 1.0F;
    buffer.Data()[1] = 2.0F;
    buffer.Data()[2] = 3.0F;
    buffer.Data()[3] = 4.0F;
    return buffer;
}

TEST(AudioEffectChainTests, ProcessesEffectsInInsertionOrder)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    auto buffer = CreateBuffer();

    ASSERT_TRUE(chain.Process(buffer));

    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_FLOAT_EQ(buffer.Data()[0], 6.0F);
    EXPECT_FLOAT_EQ(buffer.Data()[3], 24.0F);
}

TEST(AudioEffectChainTests, BypassedAndInactiveEffectsAreSkipped)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto bypassed = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto inactive = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    chain.SetBypassed(bypassed, true);
    chain.SetActive(inactive, false);
    auto buffer = CreateBuffer();

    ASSERT_TRUE(chain.Process(buffer));

    EXPECT_TRUE(order.empty());
    EXPECT_FLOAT_EQ(buffer.Data()[0], 1.0F);
    EXPECT_FLOAT_EQ(buffer.Data()[3], 4.0F);
}

TEST(AudioEffectChainTests, ReordersEffectsToCustomSequence)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto first = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto second = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    auto third = chain.Add(std::make_unique<TestEffect>(4.0F, order, 3));
    auto buffer = CreateBuffer();

    ASSERT_TRUE(chain.SetOrder({third, first, second}));
    ASSERT_TRUE(chain.Process(buffer));

    EXPECT_EQ(order, (std::vector<int>{3, 1, 2}));
    EXPECT_FLOAT_EQ(buffer.Data()[0], 24.0F);
    EXPECT_FLOAT_EQ(buffer.Data()[3], 96.0F);
}

TEST(AudioEffectChainTests, RejectsSetOrderWithWrongSize)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto first = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto second = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));

    EXPECT_FALSE(chain.SetOrder({first}));
    EXPECT_FALSE(chain.SetOrder({first, second, first}));
}

TEST(AudioEffectChainTests, RejectsSetOrderWithMissingEffectId)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto first = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto second = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));

    EXPECT_FALSE(chain.SetOrder({first, static_cast<decltype(first)>(999)}));
    EXPECT_FALSE(chain.SetOrder({first, second, static_cast<decltype(first)>(999)}));
}

TEST(AudioEffectChainTests, RejectsSetOrderWithDuplicateEffectId)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto first = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto second = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));

    EXPECT_FALSE(chain.SetOrder({first, first}));
    EXPECT_FALSE(chain.SetOrder({first, second, second}));
}

TEST(AudioEffectChainTests, SetOrderDoesNotChangeActiveStateOrBypassState)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto first = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto second = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    chain.SetBypassed(first, true);
    chain.SetActive(second, false);

    ASSERT_TRUE(chain.SetOrder({second, first}));
    auto buffer = CreateBuffer();

    ASSERT_TRUE(chain.Process(buffer));
    EXPECT_TRUE(order.empty());
    EXPECT_FLOAT_EQ(buffer.Data()[0], 1.0F);
    EXPECT_FLOAT_EQ(buffer.Data()[3], 4.0F);
}

TEST(AudioEffectChainTests, SetOrderAcceptsEveryPermutationOfFourEffects)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto first = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto second = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    auto third = chain.Add(std::make_unique<TestEffect>(4.0F, order, 3));
    auto fourth = chain.Add(std::make_unique<TestEffect>(5.0F, order, 4));

    std::map<decltype(first), int> valueById{{first, 1}, {second, 2}, {third, 3}, {fourth, 4}};
    std::vector<decltype(first)> ids{first, second, third, fourth};

    do {
        order.clear();
        ASSERT_TRUE(chain.SetOrder(ids));

        auto buffer = CreateBuffer();
        ASSERT_TRUE(chain.Process(buffer));

        std::vector<int> expectedOrder;
        expectedOrder.reserve(ids.size());
        for (const auto id : ids) {
            expectedOrder.push_back(valueById[id]);
        }
        EXPECT_EQ(order, expectedOrder);
    } while (std::next_permutation(ids.begin(), ids.end()));
}

TEST(AudioEffectChainTests, RejectsStaleIdsAfterRemovalAndReadd)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto first = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto second = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    auto third = chain.Add(std::make_unique<TestEffect>(4.0F, order, 3));

    ASSERT_TRUE(chain.Remove(second));
    auto replacement = chain.Add(std::make_unique<TestEffect>(5.0F, order, 9));

    EXPECT_FALSE(chain.SetOrder({first, second, replacement}));
    EXPECT_FALSE(chain.SetOrder({replacement, first, third, second}));
    EXPECT_TRUE(chain.SetOrder({first, replacement, third}));
    EXPECT_TRUE(chain.SetOrder({replacement, first, third}));
}

TEST(AudioEffectChainTests, RejectsEmptyAndOutOfRangeOrderRequests)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto first = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto second = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    auto third = chain.Add(std::make_unique<TestEffect>(4.0F, order, 3));

    EXPECT_FALSE(chain.SetOrder({}));
    EXPECT_FALSE(chain.SetOrder({first}));
    EXPECT_FALSE(chain.SetOrder({first, second, third, first}));
    EXPECT_FALSE(chain.SetOrder({first, std::numeric_limits<decltype(first)>::max(), third}));
}

TEST(AudioEffectChainTests, RepeatedSetOrderCallsDoNotCorruptChain)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto first = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    auto second = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    auto third = chain.Add(std::make_unique<TestEffect>(4.0F, order, 3));

    ASSERT_TRUE(chain.SetOrder({third, first, second}));
    ASSERT_TRUE(chain.SetOrder({second, third, first}));
    ASSERT_TRUE(chain.SetOrder({first, second, third}));

    auto buffer = CreateBuffer();
    ASSERT_TRUE(chain.Process(buffer));
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(AudioEffectChainTests, StopsWhenAnEffectFails)
{
    std::vector<int> order;
    LuAudio::Audio::AudioEffectChain chain;
    auto failing = std::make_unique<TestEffect>(2.0F, order, 1);
    failing->Fail();
    chain.Add(std::move(failing));
    chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    auto buffer = CreateBuffer();

    EXPECT_FALSE(chain.Process(buffer));
    EXPECT_EQ(order, (std::vector<int>{1}));
}

TEST(AudioEffectChainTests, ResetsEveryEffect)
{
    std::vector<int> order;
    auto first = std::make_unique<TestEffect>(1.0F, order, 1);
    auto second = std::make_unique<TestEffect>(1.0F, order, 2);
    TestEffect* firstPointer = first.get();
    TestEffect* secondPointer = second.get();
    LuAudio::Audio::AudioEffectChain chain;
    chain.Add(std::move(first));
    chain.Add(std::move(second));

    chain.Reset();

    EXPECT_EQ(firstPointer->ResetCount(), 1);
    EXPECT_EQ(secondPointer->ResetCount(), 1);
}

}