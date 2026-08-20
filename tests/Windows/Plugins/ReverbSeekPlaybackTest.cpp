#include <conio.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <LuAudio/LuAudio.h>
#include <LuAudio/Audio/Contracts/IAudioEffect.h>
#include <LuAudio/Audio/Processing/AudioEffectChain.h>

namespace {

const char* DefaultAudioPath =
    R"(C:\Users\Katsu\source\repos\LuAudio\tests\Audios\sample_1.wav)";

class SmallHallReverb final : public LuAudio::Audio::IAudioEffect {
public:
    explicit SmallHallReverb(const LuAudio::Audio::AudioFormat& format)
        : channelCount_(format.channelCount), sampleRate_(format.sampleRate)
    {
        constexpr double delayMilliseconds[] = {29.7, 41.3, 67.1};
        for (const double milliseconds : delayMilliseconds) {
            const auto frames = static_cast<std::size_t>(
                std::round(milliseconds * sampleRate_ / 1000.0));
            delays_.push_back(std::max<std::size_t>(frames, 1));
            lines_.emplace_back(frames * channelCount_, 0.0F);
            positions_.push_back(0);
        }
    }

    bool Process(LuAudio::Audio::AudioBuffer& buffer) noexcept override
    {
        if (!enabled_.load(std::memory_order_relaxed)) {
            return true;
        }
        if (buffer.Format().channelCount != channelCount_
            || buffer.Format().sampleRate != sampleRate_) {
            return false;
        }

        constexpr float feedback = 0.78F;
        constexpr float wetMix = 0.58F;
        for (std::size_t frame = 0; frame < buffer.FrameCount(); ++frame) {
            for (std::size_t channel = 0; channel < channelCount_; ++channel) {
                const auto sampleIndex = frame * channelCount_ + channel;
                const float input = buffer.Data()[sampleIndex];
                float wet = 0.0F;
                for (std::size_t line = 0; line < lines_.size(); ++line) {
                    wet += lines_[line][positions_[line] * channelCount_ + channel];
                }
                wet /= static_cast<float>(lines_.size());
                buffer.Data()[sampleIndex] = input + wet * wetMix;
                for (std::size_t line = 0; line < lines_.size(); ++line) {
                    lines_[line][positions_[line] * channelCount_ + channel]
                        = input + wet * feedback;
                }
            }
            for (std::size_t line = 0; line < positions_.size(); ++line) {
                positions_[line] = (positions_[line] + 1) % delays_[line];
            }
        }
        return true;
    }

    void Reset() noexcept override
    {
        for (auto& line : lines_) {
            std::fill(line.begin(), line.end(), 0.0F);
        }
        std::fill(positions_.begin(), positions_.end(), 0);
    }

    void SetEnabled(bool enabled) noexcept
    {
        enabled_.store(enabled, std::memory_order_relaxed);
    }

private:
    std::size_t channelCount_;
    std::uint32_t sampleRate_;
    std::vector<std::size_t> delays_;
    std::vector<std::vector<float>> lines_;
    std::vector<std::size_t> positions_;
    std::atomic<bool> enabled_{true};
};

void PrintStatus(const LuAudio::Audio::AudioPlayer& player)
{
    std::cout << "Status: applied=" << player.Position()
              << ", requested=" << player.RequestedPosition()
              << ", total=" << player.FrameCount()
              << ", eof=" << (player.EndOfFile() ? "yes" : "no") << '\n';
}

void SeekTo(LuAudio::Audio::AudioPlayer& player, std::uint64_t frame, const char* action)
{
    const auto result = player.Seek(frame);
    if (!result.Succeeded()) {
        std::cerr << "Seek failed: " << result.Message() << '\n';
        return;
    }
    std::cout << action << " -> requested frame " << frame << '\n';
}

void SeekRelative(LuAudio::Audio::AudioPlayer& player, std::int64_t frameDelta)
{
    const auto current = player.Position();
    const auto total = player.FrameCount();
    std::uint64_t target = current;
    if (frameDelta < 0) {
        const auto distance = static_cast<std::uint64_t>(-frameDelta);
        target = distance > current ? 0 : current - distance;
    } else {
        const auto distance = static_cast<std::uint64_t>(frameDelta);
        target = distance > total - current ? total : current + distance;
    }
    SeekTo(player, target, frameDelta < 0 ? "Seek backward" : "Seek forward");
}

}

