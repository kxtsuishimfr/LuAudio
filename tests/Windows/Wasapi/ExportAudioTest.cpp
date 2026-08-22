#include <conio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <LuAudio/LuAudio.h>

namespace {

using namespace LuAudio;
using namespace LuAudio::Audio;

Result OpenAudio(const std::filesystem::path& path, std::unique_ptr<IAudioReader>& reader)
{
    if (path.extension() == ".wav" || path.extension() == ".WAV") {
        auto wavReader = std::make_unique<WavFileReader>();
        const auto result = wavReader->Open(AudioFile(path.string(), AudioFileType::Wav));
        if (result.Succeeded()) {
            reader = std::move(wavReader);
        }
        return result;
    }

    auto mp3Reader = std::make_unique<Mp3FileReader>(
        std::make_unique<Providers::Windows::WAudioDecoder>());
    const auto result = mp3Reader->Open(AudioFile(path.string(), AudioFileType::Mp3));
    if (result.Succeeded()) {
        reader = std::move(mp3Reader);
    }
    return result;
}

Result CreateHallChain(
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
        return Result::Failure(ResultCode::ProcessingFailed, "Hall reverb plugin failed to initialize");
    }
    if (!effect->SetParameter("room_size", 0.92F) ||
        !effect->SetParameter("damping", 0.28F) ||
        !effect->SetParameter("wet", 0.31F) ||
        !effect->SetParameter("width", 0.9F)) {
        return Result::Failure(ResultCode::ProcessingFailed, "Unable to configure hall reverb plugin");
    }
    chain.Add(std::move(effect));
    return Result::Success();
}

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
    const auto wavPath = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("tests/Audios/sample_1.wav");
    const auto mp3Path = argc > 2
        ? std::filesystem::path(argv[2])
        : std::filesystem::path("tests/Audios/sample_2.mp3");
    const auto sample3Path = argc > 3
        ? std::filesystem::path(argv[3])
        : std::filesystem::path("tests/Audios/sample_3.mp3");
    const auto outputDirectory = argc > 4
        ? std::filesystem::path(argv[4])
        : std::filesystem::path("tests/Audios/Output");
#if defined(LUAUDIO_HALL_REVERB_PLUGIN_PATH)
    const std::filesystem::path hallPluginPath = LUAUDIO_HALL_REVERB_PLUGIN_PATH;
#else
    std::cerr << "Hall reverb plugin path was not configured\n";
    return 1;
#endif
#if defined(LUAUDIO_BITCRUSHER_PLUGIN_PATH)
    const std::filesystem::path bitcrusherPluginPath = LUAUDIO_BITCRUSHER_PLUGIN_PATH;
#else
    std::cerr << "Bitcrusher plugin path was not configured\n";
    return 1;
