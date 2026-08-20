#include <conio.h>

#include <algorithm>
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

class CombinedReader final : public IAudioReader {
public:
    CombinedReader(std::unique_ptr<IAudioReader> first,
        std::unique_ptr<IAudioReader> second,
        std::shared_ptr<const AudioEffectChain> firstEffects,
        std::shared_ptr<const AudioEffectChain> secondEffects)
        : first_(std::move(first)), second_(std::move(second)),
          firstEffects_(std::move(firstEffects)), secondEffects_(std::move(secondEffects)),
          format_(first_->Format()), frameCount_(std::max(first_->FrameCount(), second_->FrameCount()))
    {
    }

    Result Read(AudioBuffer& destination) override
    {
        destination.Clear();
        const auto readOne = [&](IAudioReader& reader,
            const std::shared_ptr<const AudioEffectChain>& effects) -> Result {
            if (reader.EndOfFile()) {
                return Result::Success();
            }

            AudioBuffer source(format_, destination.FrameCount());
            const auto before = reader.Position();
            const auto result = reader.Read(source);
            if (!result.Succeeded()) {
                return result;
            }
            const auto frames = static_cast<std::size_t>(reader.Position() - before);
            if (frames > destination.FrameCount()) {
                return Result::Failure(ResultCode::ProcessingFailed,
                    "Audio reader advanced beyond its requested block");
            }
            source.Resize(frames);
            if (effects && !effects->Process(source)) {
                return Result::Failure(ResultCode::ProcessingFailed,
                    "Offline source effect processing failed");
            }
            for (std::size_t index = 0; index < source.SampleCount(); ++index) {
                destination.Data()[index] += source.Data()[index];
            }
            return Result::Success();
        };

        const auto firstResult = readOne(*first_, firstEffects_);
        if (!firstResult.Succeeded()) {
            return firstResult;
        }
        const auto secondResult = readOne(*second_, secondEffects_);
        if (!secondResult.Succeeded()) {
            return secondResult;
        }
        position_ += destination.FrameCount();
        return Result::Success();
    }

    Result Seek(std::uint64_t frame) override
    {
        const auto firstResult = first_->Seek(frame);
        const auto secondResult = second_->Seek(frame);
        position_ = frame;
        return firstResult.Succeeded() ? secondResult : firstResult;
    }
    Result Rewind() override { return Seek(0); }
    std::uint64_t Position() const noexcept override { return position_; }
    bool EndOfFile() const noexcept override { return position_ >= frameCount_; }
    bool IsOpen() const noexcept override { return first_->IsOpen() && second_->IsOpen(); }
    const AudioFormat& Format() const noexcept override { return format_; }
    std::uint64_t FrameCount() const noexcept override { return frameCount_; }
    std::uint64_t FramesRemaining() const noexcept override
    {
        return position_ < frameCount_ ? frameCount_ - position_ : 0;
    }
    bool CanSeek() const noexcept override { return first_->CanSeek() && second_->CanSeek(); }

private:
    std::unique_ptr<IAudioReader> first_;
    std::unique_ptr<IAudioReader> second_;
    std::shared_ptr<const AudioEffectChain> firstEffects_;
    std::shared_ptr<const AudioEffectChain> secondEffects_;
    AudioFormat format_;
    std::uint64_t frameCount_;
    std::uint64_t position_ = 0;
};

}

