#include <chrono>
#include <iostream>
#include <thread>

#include <LuAudio/LuAudio.h>

int main()
{
    using namespace LuAudio;

    Providers::Windows::Wasapi::WasapiBackend backend;
    Audio::AudioStreamConfig config;
    const auto openResult = backend.Open(config);
    if (!openResult.Succeeded()) {
        std::cerr << "WASAPI open failed: " << openResult.Message() << '\n';
        return 1;
    }

    backend.SetCallback([](Audio::AudioBuffer& buffer) {
        buffer.Clear();
    });

    const auto startResult = backend.Start();
    if (!startResult.Succeeded()) {
        std::cerr << "WASAPI start failed: " << startResult.Message() << '\n';
        backend.Close();
        return 1;
    }

    const auto& actualConfig = backend.ActualConfig();
    std::cout << "WASAPI running at " << actualConfig.format.sampleRate << " Hz, "
              << actualConfig.format.channelCount << " channels\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    const auto stopResult = backend.Stop();
    backend.Close();
    if (!stopResult.Succeeded()) {
        std::cerr << "WASAPI stop failed: " << stopResult.Message() << '\n';
        return 1;
    }

    return 0;
}