#endif

    std::unique_ptr<IAudioReader> wavReader;
    std::unique_ptr<IAudioReader> mp3Reader;
    std::unique_ptr<IAudioReader> sample3Reader;
    const auto wavOpen = OpenAudio(wavPath, wavReader);
    const auto mp3Open = OpenAudio(mp3Path, mp3Reader);
    const auto sample3Open = OpenAudio(sample3Path, sample3Reader);
    if (!wavOpen.Succeeded() || !mp3Open.Succeeded() || !sample3Open.Succeeded()) {
        PrintResult("WAV open", wavOpen);
        PrintResult("MP3 open", mp3Open);
        PrintResult("Sample 3 open", sample3Open);
        return 1;
    }
    if (!wavReader->Format().CanMixInto(wavReader->Format()) ||
        !mp3Reader->Format().CanMixInto(wavReader->Format()) ||
        !sample3Reader->Format().CanMixInto(wavReader->Format())) {
        std::cerr << "The three sources must have compatible mixer formats\n";
        return 1;
    }
    const auto sampleRate = wavReader->Format().sampleRate;

    auto bitcrusherEffects = std::make_shared<AudioEffectChain>();
    auto hallEffects = std::make_shared<AudioEffectChain>();
    const auto bitcrusherResult = CreateBitcrusherChain(
        bitcrusherPluginPath, wavReader->Format(), *bitcrusherEffects);
    const auto hallResult = CreateHallChain(
        hallPluginPath, sample3Reader->Format(), *hallEffects);
    if (!bitcrusherResult.Succeeded() || !hallResult.Succeeded()) {
        PrintResult("Bitcrusher setup", bitcrusherResult);
        PrintResult("Hall reverb setup", hallResult);
        return 1;
    }

    Providers::Windows::Wasapi::WasapiBackend backend;
    AudioMixer mixer(backend, 3);
    const auto mixerOpen = mixer.Open(AudioStreamConfig{wavReader->Format(), 512, false});
    if (!mixerOpen.Succeeded()) {
        PrintResult("WASAPI mixer open", mixerOpen);
        return 1;
    }
    AudioMixer::SourceId wavId = 0;
    AudioMixer::SourceId mp3Id = 0;
    AudioMixer::SourceId sample3Id = 0;
    const auto addWavResult = mixer.AddSource(std::move(wavReader), wavId);
    const auto addMp3Result = mixer.AddSource(std::move(mp3Reader), mp3Id);
    const auto addSample3Result = mixer.AddSource(std::move(sample3Reader), sample3Id);
    if (!addWavResult.Succeeded() || !addMp3Result.Succeeded() ||
        !addSample3Result.Succeeded()) {
        PrintResult("WAV source add", addWavResult);
        PrintResult("MP3 source add", addMp3Result);
        PrintResult("Sample 3 source add", addSample3Result);
        mixer.Close();
        return 1;
    }
    PrintResult("Bitcrusher source setup", mixer.SetSourceEffects(wavId, bitcrusherEffects));
    PrintResult("Hall reverb source setup", mixer.SetSourceEffects(sample3Id, hallEffects));
    const auto startResult = mixer.Start();
    if (!startResult.Succeeded()) {
        PrintResult("WASAPI mixer start", startResult);
        mixer.Close();
        return 1;
    }

    std::filesystem::create_directories(outputDirectory);
    const auto outputPath = outputDirectory / "sample_1_bitcrusher_plus_sample_2_plus_sample_3_hall.wav";
    std::atomic<bool> exportRunning = false;
    std::thread exportThread;

    const auto joinExport = [&] {
        if (exportThread.joinable()) {
            std::cout << "Waiting for export thread to finish...\n";
            exportThread.join();
            std::cout << "Export thread finished.\n";
        }
    };

    std::cout << "Playing " << wavPath << ", " << mp3Path << " and " << sample3Path
              << " through WASAPI.\n"
              << "Space: pause/resume all sources\n"
              << "Left/Right: seek all sources by 5 seconds\n"
              << "R: export processed mix to " << outputPath << "\n"
              << "Q: quit\n";

    bool quit = false;
    bool paused = false;
    while (!quit && mixer.ActiveSourceCount() != 0 &&
        !(mixer.IsSourceFinished(wavId) && mixer.IsSourceFinished(mp3Id) &&
            mixer.IsSourceFinished(sample3Id))) {
        if (!_kbhit()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }
        const int key = _getch();
        if (key == 'q' || key == 'Q') {
            quit = true;
        } else if (key == ' ') {
            paused = !paused;
            PrintResult(paused ? "Pause all sources" : "Resume all sources",
                mixer.SetSourcePaused(wavId, paused));
            PrintResult(paused ? "Pause sample 2" : "Resume sample 2",
                mixer.SetSourcePaused(mp3Id, paused));
            PrintResult(paused ? "Pause sample 3" : "Resume sample 3",
                mixer.SetSourcePaused(sample3Id, paused));
        } else if (key == 0 || key == 0xE0) {
            const int arrow = _getch();
            if (arrow == 75 || arrow == 77) {
                const auto seconds = static_cast<std::int64_t>(sampleRate) * 5;
                const auto seek = arrow == 75 ? -seconds : seconds;
                PrintResult("Seek WAV", mixer.SeekSourceRelative(wavId, seek));
                PrintResult("Seek MP3", mixer.SeekSourceRelative(mp3Id, seek));
                PrintResult("Seek sample 3", mixer.SeekSourceRelative(sample3Id, seek));
            }
        } else if (key == 'r' || key == 'R') {
            if (exportRunning.exchange(true)) {
                std::cout << "Export is already running.\n";
                continue;
            }
            joinExport();
            exportThread = std::thread([&, outputPath] {
                std::unique_ptr<IAudioReader> exportWav;
                std::unique_ptr<IAudioReader> exportMp3;
                std::unique_ptr<IAudioReader> exportSample3;
                const auto exportWavResult = OpenAudio(wavPath, exportWav);
                const auto exportMp3Result = OpenAudio(mp3Path, exportMp3);
                const auto exportSample3Result = OpenAudio(sample3Path, exportSample3);
                if (!exportWavResult.Succeeded() || !exportMp3Result.Succeeded() ||
                    !exportSample3Result.Succeeded()) {
                    PrintResult("Export WAV open", exportWavResult);
                    PrintResult("Export MP3 open", exportMp3Result);
                    PrintResult("Export sample 3 open", exportSample3Result);
                    exportRunning = false;
                    return;
                }
                auto exportBitcrusherEffects = std::make_shared<AudioEffectChain>();
                auto exportHallEffects = std::make_shared<AudioEffectChain>();
                const auto exportBitcrusherResult = CreateBitcrusherChain(
                    bitcrusherPluginPath, exportWav->Format(), *exportBitcrusherEffects);
                const auto exportHallResult = CreateHallChain(
                    hallPluginPath, exportSample3->Format(), *exportHallEffects);
                if (!exportBitcrusherResult.Succeeded() || !exportHallResult.Succeeded()) {
                    PrintResult("Export bitcrusher setup", exportBitcrusherResult);
                    PrintResult("Export hall reverb setup", exportHallResult);
                    exportRunning = false;
                    return;
                }
                const auto exportFormat = exportWav->Format();
                std::vector<OfflineRenderer::Source> sources;
                sources.reserve(3);
                OfflineRenderer::Source wavSource;
                wavSource.reader = std::move(exportWav);
                wavSource.effects = exportBitcrusherEffects;
                sources.push_back(std::move(wavSource));
                OfflineRenderer::Source mp3Source;
                mp3Source.reader = std::move(exportMp3);
                sources.push_back(std::move(mp3Source));
                OfflineRenderer::Source sample3Source;
                sample3Source.reader = std::move(exportSample3);
                sample3Source.effects = exportHallEffects;
                sources.push_back(std::move(sample3Source));
                WavFileWriter writer(outputPath.string());
                const auto renderResult = OfflineRenderer::RenderSources(
                    std::move(sources), exportFormat, writer, nullptr, 4096);
                PrintResult("Offline export", renderResult);
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