int main(int argc, char* argv[])
{
    const auto wavPath = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("tests/Audios/sample_1.wav");
    const auto mp3Path = argc > 2
        ? std::filesystem::path(argv[2])
        : std::filesystem::path("tests/Audios/sample_2.mp3");
    const auto outputDirectory = argc > 3
        ? std::filesystem::path(argv[3])
        : std::filesystem::path("tests/Audios/Output");
#if defined(LUAUDIO_HALL_REVERB_PLUGIN_PATH)
    const std::filesystem::path pluginPath = LUAUDIO_HALL_REVERB_PLUGIN_PATH;
#else
    std::cerr << "Hall reverb plugin path was not configured\n";
    return 1;
#endif

    std::unique_ptr<IAudioReader> wavReader;
    std::unique_ptr<IAudioReader> mp3Reader;
    const auto wavOpen = OpenAudio(wavPath, wavReader);
    const auto mp3Open = OpenAudio(mp3Path, mp3Reader);
    if (!wavOpen.Succeeded() || !mp3Open.Succeeded()) {
        PrintResult("WAV open", wavOpen);
        PrintResult("MP3 open", mp3Open);
        return 1;
    }
    if (wavReader->Format().sampleRate != mp3Reader->Format().sampleRate ||
        wavReader->Format().channelCount != mp3Reader->Format().channelCount) {
        std::cerr << "The WAV and MP3 sources must have matching formats\n";
        return 1;
    }
    const auto sampleRate = wavReader->Format().sampleRate;

    auto wavEffects = std::make_shared<AudioEffectChain>();
    auto mp3Effects = std::make_shared<AudioEffectChain>();
    const auto wavEffectResult = CreateHallChain(pluginPath, wavReader->Format(), *wavEffects);
    const auto mp3EffectResult = CreateHallChain(pluginPath, mp3Reader->Format(), *mp3Effects);
    if (!wavEffectResult.Succeeded() || !mp3EffectResult.Succeeded()) {
        PrintResult("WAV effect setup", wavEffectResult);
        PrintResult("MP3 effect setup", mp3EffectResult);
        return 1;
    }

    Providers::Windows::Wasapi::WasapiBackend backend;
    AudioMixer mixer(backend, 2);
    const auto mixerOpen = mixer.Open(AudioStreamConfig{wavReader->Format(), 512, false});
    if (!mixerOpen.Succeeded()) {
        PrintResult("WASAPI mixer open", mixerOpen);
        return 1;
    }
    AudioMixer::SourceId wavId = 0;
    AudioMixer::SourceId mp3Id = 0;
    const auto addWavResult = mixer.AddSource(std::move(wavReader), wavId);
    const auto addMp3Result = mixer.AddSource(std::move(mp3Reader), mp3Id);
    if (!addWavResult.Succeeded() || !addMp3Result.Succeeded()) {
        PrintResult("WAV source add", addWavResult);
        PrintResult("MP3 source add", addMp3Result);
        mixer.Close();
        return 1;
    }
    bool wavReverb = true;
    bool mp3Reverb = true;
    mixer.SetSourceEffects(wavId, wavEffects);
    mixer.SetSourceEffects(mp3Id, mp3Effects);
    const auto startResult = mixer.Start();
    if (!startResult.Succeeded()) {
        PrintResult("WASAPI mixer start", startResult);
        mixer.Close();
        return 1;
    }

    std::filesystem::create_directories(outputDirectory);
    const auto outputPath = outputDirectory / "sample_1_plus_sample_2_hall_reverb.wav";
    std::atomic<bool> exportRunning = false;
    std::thread exportThread;

    const auto joinExport = [&] {
        if (exportThread.joinable()) {
            std::cout << "Waiting for export thread to finish...\n";
            exportThread.join();
            std::cout << "Export thread finished.\n";
        }
    };

    std::cout << "Playing " << wavPath << " and " << mp3Path
              << " through WASAPI with ProfessionalHallReverb.\n"
              << "Space: offline export to " << outputPath << "\n"
              << "R: pause/resume both sources\n"
              << "Left/Right: seek both sources by 5 seconds\n"
              << "O: toggle reverb for sample_1.wav\n"
              << "P: toggle reverb for sample_2.mp3\n"
              << "Q: quit\n";

    bool quit = false;
    bool paused = false;
    while (!quit && mixer.ActiveSourceCount() != 0 &&
        !(mixer.IsSourceFinished(wavId) && mixer.IsSourceFinished(mp3Id))) {
        if (!_kbhit()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }
        const int key = _getch();
        if (key == 'q' || key == 'Q') {
            quit = true;
        } else if (key == 'r' || key == 'R') {
            paused = !paused;
            PrintResult(paused ? "Pause both sources" : "Resume both sources",
                mixer.SetSourcePaused(wavId, paused));
            PrintResult(paused ? "Pause MP3 source" : "Resume MP3 source",
                mixer.SetSourcePaused(mp3Id, paused));
        } else if (key == 'o' || key == 'O') {
            wavReverb = !wavReverb;
            PrintResult(wavReverb ? "Enable WAV reverb" : "Disable WAV reverb",
                mixer.SetSourceEffects(wavId, wavReverb ? wavEffects : nullptr));
        } else if (key == 'p' || key == 'P') {
            mp3Reverb = !mp3Reverb;
            PrintResult(mp3Reverb ? "Enable MP3 reverb" : "Disable MP3 reverb",
                mixer.SetSourceEffects(mp3Id, mp3Reverb ? mp3Effects : nullptr));
        } else if (key == 0 || key == 0xE0) {
            const int arrow = _getch();
            if (arrow == 75 || arrow == 77) {
                const auto seconds = static_cast<std::int64_t>(sampleRate) * 5;
                const auto seek = arrow == 75 ? -seconds : seconds;
                PrintResult("Seek WAV", mixer.SeekSourceRelative(wavId, seek));
                PrintResult("Seek MP3", mixer.SeekSourceRelative(mp3Id, seek));
            }
        } else if (key == ' ' && !exportRunning.exchange(true)) {
            joinExport();
            exportThread = std::thread([&, outputPath, wavReverb, mp3Reverb] {
                std::unique_ptr<IAudioReader> exportWav;
                std::unique_ptr<IAudioReader> exportMp3;
                const auto exportWavResult = OpenAudio(wavPath, exportWav);
                const auto exportMp3Result = OpenAudio(mp3Path, exportMp3);
                if (!exportWavResult.Succeeded() || !exportMp3Result.Succeeded()) {
                    PrintResult("Export WAV open", exportWavResult);
                    PrintResult("Export MP3 open", exportMp3Result);
                    exportRunning = false;
                    return;
                }
                std::shared_ptr<AudioEffectChain> exportWavEffects;
                std::shared_ptr<AudioEffectChain> exportMp3Effects;
                if (wavReverb) {
                    exportWavEffects = std::make_shared<AudioEffectChain>();
                    const auto result = CreateHallChain(
                        pluginPath, exportWav->Format(), *exportWavEffects);
                    if (!result.Succeeded()) {
                        PrintResult("Export WAV effect setup", result);
                        exportRunning = false;
                        return;
                    }
                }
                if (mp3Reverb) {
                    exportMp3Effects = std::make_shared<AudioEffectChain>();
                    const auto result = CreateHallChain(
                        pluginPath, exportMp3->Format(), *exportMp3Effects);
                    if (!result.Succeeded()) {
                        PrintResult("Export MP3 effect setup", result);
                        exportRunning = false;
                        return;
                    }
                }
                CombinedReader exportReader(std::move(exportWav), std::move(exportMp3),
                    exportWavEffects, exportMp3Effects);
                WavFileWriter writer(outputPath.string());
                const auto renderResult = OfflineRenderer::Render(
                    exportReader, writer, nullptr, 4096);
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
