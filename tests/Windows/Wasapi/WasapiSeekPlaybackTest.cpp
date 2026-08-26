#include <conio.h>
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <LuAudio/LuAudio.h>

namespace {

const char* DefaultAudioPath =
    R"(C:\Users\Katsu\source\repos\LuAudio\tests\Audios\sample_4.ogg)";

void PrintPosition(const LuAudio::Audio::AudioPlayer& player, const char* action)
{
    std::cout << action << " -> requested frame " << player.RequestedPosition()
              << ", applied frame " << player.Position() << '\n';
}

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
    PrintPosition(player, action);
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
    auto reader = std::make_unique<Audio::OggFileReader>(nullptr);
    const auto fileResult = reader->Open(
        Audio::AudioFile(path, Audio::AudioFileType::Ogg));
    if (!fileResult.Succeeded()) {
        std::cerr << "Audio file open failed: " << fileResult.Message() << '\n';
        return 1;
    }

    auto backend = Audio::CreateBackend();
    Audio::AudioPlayer player(*backend);
    Audio::AudioStreamConfig requestedConfig;
    requestedConfig.format = reader->Format();

    const auto openResult = player.Open(std::move(reader), requestedConfig);
    if (!openResult.Succeeded()) {
        std::cerr << "Audio player open failed: " << openResult.Message() << '\n';
        return 1;
    }

    const auto startResult = player.Start();
    if (!startResult.Succeeded()) {
        std::cerr << "Audio backend start failed: " << startResult.Message() << '\n';
        player.Close();
        return 1;
    }

    std::cout << "Playing " << path << '\n'
              << "Space/M: seek to the middle\n"
              << "Alt/R/Home: seek to the beginning\n"
              << "Left/Right: seek backward/forward 5 seconds\n"
              << "End/E: seek to the end\n"
              << "P: print playback position and EOF state\n"
              << "Q or Escape: stop and exit\n";
    PrintStatus(player);

    bool altWasPressed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(1);
    while (std::chrono::steady_clock::now() < deadline) {
        const bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        if (altPressed && !altWasPressed) {
            const auto result = player.Rewind();
            if (!result.Succeeded()) {
                std::cerr << "Rewind failed: " << result.Message() << '\n';
            } else {
                PrintPosition(player, "Rewind");
            }
        }
        altWasPressed = altPressed;

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
                if (altPressed) {
                    continue;
                }
                SeekTo(player, player.FrameCount() / 2, "Seek to middle");
            } else if (key == 'm' || key == 'M') {
                SeekTo(player, player.FrameCount() / 2, "Seek to middle");
            } else if (key == 'r' || key == 'R') {
                const auto result = player.Rewind();
                if (!result.Succeeded()) {
                    std::cerr << "Rewind failed: " << result.Message() << '\n';
                } else {
                    PrintPosition(player, "Rewind");
                }
            } else if (key == 'e' || key == 'E') {
                SeekTo(player, player.FrameCount(), "Seek to end");
            } else if (key == 'p' || key == 'P') {
                PrintStatus(player);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto stopResult = player.Stop();
    player.Close();
    if (!stopResult.Succeeded()) {
        std::cerr << "Audio backend stop failed: " << stopResult.Message() << '\n';
        return 1;
    }
    return 0;
}