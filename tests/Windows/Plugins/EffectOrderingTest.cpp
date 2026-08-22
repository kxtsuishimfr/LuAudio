#include <conio.h>

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

const std::filesystem::path DefaultAudioPath = "tests/Audios/sample_2.mp3";

class PreGain final : public IAudioEffect {
public:
    explicit PreGain(float gain)
        : gain_(gain)
    {
    }

    bool Process(AudioBuffer& buffer) noexcept override
    {
        const auto sampleCount = buffer.SampleCount();
        for (std::size_t index = 0; index < sampleCount; ++index) {
            const float sample = buffer.Data()[index];
            const float warped = sample * (1.0F + gain_ * std::abs(sample));
            buffer.Data()[index] = warped;
        }
        return true;
    }

    void Reset() noexcept override {}

private:
    float gain_;
};

class SoftClip final : public IAudioEffect {
public:
    explicit SoftClip(float intensity)
        : intensity_(intensity)
    {
    }

    bool Process(AudioBuffer& buffer) noexcept override
    {
        const auto sampleCount = buffer.SampleCount();
        for (std::size_t index = 0; index < sampleCount; ++index) {
            const float sample = buffer.Data()[index];
            buffer.Data()[index] = std::tanh(sample * intensity_);
        }
        return true;
    }

    void Reset() noexcept override {}

private:
    float intensity_;
};

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

} // namespace

int main(int argc, char* argv[])
{
    const auto audio_path = argc > 1 ? std::filesystem::path(argv[1]) : DefaultAudioPath;

    std::unique_ptr<IAudioReader> reader;
    const auto open_result = OpenAudio(audio_path, reader);
    if (!open_result.Succeeded()) {
        std::cerr << "MP3 open failed: " << open_result.Message() << '\n';
        return 1;
    }

    AudioEffectChain effects;
    const auto driveId = effects.Add(std::make_unique<PreGain>(3.0F));
    const auto warpId = effects.Add(std::make_unique<SoftClip>(2.5F));

    Providers::Windows::Wasapi::WasapiBackend backend;
    AudioPlayer player(backend);
    AudioStreamConfig config;
    config.format = reader->Format();
    const auto player_open = player.Open(std::move(reader), config);
    if (!player_open.Succeeded()) {
        std::cerr << "Audio player open failed: " << player_open.Message() << '\n';
        return 1;
    }

    player.SetEffectChain(&effects);
    const auto start_result = player.Start();
    if (!start_result.Succeeded()) {
        std::cerr << "WASAPI start failed: " << start_result.Message() << '\n';
        return 1;
    }

    std::cout << "Order-sensitive effect demo\n"
              << "Space: pause/resume\n"
              << "R: reverse the current effect order\n"
              << "Q: quit\n\n";
    std::cout << "Initial order: pre-gain -> soft-clip\n";

    bool playing = true;
    bool orderForward = true;
    while (!player.EndOfFile() && playing) {
        if (_kbhit()) {
            const int key = _getch();
            if (key == 'q' || key == 'Q' || key == 27) {
                break;
            }

            if (key == ' ') {
                if (player.IsPaused()) {
                    const auto result = player.Resume();
                    if (!result.Succeeded()) {
                        std::cerr << "Resume failed: " << result.Message() << '\n';
                    } else {
                        std::cout << "Resumed playback\n";
                    }
                } else {
                    const auto result = player.Pause();
                    if (!result.Succeeded()) {
                        std::cerr << "Pause failed: " << result.Message() << '\n';
                    } else {
                        std::cout << "Paused playback\n";
                    }
                }
            } else if (key == 'r' || key == 'R') {
                const auto ids = orderForward ? std::vector<AudioEffectChain::EffectId>{driveId, warpId}
                                              : std::vector<AudioEffectChain::EffectId>{warpId, driveId};
                const auto changed = effects.SetOrder(ids);
                if (!changed) {
                    std::cerr << "SetOrder failed; effect IDs not valid for current chain\n";
                } else {
                    orderForward = !orderForward;
                    std::cout << "Switched order: "
                              << (orderForward ? "pre-gain -> soft-clip"
                                                : "soft-clip -> pre-gain")
                              << '\n';
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    player.Stop();
    player.Close();
    return 0;
}
