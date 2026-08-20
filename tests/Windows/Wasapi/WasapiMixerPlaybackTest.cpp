#include <conio.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <LuAudio/LuAudio.h>

namespace {

const char* DefaultWavPath =
    R"(C:\Users\Katsu\source\repos\LuAudio\tests\Audios\sample_1.wav)";
const char* DefaultMp3Path =
    R"(C:\Users\Katsu\source\repos\LuAudio\tests\Audios\sample_2.mp3)";

bool SameFormat(const LuAudio::Audio::AudioFormat& left, const LuAudio::Audio::AudioFormat& right)
{
    return left.sampleRate == right.sampleRate &&
        left.channelCount == right.channelCount &&
        left.sampleType == right.sampleType &&
        left.channelLayout == right.channelLayout;
}

void PrintResult(const char* action, const LuAudio::Audio::Result& result)
{
    if (!result.Succeeded()) {
        std::cerr << action << " failed: " << result.Message() << '\n';
    } else {
        std::cout << action << "\n";
    }
}

void SeekSource(LuAudio::Audio::AudioMixer& mixer,
    LuAudio::Audio::AudioMixer::SourceId sourceId,
    std::int64_t frameDelta,
    const char* label)
{
    const auto result = mixer.SeekSourceRelative(sourceId, frameDelta);
    if (!result.Succeeded()) {
        std::cerr << label << " failed: " << result.Message() << '\n';
    } else {
        std::cout << label << '\n';
    }
}

}

int main(int argc, char* argv[])
{
    using namespace LuAudio;

    const std::string wavPath = argc > 1 ? argv[1] : DefaultWavPath;
    const std::string mp3Path = argc > 2 ? argv[2] : DefaultMp3Path;

    auto wavReader = std::make_unique<Audio::WavFileReader>();
    auto wavResult = wavReader->Open(Audio::AudioFile(wavPath, Audio::AudioFileType::Wav));
    if (!wavResult.Succeeded()) {
        std::cerr << "WAV open failed: " << wavResult.Message() << '\n';
        return 1;
    }

    auto mp3Reader = std::make_unique<Audio::Mp3FileReader>(
        std::make_unique<Providers::Windows::WAudioDecoder>());
    auto mp3Result = mp3Reader->Open(Audio::AudioFile(mp3Path, Audio::AudioFileType::Mp3));
    if (!mp3Result.Succeeded()) {
        std::cerr << "MP3 open failed: " << mp3Result.Message() << '\n';
        return 1;
    }

    if (!SameFormat(wavReader->Format(), mp3Reader->Format())) {
        std::cerr << "The sources must have matching formats for this no-resampling mixer test.\n"
                  << "WAV: " << wavReader->Format().sampleRate << " Hz, "
                  << wavReader->Format().channelCount << " channels\n"
                  << "MP3: " << mp3Reader->Format().sampleRate << " Hz, "
                  << mp3Reader->Format().channelCount << " channels\n";
        return 1;
    }

    Providers::Windows::Wasapi::WasapiBackend backend;
    Audio::AudioMixer mixer(backend, 2);
    Audio::AudioStreamConfig config;
    config.format = wavReader->Format();
    const auto openResult = mixer.Open(config);
    if (!openResult.Succeeded()) {
        std::cerr << "WASAPI mixer open failed: " << openResult.Message() << '\n';
        return 1;
    }

    Audio::AudioMixer::SourceId wavId = 0;
    Audio::AudioMixer::SourceId mp3Id = 0;
    const auto addWavResult = mixer.AddSource(std::move(wavReader), wavId);
    if (!addWavResult.Succeeded()) {
        std::cerr << "WAV source add failed: " << addWavResult.Message() << '\n';
        mixer.Close();
        return 1;
    }
    const auto addMp3Result = mixer.AddSource(std::move(mp3Reader), mp3Id);
    if (!addMp3Result.Succeeded()) {
        std::cerr << "MP3 source add failed: " << addMp3Result.Message() << '\n';
        mixer.Close();
        return 1;
    }

    const auto startResult = mixer.Start();
    if (!startResult.Succeeded()) {
        std::cerr << "WASAPI mixer start failed: " << startResult.Message() << '\n';
        mixer.Close();
        return 1;
    }

    bool wavActive = true;
    bool mp3Active = true;
    bool wavPaused = false;
    bool mp3Paused = false;
    std::cout << "Playing:\n  W: " << wavPath << "\n  S: " << mp3Path << "\n"
              << "W: stop/remove WAV source\n"
              << "S: stop/remove MP3 source\n"
              << "E: pause/resume WAV source\n"
              << "R: pause/resume MP3 source\n"
              << "Left/Right: seek active sources by 5 seconds\n"
              << "Q or Escape: stop and exit\n";

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
    while ((wavActive || mp3Active) && std::chrono::steady_clock::now() < deadline) {
        if (_kbhit()) {
            const int key = _getch();
            if (key == 'q' || key == 'Q' || key == 27) {
                break;
            }
            if (key == 0 || key == 0xE0) {
                const int arrow = _getch();
                if (arrow == 75 || arrow == 77) {
                    const auto direction = arrow == 75 ? -1 : 1;
                    const auto seconds = static_cast<std::int64_t>(config.format.sampleRate) * 5 * direction;
                    if (wavActive) {
                        SeekSource(mixer, wavId, seconds, arrow == 75 ? "WAV seek left" : "WAV seek right");
                    }
                    if (mp3Active) {
                        SeekSource(mixer, mp3Id, seconds, arrow == 75 ? "MP3 seek left" : "MP3 seek right");
                    }
                }
                continue;
            }
            if (key == 'w' || key == 'W') {
                if (wavActive) {
                    PrintResult("Remove WAV source", mixer.RemoveSource(wavId));
                    wavActive = false;
                }
            } else if (key == 's' || key == 'S') {
                if (mp3Active) {
                    PrintResult("Remove MP3 source", mixer.RemoveSource(mp3Id));
                    mp3Active = false;
                }
            } else if (key == 'e' || key == 'E') {
                if (wavActive) {
                    wavPaused = !wavPaused;
                    PrintResult(wavPaused ? "Pause WAV source" : "Resume WAV source",
                        mixer.SetSourcePaused(wavId, wavPaused));
                }
            } else if (key == 'r' || key == 'R') {
                if (mp3Active) {
                    mp3Paused = !mp3Paused;
                    PrintResult(mp3Paused ? "Pause MP3 source" : "Resume MP3 source",
                        mixer.SetSourcePaused(mp3Id, mp3Paused));
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto stopResult = mixer.Stop();
    mixer.Close();
    if (!stopResult.Succeeded()) {
        std::cerr << "WASAPI mixer stop failed: " << stopResult.Message() << '\n';
        return 1;
    }
    return 0;
}
