#include <gtest/gtest.h>

#include <LuAudio/LuAudio.h>

namespace {

LuAudio::Audio::AudioStreamConfig ValidConfig()
{
    return {};
}

}

TEST(WasapiBackendTests, InvalidConfig)
{
    LuAudio::Providers::Windows::Wasapi::WasapiBackend backend;
    auto config = ValidConfig();
    config.format.sampleRate = 0;

    const auto result = backend.Open(config);

    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Code(), LuAudio::Audio::ResultCode::InvalidArgument);
}

TEST(WasapiBackendTests, StartBeforeOpen)
{
    LuAudio::Providers::Windows::Wasapi::WasapiBackend backend;

    const auto result = backend.Start();

    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Code(), LuAudio::Audio::ResultCode::InvalidState);
}

TEST(WasapiBackendTests, StopBeforeOpen)
{
    LuAudio::Providers::Windows::Wasapi::WasapiBackend backend;

    const auto result = backend.Stop();

    EXPECT_TRUE(result.Succeeded());
}

TEST(WasapiBackendTests, CloseIsIdempotent)
{
    LuAudio::Providers::Windows::Wasapi::WasapiBackend backend;

    backend.Close();
    backend.Close();

    SUCCEED();
}