int main(int argc, char* argv[])
{
    using namespace LuAudio;

    const std::string path = argc > 1 ? argv[1] : DefaultAudioPath;
    auto reader = std::make_unique<Audio::WavFileReader>();
    const auto fileResult = reader->Open(Audio::AudioFile(path, Audio::AudioFileType::Wav));
    if (!fileResult.Succeeded()) {
        std::cerr << "Audio file open failed: " << fileResult.Message() << '\n';
        return 1;
    }

    Audio::AudioEffectChain effects;
    auto reverb = std::make_unique<SmallHallReverb>(reader->Format());
    SmallHallReverb* reverbPointer = reverb.get();
    effects.Add(std::move(reverb));

    Providers::Windows::Wasapi::WasapiBackend backend;
    Audio::AudioPlayer player(backend);
    player.SetEffectChain(&effects);
    Audio::AudioStreamConfig config;
    config.format = reader->Format();
    const auto openResult = player.Open(std::move(reader), config);
    if (!openResult.Succeeded()) {
        std::cerr << "Audio player open failed: " << openResult.Message() << '\n';
        return 1;
    }

    const auto startResult = player.Start();
    if (!startResult.Succeeded()) {
        std::cerr << "WASAPI start failed: " << startResult.Message() << '\n';
        return 1;
    }

    std::cout << "Playing " << path << '\n'
              << "Space: toggle small hall reverb\n"
              << "M/Left/Right/Home/End: seek\n"
              << "P: pause/resume, R: rewind, O: status, Q/Escape: stop and exit\n"
              << "Reverb: on\n";
    PrintStatus(player);

    bool reverbEnabled = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
    while (std::chrono::steady_clock::now() < deadline) {
        if (_kbhit()) {
            const int key = _getch();
            if (key == 'q' || key == 'Q' || key == 27) {
                break;
            }
            if (key == 0 || key == 0xE0) {
                switch (_getch()) {
                case 71:
                    SeekTo(player, 0, "Seek to beginning");
                    break;
                case 75:
                    SeekRelative(player, -static_cast<std::int64_t>(player.Format().sampleRate) * 5);
                    break;
                case 77:
                    SeekRelative(player, static_cast<std::int64_t>(player.Format().sampleRate) * 5);
                    break;
                case 79:
                    SeekTo(player, player.FrameCount(), "Seek to end");
                    break;
                default:
                    break;
                }
                continue;
            }
            if (key == ' ') {
                reverbEnabled = !reverbEnabled;
                reverbPointer->SetEnabled(reverbEnabled);
                std::cout << "Reverb: " << (reverbEnabled ? "on" : "off") << '\n';
            } else if (key == 'm' || key == 'M') {
                SeekTo(player, player.FrameCount() / 2, "Seek to middle");
            } else if (key == 'p' || key == 'P') {
                const auto result = player.IsPaused() ? player.Resume() : player.Pause();
                if (!result.Succeeded()) {
                    std::cerr << "Pause/resume failed: " << result.Message() << '\n';
                } else {
                    std::cout << (player.IsPaused() ? "Paused\n" : "Resumed\n");
                }
            } else if (key == 'r' || key == 'R') {
                player.Rewind();
                effects.Reset();
                std::cout << "Rewound\n";
            } else if (key == 'e' || key == 'E') {
                SeekTo(player, player.FrameCount(), "Seek to end");
            } else if (key == 'o' || key == 'O') {
                PrintStatus(player);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    player.Stop();
    player.Close();
    return 0;
}