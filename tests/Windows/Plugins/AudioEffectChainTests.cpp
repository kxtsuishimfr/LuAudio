#include <gtest/gtest.h>

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
    const auto bypassed = chain.Add(std::make_unique<TestEffect>(2.0F, order, 1));
    const auto inactive = chain.Add(std::make_unique<TestEffect>(3.0F, order, 2));
    chain.SetBypassed(bypassed, true);
    chain.SetActive(inactive, false);
    auto buffer = CreateBuffer();

    ASSERT_TRUE(chain.Process(buffer));

    EXPECT_TRUE(order.empty());
    EXPECT_FLOAT_EQ(buffer.Data()[0], 1.0F);
    EXPECT_FLOAT_EQ(buffer.Data()[3], 4.0F);
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