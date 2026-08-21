#include <gtest/gtest.h>

#include <stdexcept>
#include <string_view>

#include <LuAudio/Plugins/SDK/PluginTrampoline.h>

namespace {

using namespace LuAudio::Plugins;
using LuAudio::Plugins::SDK::PluginBase;
using LuAudio::Plugins::SDK::PluginTrampoline;

const PluginParameterDescriptor GainParameters[]{{
    "gain", "Gain", PluginParameterType::Float, 1.0F, 0.0F, 4.0F, 0.01F,
    "", "", 0, nullptr}};
const PluginParameterDescriptor EmptyParameters[1]{};

class GainPlugin final : public PluginBase {
public:
    bool Init(const PluginInstanceConfig& config) override
    {
        sample_rate_ = config.sample_rate;
        return config.channel_count == 2;
    }

    bool Process(PluginAudioBuffer& buffer) override
    {
        for (std::uint32_t index = 0;
             index < buffer.frame_count * buffer.channel_count;
             ++index) {
            buffer.data[index] *= gain_;
        }
        return true;
    }

    void Reset() override { gain_ = 1.0F; }

    bool SetParameter(const char* name, float value) override
    {
        if (std::string_view(name) != "gain") {
            return false;
        }
        gain_ = value;
        return true;
    }

    bool GetParameter(const char* name, float& value) override
    {
        if (std::string_view(name) != "gain") {
            return false;
        }
        value = gain_;
        return true;
    }

private:
    float gain_ = 1.0F;
    std::uint32_t sample_rate_ = 0;
};

class ThrowingPlugin final : public PluginBase {
public:
    bool Init(const PluginInstanceConfig&) override
    {
        throw std::runtime_error("init failure");
    }

    bool Process(PluginAudioBuffer&) override
    {
        throw std::runtime_error("process failure");
    }

    bool SetParameter(const char*, float) override
    {
        throw std::runtime_error("set failure");
    }

    bool GetParameter(const char*, float&) override
    {
        throw std::runtime_error("get failure");
    }

    void Reset() override
    {
        throw std::runtime_error("reset failure");
    }
};

TEST(PluginTrampolineTests, CreatesProcessesParametersAndResets)
{
    const auto* descriptor = PluginTrampoline<GainPlugin>::Descriptor(
        "Gain", "com.example.gain", "1.0.0", nullptr, nullptr, 1, GainParameters);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->abi_version, PluginAbiVersion);
    EXPECT_STREQ(descriptor->name, "Gain");
    ASSERT_NE(descriptor->create, nullptr);
    ASSERT_NE(descriptor->destroy, nullptr);
    ASSERT_NE(descriptor->process, nullptr);
    ASSERT_NE(descriptor->set_parameter, nullptr);
    ASSERT_NE(descriptor->get_parameter, nullptr);
    ASSERT_NE(descriptor->reset, nullptr);

    const PluginInstanceConfig config{48000, 2, 256};
    void* instance = descriptor->create(&config);
    ASSERT_NE(instance, nullptr);

    float samples[] = {1.0F, 2.0F, 3.0F, 4.0F};
    PluginAudioBuffer buffer{samples, 2, 2, 48000};
    ASSERT_TRUE(descriptor->set_parameter(instance, "gain", 3.0F));
    ASSERT_TRUE(descriptor->process(instance, &buffer));
    EXPECT_FLOAT_EQ(samples[0], 3.0F);
    EXPECT_FLOAT_EQ(samples[3], 12.0F);

    float gain = 0.0F;
    ASSERT_TRUE(descriptor->get_parameter(instance, "gain", &gain));
    EXPECT_FLOAT_EQ(gain, 3.0F);

    descriptor->reset(instance);
    ASSERT_TRUE(descriptor->get_parameter(instance, "gain", &gain));
    EXPECT_FLOAT_EQ(gain, 1.0F);
    descriptor->destroy(instance);
}

TEST(PluginTrampolineTests, RejectsFailedInitialization)
{
    const auto* descriptor = PluginTrampoline<GainPlugin>::Descriptor(
        "Gain", "com.example.gain", "1.0.0", nullptr, nullptr, 1, GainParameters);
    const PluginInstanceConfig invalid_config{48000, 1, 256};

    EXPECT_EQ(descriptor->create(&invalid_config), nullptr);
}

TEST(PluginTrampolineTests, ConvertsExceptionsAtTheAbiBoundary)
{
    const auto* descriptor = PluginTrampoline<ThrowingPlugin>::Descriptor(
        "Throwing", "com.example.throwing", "1.0.0", nullptr, nullptr, 0,
        EmptyParameters);
    const PluginInstanceConfig config{48000, 2, 256};
    EXPECT_EQ(descriptor->create(&config), nullptr);

    auto* instance = new ThrowingPlugin();
    PluginAudioBuffer buffer{nullptr, 0, 0, 0};
    float value = 0.0F;
    EXPECT_FALSE(descriptor->process(instance, &buffer));
    EXPECT_FALSE(descriptor->set_parameter(instance, "value", 1.0F));
    EXPECT_FALSE(descriptor->get_parameter(instance, "value", &value));
    EXPECT_NO_THROW(descriptor->reset(instance));
    descriptor->destroy(instance);
}

TEST(PluginTrampolineTests, RejectsNullAbiInputs)
{
    const auto* descriptor = PluginTrampoline<GainPlugin>::Descriptor(
        "Gain", "com.example.gain", "1.0.0", nullptr, nullptr, 1, GainParameters);
    const PluginInstanceConfig config{48000, 2, 256};
    void* instance = descriptor->create(&config);
    ASSERT_NE(instance, nullptr);

    float value = 0.0F;
    EXPECT_EQ(descriptor->create(nullptr), nullptr);
    EXPECT_FALSE(descriptor->process(nullptr, nullptr));
    EXPECT_FALSE(descriptor->process(instance, nullptr));
    EXPECT_FALSE(descriptor->set_parameter(nullptr, "gain", 1.0F));
    EXPECT_FALSE(descriptor->set_parameter(instance, nullptr, 1.0F));
    EXPECT_FALSE(descriptor->get_parameter(nullptr, "gain", &value));
    EXPECT_FALSE(descriptor->get_parameter(instance, nullptr, &value));
    EXPECT_FALSE(descriptor->get_parameter(instance, "gain", nullptr));
    EXPECT_NO_THROW(descriptor->reset(nullptr));
    descriptor->destroy(instance);
}

}