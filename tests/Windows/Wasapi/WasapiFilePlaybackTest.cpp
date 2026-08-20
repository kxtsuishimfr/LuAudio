#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include <LuAudio/LuAudio.h>

namespace {

const char* DefaultAudioPath =
    R"(C:\Users\Katsu\source\repos\LuAudio\tests\Audios\WhatsAFutureFunk.wav)";

bool SameFormat(const LuAudio::Audio::AudioFormat& left, const LuAudio::Audio::AudioFormat& right)
{
    return left.sampleRate == right.sampleRate && left.channelCount == right.channelCount;
}

}

int main(int argc, char* argv[])
{
    using namespace LuAudio;

    const std::string path = argc > 1 ? argv[1] : DefaultAudioPath;
    Audio::AudioFile file(path, Audio::AudioFileType::Wav);
    Audio::WavFileReader reader;
    const auto fileResult = reader.Open(file);
    if (!fileResult.Succeeded()) {
        std::cerr << "Audio file open failed: " << fileResult.Message() << '\n';
        return 1;
    }

    Providers::Windows::Wasapi::WasapiBackend backend;
    const auto backendResult = backend.Open({});
    if (!backendResult.Succeeded()) {
        std::cerr << "WASAPI open failed: " << backendResult.Message() << '\n';
        return 1;
    }

    if (!SameFormat(reader.Format(), backend.ActualConfig().format)) {
        std::cerr << "Audio format does not match WASAPI format. File: "
                  << reader.Format().sampleRate << " Hz, " << reader.Format().channelCount
                  << " channels. Device: " << backend.ActualConfig().format.sampleRate << " Hz, "
                  << backend.ActualConfig().format.channelCount << " channels.\n";
        backend.Close();
        return 1;
    }

    std::atomic<bool> reachedEnd = false;
    std::atomic<std::size_t> renderedFrames = 0;
    backend.SetCallback([&](Audio::AudioBuffer& buffer) {
        if (!reader.EndOfFile()) {
            const auto result = reader.Read(buffer);
            if (!result.Succeeded()) {
                buffer.Clear();
                reachedEnd = true;
                return;
            }
            renderedFrames += buffer.FrameCount();
        } else {
            buffer.Clear();
            reachedEnd = true;
        }
    });

    const auto startResult = backend.Start();
    if (!startResult.Succeeded()) {
        std::cerr << "WASAPI start failed: " << startResult.Message() << '\n';
        backend.Close();
        return 1;
    }

    std::cout << "Playing " << path << "\n"
              << reader.Format().sampleRate << " Hz, " << reader.Format().channelCount
              << " channels, " << reader.FrameCount() << " frames\n";

    const auto frameDuration = std::chrono::duration<double>(
        static_cast<double>(reader.FrameCount()) / reader.Format().sampleRate);
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration) +
        std::chrono::seconds(5);
    while (!reachedEnd && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const auto stopResult = backend.Stop();
    backend.Close();
    if (!stopResult.Succeeded()) {
        std::cerr << "WASAPI stop failed: " << stopResult.Message() << '\n';
        return 1;
    }

    if (!reachedEnd) {
        std::cerr << "Playback timed out after " << renderedFrames.load() << " frames\n";
        return 1;
    }

    std::cout << "Playback completed after " << renderedFrames.load() << " frames\n";
    return 0;
}
