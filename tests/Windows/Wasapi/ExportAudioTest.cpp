#include <conio.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <LuAudio/LuAudio.h>

namespace {

using namespace LuAudio;
using namespace LuAudio::Audio;

Result CreateBitcrusherChain(
    const std::filesystem::path& pluginPath,
    const AudioFormat& format,
    AudioEffectChain& chain)
{
    Providers::Windows::WPluginProvider provider;
    Plugins::PluginManager manager(provider);
    std::unique_ptr<Plugins::PluginHandle> plugin;
    const auto loadResult = manager.Load(pluginPath, plugin);
    if (!loadResult.Succeeded()) {
        return loadResult;
    }

    auto effect = std::make_unique<Plugins::PluginEffectAdapter>(
        std::move(plugin),
        Plugins::PluginInstanceConfig{format.sampleRate, format.channelCount, 4096});
    if (!effect->IsReady()) {
        return Result::Failure(ResultCode::ProcessingFailed,
            "Bitcrusher plugin failed to initialize");
    }
    if (!effect->SetParameter("bits", 6.0F) ||
        !effect->SetParameter("hold", 4.0F) ||
        !effect->SetParameter("drive", 1.35F)) {
        return Result::Failure(ResultCode::ProcessingFailed,
            "Unable to configure bitcrusher plugin");
    }
    chain.Add(std::move(effect));
    return Result::Success();
}

void PrintResult(const char* operation, const Result& result)
{
    if (!result.Succeeded()) {
        std::cerr << operation << " failed: " << result.Message() << '\n';
    }
}

}

int main(int argc, char* argv[])
{
    const auto inputPath = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("tests/Audios/sample_4.ogg");
    const auto outputDirectory = argc > 2
        ? std::filesystem::path(argv[2])
        : std::filesystem::path("tests/Audios/Output");
#if defined(LUAUDIO_BITCRUSHER_PLUGIN_PATH)
    const std::filesystem::path bitcrusherPluginPath = LUAUDIO_BITCRUSHER_PLUGIN_PATH;
#else
    std::cerr << "Bitcrusher plugin path was not configured\n";
    return 1;
#endif

    Providers::Windows::Wasapi::WasapiBackend backend;
    AudioMixer mixer(backend, 1);
    auto playbackReader = std::make_unique<OggFileReader>(nullptr);
    const auto playbackOpen = playbackReader->Open(
        AudioFile(inputPath.string(), AudioFileType::Ogg));
    if (!playbackOpen.Succeeded()) {
        PrintResult("Playback Ogg open", playbackOpen);
        mixer.Close();
        return 1;
    }
    const AudioFormat playbackFormat = playbackReader->Format();
    const auto mixerOpen = mixer.Open(AudioStreamConfig{playbackFormat, 512, false});
    if (!mixerOpen.Succeeded()) {
        PrintResult("WASAPI mixer open", mixerOpen);
        return 1;
    }
    AudioMixer::SourceId sourceId = 0;
    const auto addSourceResult = mixer.AddSource(std::move(playbackReader), sourceId);
    if (!addSourceResult.Succeeded()) {
        PrintResult("Ogg source add", addSourceResult);
        mixer.Close();
        return 1;
    }

    auto effects = std::make_shared<AudioEffectChain>();
    const auto effectsResult = CreateBitcrusherChain(
        bitcrusherPluginPath, playbackFormat, *effects);
    if (!effectsResult.Succeeded()) {
        PrintResult("Bitcrusher setup", effectsResult);
        mixer.Close();
        return 1;
    }
    PrintResult("Bitcrusher source setup", mixer.SetSourceEffects(sourceId, effects));

    const auto startResult = mixer.Start();
    if (!startResult.Succeeded()) {
        PrintResult("WASAPI mixer start", startResult);
        mixer.Close();
        return 1;
    }

    std::filesystem::create_directories(outputDirectory);
    const auto outputPath = outputDirectory / "sample_4_bitcrusher.ogg";
    std::atomic<bool> exportRunning = false;
    std::thread exportThread;
    const auto joinExport = [&] {
        if (exportThread.joinable()) {
            std::cout << "Waiting for export thread to finish...\n";
            exportThread.join();
            std::cout << "Export thread finished.\n";
        }
    };

    std::cout << "Playing " << inputPath << " through WASAPI.\n"
              << "Space: pause/resume source\n"
              << "Left/Right: seek source by 5 seconds\n"
              << "R: export processed Ogg to " << outputPath << "\n"
              << "Q: quit\n";

    bool quit = false;
    bool paused = false;
    while (!quit && mixer.ActiveSourceCount() != 0 && !mixer.IsSourceFinished(sourceId)) {
        if (!_kbhit()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }
        const int key = _getch();
        if (key == 'q' || key == 'Q') {
            quit = true;
        } else if (key == ' ') {
            paused = !paused;
            PrintResult(paused ? "Pause source" : "Resume source",
                mixer.SetSourcePaused(sourceId, paused));
        } else if (key == 0 || key == 0xE0) {
            const int arrow = _getch();
            if (arrow == 75 || arrow == 77) {
                const auto seconds = static_cast<std::int64_t>(playbackFormat.sampleRate) * 5;
                const auto seek = arrow == 75 ? -seconds : seconds;
                PrintResult("Seek Ogg", mixer.SeekSourceRelative(sourceId, seek));
            }
        } else if (key == 'r' || key == 'R') {
            if (exportRunning.exchange(true)) {
                std::cout << "Export is already running.\n";
                continue;
            }
            joinExport();
            exportThread = std::thread([&, outputPath] {
                auto exportReader = std::make_unique<OggFileReader>(nullptr);
                const auto exportOpen = exportReader->Open(
                    AudioFile(inputPath.string(), AudioFileType::Ogg));
                if (!exportOpen.Succeeded()) {
                    PrintResult("Export Ogg open", exportOpen);
                    exportRunning = false;
                    return;
                }
                auto exportEffects = std::make_shared<AudioEffectChain>();
                const auto exportEffectsResult = CreateBitcrusherChain(
                    bitcrusherPluginPath, exportReader->Format(), *exportEffects);
                if (!exportEffectsResult.Succeeded()) {
                    PrintResult("Export bitcrusher setup", exportEffectsResult);
                    exportRunning = false;
                    return;
                }
                OggFileWriter writer(outputPath.string());
                const auto renderResult = OfflineRenderer::Render(
                    *exportReader, writer, exportEffects.get(), 4096);
                PrintResult("Offline Ogg export", renderResult);
                if (renderResult.Succeeded()) {
                    std::cout << "Export finished: " << outputPath << '\n';
                }
                exportRunning = false;
            });
        }
    }

    joinExport();
    const auto stopResult = mixer.Stop();
    mixer.Close();
    if (!stopResult.Succeeded()) {
        PrintResult("WASAPI mixer stop", stopResult);
        return 1;
    }
    return 0;
}
