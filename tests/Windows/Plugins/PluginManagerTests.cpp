#include <gtest/gtest.h>

#include <memory>

#include <LuAudio/Plugins/PluginManager.h>
#include <LuAudio/Plugins/PluginEffectAdapter.h>
#include <LuAudio/Audio/Processing/AudioEffectChain.h>
#include <LuAudio/Providers/Windows/WPluginProvider.h>

namespace {

using namespace LuAudio::Plugins;

struct TestInstance {
    float gain = 1.0F;
};

void* CreateInstance(const PluginInstanceConfig*)
{
    return new TestInstance();
}

void DestroyInstance(void* instance)
{
    delete static_cast<TestInstance*>(instance);
}

bool ProcessInstance(void* instance, PluginAudioBuffer* buffer)
{
    const auto gain = static_cast<TestInstance*>(instance)->gain;
    for (std::uint32_t index = 0; index < buffer->frame_count * buffer->channel_count; ++index) {
        buffer->data[index] *= gain;
    }
    return true;
}

const PluginDescriptor ValidDescriptor{
    PluginAbiVersion,
    "TestPlugin",
    &CreateInstance,
    &DestroyInstance,
    &ProcessInstance,
    nullptr,
    nullptr,
    nullptr};

const PluginDescriptor* descriptorToReturn = nullptr;

const PluginDescriptor* GetDescriptor()
{
    return descriptorToReturn;
}

class FakeLibrary final : public LuAudio::Providers::IPluginLibrary {
public:
    explicit FakeLibrary(const PluginDescriptor* descriptor)
        : descriptor_(descriptor)
    {
        descriptorToReturn = descriptor_;
    }

    void* Resolve(std::string_view name) noexcept override
    {
        if (name != PluginDescriptorSymbol) {
            return nullptr;
        }
        return reinterpret_cast<void*>(&GetDescriptor);
    }

private:
    const PluginDescriptor* descriptor_;
};

class FakeProvider final : public LuAudio::Providers::IPluginProvider {
public:
    explicit FakeProvider(const PluginDescriptor* descriptor)
        : descriptor_(descriptor)
    {
    }

    LuAudio::Audio::Result Open(
        const std::filesystem::path&,
        std::unique_ptr<LuAudio::Providers::IPluginLibrary>& library) override
    {
        library = std::make_unique<FakeLibrary>(descriptor_);
        return LuAudio::Audio::Result::Success();
    }

private:
    const PluginDescriptor* descriptor_;
};

TEST(PluginManagerTests, RejectsInvalidAbiVersion)
{
    auto descriptor = ValidDescriptor;
    descriptor.abi_version = PluginAbiVersion + 1;
    FakeProvider provider(&descriptor);
    PluginManager manager(provider);
    std::unique_ptr<PluginHandle> plugin;

    const auto result = manager.Load("invalid.dll", plugin);

    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Code(), LuAudio::Audio::ResultCode::InvalidArgument);
    EXPECT_EQ(plugin, nullptr);
}

TEST(PluginManagerTests, LoadsAndProcessesPlugin)
{
    FakeProvider provider(&ValidDescriptor);
    PluginManager manager(provider);
    std::unique_ptr<PluginHandle> plugin;
    const auto loadResult = manager.Load("valid.dll", plugin);

    ASSERT_TRUE(loadResult.Succeeded());
    ASSERT_NE(plugin, nullptr);
    EXPECT_STREQ(plugin->Name(), "TestPlugin");
    ASSERT_TRUE(plugin->Create({48000, 2, 256}).Succeeded());

    float samples[] = {1.0F, 2.0F, 3.0F, 4.0F};
    PluginAudioBuffer buffer{samples, 2, 2, 48000};

    ASSERT_TRUE(plugin->Process(buffer));
    EXPECT_FLOAT_EQ(samples[0], 1.0F);
    EXPECT_FLOAT_EQ(samples[3], 4.0F);
}

TEST(PluginManagerTests, RejectsMissingDescriptor)
{
    class MissingDescriptorLibrary final : public LuAudio::Providers::IPluginLibrary {
        void* Resolve(std::string_view) noexcept override { return nullptr; }
    };

    class MissingDescriptorProvider final : public LuAudio::Providers::IPluginProvider {
        LuAudio::Audio::Result Open(
            const std::filesystem::path&,
            std::unique_ptr<LuAudio::Providers::IPluginLibrary>& library) override
        {
            library = std::make_unique<MissingDescriptorLibrary>();
            return LuAudio::Audio::Result::Success();
        }
    } provider;

    PluginManager manager(provider);
    std::unique_ptr<PluginHandle> plugin;

    const auto result = manager.Load("missing.dll", plugin);

    EXPECT_FALSE(result.Succeeded());
    EXPECT_EQ(result.Code(), LuAudio::Audio::ResultCode::InvalidArgument);
    EXPECT_EQ(plugin, nullptr);
}

#if defined(LUAUDIO_TEST_PLUGIN_PATH)
TEST(WindowsPluginProviderTests, LoadsFixtureAndResolvesDescriptor)
{
    LuAudio::Providers::Windows::WPluginProvider provider;
    std::unique_ptr<LuAudio::Providers::IPluginLibrary> library;

    const auto result = provider.Open(LUAUDIO_TEST_PLUGIN_PATH, library);

    ASSERT_TRUE(result.Succeeded());
    ASSERT_NE(library, nullptr);
    EXPECT_NE(library->Resolve(PluginDescriptorSymbol), nullptr);
}

TEST(PluginIntegrationTests, ProcessesGainThroughAdapterAndChain)
{
    LuAudio::Providers::Windows::WPluginProvider provider;
    PluginManager manager(provider);
    std::unique_ptr<PluginHandle> plugin;
    ASSERT_TRUE(manager.Load(LUAUDIO_TEST_PLUGIN_PATH, plugin).Succeeded());

    auto adapter = std::make_unique<PluginEffectAdapter>(
        std::move(plugin),
        PluginInstanceConfig{48000, 2, 256});
    ASSERT_TRUE(adapter->IsReady());
    ASSERT_TRUE(adapter->SetParameter("gain", 3.0F));

    LuAudio::Audio::AudioEffectChain chain;
    chain.Add(std::move(adapter));
    LuAudio::Audio::AudioBuffer buffer({}, 2);
    buffer.Data()[0] = 1.0F;
    buffer.Data()[1] = 2.0F;
    buffer.Data()[2] = 3.0F;
    buffer.Data()[3] = 4.0F;

    ASSERT_TRUE(chain.Process(buffer));
    EXPECT_FLOAT_EQ(buffer.Data()[0], 3.0F);
    EXPECT_FLOAT_EQ(buffer.Data()[3], 12.0F);
}
#endif

}