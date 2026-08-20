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

std::filesystem::path ChooseInputPath()
{
    std::cout << "Choose audio for this session:\n"
              << "1: tests/Audios/sample_1.wav\n"
              << "2: tests/Audios/sample_2.mp3\n";
    while (true) {
        const int key = _getch();
        if (key == '1') {
            return "tests/Audios/sample_1.wav";
        }
        if (key == '2') {
            return "tests/Audios/sample_2.mp3";
        }
    }
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
        : ChooseInputPath();
    const auto outputDirectory = argc > 2
        ? std::filesystem::path(argv[2])
        : std::filesystem::path("tests/Audios/Output");
#if defined(LUAUDIO_HALL_REVERB_PLUGIN_PATH)
    const std::filesystem::path pluginPath = LUAUDIO_HALL_REVERB_PLUGIN_PATH;
#else
    std::cerr << "Hall reverb plugin path was not configured\n";
    return 1;
#endif

    std::unique_ptr<IAudioReader> previewReader;
    const auto previewOpen = OpenAudio(inputPath, previewReader);
    if (!previewOpen.Succeeded()) {
        PrintResult("Preview audio open", previewOpen);
        return 1;
    }
    const AudioFormat previewFormat = previewReader->Format();

    AudioEffectChain previewEffects;
    const auto previewEffectResult = CreateHallChain(pluginPath, previewReader->Format(), previewEffects);
    if (!previewEffectResult.Succeeded()) {
        PrintResult("Preview effect setup", previewEffectResult);
        return 1;
    }

    Providers::Windows::Wasapi::WasapiBackend backend;
    AudioPlayer player(backend);
    const auto playerOpen = player.Open(
        std::move(previewReader), AudioStreamConfig{previewFormat, 512, false});
    if (!playerOpen.Succeeded()) {
        PrintResult("WASAPI player open", playerOpen);
        return 1;
    }
    player.SetEffectChain(&previewEffects);
    const auto startResult = player.Start();
    if (!startResult.Succeeded()) {
        PrintResult("WASAPI start", startResult);
        player.Close();
        return 1;
    }

    std::filesystem::create_directories(outputDirectory);
    const auto outputPath = outputDirectory / (inputPath.stem().string() + "_hall_reverb.wav");
    std::atomic<bool> exportRunning = false;
    std::thread exportThread;

    const auto joinExport = [&] {
        if (exportThread.joinable()) {
            std::cout << "Waiting for export thread to finish...\n";
            exportThread.join();
            std::cout << "Export thread finished.\n";
        }
    };

    std::cout << "Playing " << inputPath << " through WASAPI with ProfessionalHallReverb.\n"
              << "Space: export to " << outputPath << "\n"
              << "P: pause/resume preview, Q: quit\n";

    bool quit = false;
    while (!quit && !player.EndOfFile()) {
        if (!_kbhit()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }
        const int key = _getch();
        if (key == 'q' || key == 'Q') {
            quit = true;
        } else if (key == 'p' || key == 'P') {
            const auto result = player.IsPaused() ? player.Resume() : player.Pause();
            PrintResult(player.IsPaused() ? "Resume" : "Pause", result);
        } else if (key == ' ' && !exportRunning.exchange(true)) {
            joinExport();
            exportThread = std::thread([&, outputPath] {
                std::unique_ptr<IAudioReader> exportReader;
                const auto readerResult = OpenAudio(inputPath, exportReader);
                if (!readerResult.Succeeded()) {
                    PrintResult("Export audio open", readerResult);
                    exportRunning = false;
                    return;
                }
                AudioEffectChain exportEffects;
                const auto effectResult = CreateHallChain(pluginPath, exportReader->Format(), exportEffects);
                if (!effectResult.Succeeded()) {
                    PrintResult("Export effect setup", effectResult);
                    exportRunning = false;
                    return;
                }
                WavFileWriter writer(outputPath.string());
                const auto renderResult = OfflineRenderer::Render(
                    *exportReader, writer, &exportEffects, 4096);
                PrintResult("Offline export", renderResult);
                if (renderResult.Succeeded()) {
                    std::cout << "Export finished: " << outputPath << '\n';
                }
                exportRunning = false;
            });
        }
    }

    joinExport();
    player.Close();
    return 0;
}
