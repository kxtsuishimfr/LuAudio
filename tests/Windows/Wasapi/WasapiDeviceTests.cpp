#include <gtest/gtest.h>

#include <LuAudio/LuAudio.h>

TEST(WasapiDeviceTests, StartsWithDefaultConfiguration)
{
    LuAudio::Providers::Windows::Wasapi::WasapiDevice device;

    const auto config = device.ActualConfig();

    EXPECT_EQ(config.format.sampleRate, 48000U);
    EXPECT_EQ(config.format.channelCount, 2U);
    EXPECT_EQ(config.framesPerBuffer, 512U);
}

TEST(WasapiDeviceTests, InvalidConfigurationDoesNotInitializeDevice)
{
    LuAudio::Providers::Windows::Wasapi::WasapiDevice device;
    LuAudio::Audio::AudioStreamConfig config;
    config.framesPerBuffer = 0;

    const auto result = device.Open(config);

    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Code(), LuAudio::Audio::ResultCode::InvalidArgument);
}

TEST(WasapiDeviceTests, StopBeforeOpenIsSafe)
{
    LuAudio::Providers::Windows::Wasapi::WasapiDevice device;

    EXPECT_TRUE(device.Stop().Succeeded());
}
