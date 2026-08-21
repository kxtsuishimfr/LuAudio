#include <conio.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <LuAudio/LuAudio.h>
#include <LuAudio/Audio/Processing/AudioEffectChain.h>

namespace {

using namespace LuAudio;
using namespace LuAudio::Audio;

const std::filesystem::path DefaultAudioPath = "tests/Audios/sample_3.mp3";

Result OpenAudio(const std::filesystem::path& path,
    std::unique_ptr<IAudioReader>& reader)
{
    auto mp3Reader = std::make_unique<Mp3FileReader>(
        std::make_unique<Providers::Windows::WAudioDecoder>());
    const auto result = mp3Reader->Open(AudioFile(path.string(), AudioFileType::Mp3));
    if (result.Succeeded()) {
        reader = std::move(mp3Reader);
    }
    return result;
}

Result CreateBitcrusherChain(const std::filesystem::path& plugin_path,
    const AudioFormat& format, AudioEffectChain& chain,
    Plugins::PluginEffectAdapter*& effect_pointer)
{
    Providers::Windows::WPluginProvider provider;
    Plugins::PluginManager manager(provider);
    std::unique_ptr<Plugins::PluginHandle> plugin;
    const auto load_result = manager.Load(plugin_path, plugin);
    if (!load_result.Succeeded()) {
        return load_result;
    }

    auto effect = std::make_unique<Plugins::PluginEffectAdapter>(
        std::move(plugin),
        Plugins::PluginInstanceConfig{format.sampleRate, format.channelCount, 4096});
    if (!effect->IsReady()) {
        return Result::Failure(ResultCode::ProcessingFailed,
            "Bitcrusher plugin failed to initialize");
    }
    effect_pointer = effect.get();
    effect->SetParameter("bits", 6.0F);
    effect->SetParameter("hold", 4.0F);
    effect->SetParameter("drive", 1.35F);
    chain.Add(std::move(effect));
    return Result::Success();
}

void PrintParameter(Plugins::PluginEffectAdapter& effect)
{
    float bits = 0.0F;
    float hold = 0.0F;
    effect.GetParameter("bits", bits);
    effect.GetParameter("hold", hold);
    std::cout << "Bitcrusher: " << bits << " bits, hold " << hold << " frames\n";
}

}

int main(int argc, char* argv[])
{
    const auto audio_path = argc > 1 ? std::filesystem::path(argv[1]) : DefaultAudioPath;
#if defined(LUAUDIO_BITCRUSHER_PLUGIN_PATH)
    const std::filesystem::path plugin_path = LUAUDIO_BITCRUSHER_PLUGIN_PATH;
#else
    std::cerr << "Bitcrusher plugin path was not configured\n";
    return 1;
#endif

    std::unique_ptr<IAudioReader> reader;
    const auto open_result = OpenAudio(audio_path, reader);
    if (!open_result.Succeeded()) {
        std::cerr << "MP3 open failed: " << open_result.Message() << '\n';
        return 1;
    }

    AudioEffectChain effects;
    Plugins::PluginEffectAdapter* effect = nullptr;
    const auto effect_result = CreateBitcrusherChain(
        plugin_path, reader->Format(), effects, effect);
    if (!effect_result.Succeeded()) {
        std::cerr << "Bitcrusher setup failed: " << effect_result.Message() << '\n';
        return 1;
    }
    Providers::Windows::Wasapi::WasapiBackend backend;
    AudioPlayer player(backend);
    player.SetEffectChain(&effects);
    AudioStreamConfig config;
    config.format = reader->Format();
    const auto player_open = player.Open(std::move(reader), config);
    if (!player_open.Succeeded()) {
        std::cerr << "Audio player open failed: " << player_open.Message() << '\n';
        return 1;
    }
    const auto start_result = player.Start();
    if (!start_result.Succeeded()) {
        std::cerr << "WASAPI start failed: " << start_result.Message() << '\n';
        return 1;
    }

    std::cout << "Playing " << audio_path << " through WASAPI with Bitcrusher.\n"
              << "Up/Down: bit depth, Left/Right: sample hold, D/F: drive, Space: bypass\n"
              << "P: pause/resume, R: rewind, Q/Escape: stop and exit\n";
    PrintParameter(*effect);

    bool bypassed = false;
    while (!player.EndOfFile()) {
        if (_kbhit()) {
            const int key = _getch();
            if (key == 'q' || key == 'Q' || key == 27) {
                break;
            }
            if (key == 0 || key == 0xE0) {
                const int arrow = _getch();
                float value = 0.0F;
                if (arrow == 72 || arrow == 80) {
                    effect->GetParameter("bits", value);
                    effect->SetParameter("bits", value + (arrow == 72 ? 1.0F : -1.0F));
                    PrintParameter(*effect);
                } else if (arrow == 75 || arrow == 77) {
                    effect->GetParameter("hold", value);
                    effect->SetParameter("hold", value + (arrow == 77 ? 1.0F : -1.0F));
                    PrintParameter(*effect);
                }
                continue;
            }
            if (key == ' ') {
                bypassed = !bypassed;
                effects.SetBypassed(0, bypassed);
                std::cout << "Bitcrusher: " << (bypassed ? "bypassed\n" : "active\n");
            } else if (key == 'p' || key == 'P') {
                const auto result = player.IsPaused() ? player.Resume() : player.Pause();
                if (!result.Succeeded()) {
                    std::cerr << "Pause/resume failed: " << result.Message() << '\n';
                }
            } else if (key == 'r' || key == 'R') {
                player.Rewind();
                effects.Reset();
                std::cout << "Rewound\n";
            } else if (key == 'd' || key == 'D' || key == 'f' || key == 'F') {
                float value = 0.0F;
                effect->GetParameter("drive", value);
                effect->SetParameter("drive", value + (key == 'F' ? 0.25F : -0.25F));
                PrintParameter(*effect);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    player.Stop();
    player.Close();
    return 0;
}