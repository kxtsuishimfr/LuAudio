#if defined(__ANDROID__)
#include <atomic>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <thread>

#include <android/log.h>
#include <jni.h>

#include <LuAudio/LuAudio.h>

namespace {

constexpr const char* kLogTag = "LuAudioAndroidTests";
constexpr const char* kAudioPath = "/sdcard/LuAudio_Tests/sample_1.wav";

void AppendResult(std::ostringstream& output, const char* name, const LuAudio::Audio::Result& result)
{
    output << name << ": " << (result.Succeeded() ? "PASS" : "FAIL");
    if (!result.Succeeded()) {
        output << " (" << result.Message() << ")";
    }
    output << '\n';
}

}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_runPlaybackTest(JNIEnv* environment, jclass)
{
    using LuAudio::Audio::AudioFile;
    using LuAudio::Audio::AudioFileType;
    using LuAudio::Audio::AudioStreamConfig;
    using LuAudio::Audio::WavFileReader;
    using LuAudio::Providers::Android::Oboe::OboeBackend;

    std::ostringstream output;
    output << "LuAudio Android playback test\n\n";

    WavFileReader reader;
    const auto fileResult = reader.Open(AudioFile(kAudioPath, AudioFileType::Wav));
    AppendResult(output, "Open WAV", fileResult);
    if (!fileResult.Succeeded()) {
        const auto text = output.str();
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", text.c_str());
        return environment->NewStringUTF(text.c_str());
    }

    output << "File sample rate: " << reader.Format().sampleRate << '\n';
    output << "File channels: " << reader.Format().channelCount << '\n';
    output << "File frames: " << reader.FrameCount() << "\n\n";

    OboeBackend backend;
    AudioStreamConfig config;
    config.format = reader.Format();
    std::atomic<std::size_t> renderedFrames = 0;
    std::atomic<bool> reachedEnd = false;

    const auto openResult = backend.Open(config);
    AppendResult(output, "Open Oboe", openResult);
    if (!openResult.Succeeded()) {
        const auto text = output.str();
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", text.c_str());
        return environment->NewStringUTF(text.c_str());
    }

    if (backend.ActualConfig().format.sampleRate != reader.Format().sampleRate ||
        backend.ActualConfig().format.channelCount != reader.Format().channelCount) {
        output << "FAIL: Oboe format does not match WAV format\n";
        backend.Close();
        const auto text = output.str();
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", text.c_str());
        return environment->NewStringUTF(text.c_str());
    }

    backend.SetCallback([&reader, &renderedFrames, &reachedEnd](LuAudio::Audio::AudioBuffer& buffer) {
        if (reader.EndOfFile()) {
            buffer.Clear();
            reachedEnd = true;
            return;
        }

        const auto result = reader.Read(buffer);
        if (!result.Succeeded()) {
            buffer.Clear();
            reachedEnd = true;
            return;
        }
        renderedFrames += buffer.FrameCount();
    });

    const auto startResult = backend.Start();
    AppendResult(output, "Start", startResult);
    if (startResult.Succeeded()) {
        const auto duration = std::chrono::duration<double>(
            static_cast<double>(reader.FrameCount()) / reader.Format().sampleRate);
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration) +
            std::chrono::seconds(5);
        while (!reachedEnd && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        const auto stopResult = backend.Stop();
        AppendResult(output, "Stop", stopResult);
    }

    backend.Close();
    output << "Rendered frames: " << renderedFrames.load() << '\n';
    output << "Playback: " << (reachedEnd ? "PASS" : "TIMEOUT") << '\n';
    output << "Close: PASS\n";

    const auto text = output.str();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", text.c_str());
    return environment->NewStringUTF(text.c_str());
}
    #endif