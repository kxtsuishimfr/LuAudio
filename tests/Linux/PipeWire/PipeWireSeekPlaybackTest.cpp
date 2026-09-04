#include <LuAudio/LuAudio.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace {

class TerminalMode final {
public:
    TerminalMode()
    {
        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            return;
        }
        auto raw = original_;
        raw.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        active_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }

    ~TerminalMode()
    {
        if (active_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original_);
        }
    }

    TerminalMode(const TerminalMode&) = delete;
    TerminalMode& operator=(const TerminalMode&) = delete;

private:
    termios original_{};
    bool active_ = false;
};

bool KeyAvailable()
{
    timeval timeout{};
    fd_set input;
    FD_ZERO(&input);
    FD_SET(STDIN_FILENO, &input);
    return select(STDIN_FILENO + 1, &input, nullptr, nullptr, &timeout) > 0;
}

void PrintStatus(const LuAudio::Audio::AudioMixer& mixer,
    LuAudio::Audio::AudioMixer::SourceId sourceId)
{
    std::cout << "Position: " << mixer.SourcePosition(sourceId)
              << " frames, finished: "
              << (mixer.IsSourceFinished(sourceId) ? "yes" : "no") << '\n';
}

}

int main(int argc, char* argv[])
{
    using namespace LuAudio;

    if (argc != 2) {
        std::cerr << "Usage: LuAudioPipeWireSeekPlaybackTest <wav-file>\n";
        return 1;
    }

    auto reader = std::make_unique<Audio::WavFileReader>();
    const auto readerResult = reader->Open(Audio::AudioFile(argv[1], Audio::AudioFileType::Wav));
    if (!readerResult.Succeeded()) {
        std::cerr << "WAV open failed: " << readerResult.Message() << '\n';
        return 2;
    }

    auto backend = Audio::CreateBackend();
    if (!backend) {
        std::cerr << "PipeWire backend creation failed\n";
        return 3;
    }

    Audio::AudioMixer mixer(*backend, 1);
    Audio::AudioStreamConfig config;
    config.format = reader->Format();
    const auto frameCount = reader->FrameCount();
    const auto seekStep = static_cast<std::int64_t>(config.format.sampleRate) * 5;
    const auto openResult = mixer.Open(config);
    if (!openResult.Succeeded()) {
        std::cerr << "PipeWire mixer open failed: " << openResult.Message() << '\n';
        return 4;
    }

    Audio::AudioMixer::SourceId sourceId = 0;
    const auto addResult = mixer.AddSource(std::move(reader), sourceId);
    if (!addResult.Succeeded()) {
        std::cerr << "WAV source add failed: " << addResult.Message() << '\n';
        mixer.Close();
        return 5;
    }

    const auto startResult = mixer.Start();
    if (!startResult.Succeeded()) {
        std::cerr << "PipeWire mixer start failed: " << startResult.Message() << '\n';
        mixer.Close();
        return 6;
    }

    std::cout << "Playing " << argv[1] << '\n'
              << "Left/Right: seek backward/forward 5 seconds\n"
              << "M: seek to middle, R: rewind, P: pause/resume, S: status\n"
              << "Q or Escape: stop and exit\n";
    PrintStatus(mixer, sourceId);

    TerminalMode terminal;
    bool paused = false;
    bool quit = false;
    while (!quit && !mixer.IsSourceFinished(sourceId)) {
        if (KeyAvailable()) {
            auto key = static_cast<char>(std::cin.get());
            if (key == 27 && KeyAvailable() && std::cin.get() == '[' && KeyAvailable()) {
                const auto arrow = static_cast<char>(std::cin.get());
                const auto delta = arrow == 'D' ? -seekStep : arrow == 'C' ? seekStep : 0;
                if (delta != 0) {
                    const auto result = mixer.SeekSourceRelative(sourceId, delta);
                    if (!result.Succeeded()) {
                        std::cerr << "Seek failed: " << result.Message() << '\n';
                    }
                }
                continue;
            }
            if (key == 'q' || key == 'Q' || key == 27) {
                quit = true;
            } else if (key == 'm' || key == 'M') {
                const auto result = mixer.SeekSource(sourceId, frameCount / 2);
                if (!result.Succeeded()) {
                    std::cerr << "Seek failed: " << result.Message() << '\n';
                }
            } else if (key == 'r' || key == 'R') {
                const auto result = mixer.SeekSource(sourceId, 0);
                if (!result.Succeeded()) {
                    std::cerr << "Rewind failed: " << result.Message() << '\n';
                }
            } else if (key == 'p' || key == 'P') {
                paused = !paused;
                const auto result = mixer.SetSourcePaused(sourceId, paused);
                if (!result.Succeeded()) {
                    std::cerr << "Pause/resume failed: " << result.Message() << '\n';
                }
            } else if (key == 's' || key == 'S') {
                PrintStatus(mixer, sourceId);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto stopResult = mixer.Stop();
    mixer.Close();
    if (!stopResult.Succeeded()) {
        std::cerr << "PipeWire mixer stop failed: " << stopResult.Message() << '\n';
        return 7;
    }
    return 0;
}