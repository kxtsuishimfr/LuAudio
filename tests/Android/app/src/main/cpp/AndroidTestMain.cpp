#if defined(__ANDROID__)
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include <android/log.h>
#include <jni.h>

#include <LuAudio/LuAudio.h>

namespace {

constexpr const char* kLogTag = "LuAudioAndroidTests";
constexpr const char* kAudioPath = "/sdcard/LuAudio_Tests/sample_2.mp3";

std::mutex sessionMutex;
std::unique_ptr<LuAudio::Providers::Android::Oboe::OboeBackend> backend;
std::unique_ptr<LuAudio::Audio::AudioPlayer> player;

std::string ResultText(const LuAudio::Audio::Result& result)
{
    return result.Succeeded() ? "PASS" : "FAIL: " + result.Message();
}

std::string StatusTextLocked()
{
    if (!player) {
        return "Player is not open.";
    }

    std::ostringstream output;
    output << "Position: " << player->Position() << " / " << player->FrameCount()
           << "\nRequested: " << player->RequestedPosition()
           << "\nEnd of file: " << (player->EndOfFile() ? "yes" : "no");
    return output.str();
}

std::string StartPlaybackLocked()
{
    if (player) {
        return "Playback is already running.\n\n" + StatusTextLocked();
    }

    auto reader = std::make_unique<LuAudio::Audio::Mp3FileReader>(
        std::make_unique<LuAudio::Providers::Android::AAudioDecoder>());
    const auto fileResult = reader->Open(
        LuAudio::Audio::AudioFile(kAudioPath, LuAudio::Audio::AudioFileType::Mp3));
    if (!fileResult.Succeeded()) {
        return "Open MP3: " + ResultText(fileResult);
    }

    backend = std::make_unique<LuAudio::Providers::Android::Oboe::OboeBackend>();
    player = std::make_unique<LuAudio::Audio::AudioPlayer>(*backend);

    LuAudio::Audio::AudioStreamConfig config;
    config.format = reader->Format();
    const auto openResult = player->Open(std::move(reader), config);
    if (!openResult.Succeeded()) {
        player.reset();
        backend.reset();
        return "Open player: " + ResultText(openResult);
    }

    const auto startResult = player->Start();
    if (!startResult.Succeeded()) {
        player->Close();
        player.reset();
        backend.reset();
        return "Start playback: " + ResultText(startResult);
    }

    return "Playback started.\n\n" + StatusTextLocked();
}

std::string SeekLocked(std::uint64_t frame, const char* action)
{
    if (!player) {
        return "Player is not open.";
    }

    const auto result = player->Seek(frame);
    if (!result.Succeeded()) {
        return std::string(action) + ": " + ResultText(result);
    }
    return std::string(action) + ".\n\n" + StatusTextLocked();
}

}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStartPlayback(JNIEnv* environment, jclass)
{
    std::lock_guard lock(sessionMutex);
    const auto text = StartPlaybackLocked();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", text.c_str());
    return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSeekMiddle(JNIEnv* environment, jclass)
{
    std::lock_guard lock(sessionMutex);
    const auto frame = player ? player->FrameCount() / 2 : 0;
    const auto text = SeekLocked(frame, "Seek to middle");
    return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeRewind(JNIEnv* environment, jclass)
{
    std::lock_guard lock(sessionMutex);
    if (!player) {
        return environment->NewStringUTF("Player is not open.");
    }
    const auto text = ResultText(player->Rewind()) + ".\n\n" + StatusTextLocked();
    return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSeekRelative(JNIEnv* environment, jclass, jlong seconds)
{
    std::lock_guard lock(sessionMutex);
    if (!player) {
        return environment->NewStringUTF("Player is not open.");
    }

    const auto current = player->RequestedPosition();
    const auto total = player->FrameCount();
    const auto delta = static_cast<std::int64_t>(player->Format().sampleRate) * seconds;
    std::uint64_t target = current;
    if (delta < 0) {
        const auto distance = static_cast<std::uint64_t>(-delta);
        target = distance > current ? 0 : current - distance;
    } else {
        const auto distance = static_cast<std::uint64_t>(delta);
        target = distance > total - current ? total : current + distance;
    }

    const auto action = seconds < 0 ? "Seek backward 5 seconds" : "Seek forward 5 seconds";
    const auto text = SeekLocked(target, action);
    return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSeekEnd(JNIEnv* environment, jclass)
{
    std::lock_guard lock(sessionMutex);
    const auto frame = player ? player->FrameCount() : 0;
    const auto text = SeekLocked(frame, "Seek to end");
    return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStatus(JNIEnv* environment, jclass)
{
    std::lock_guard lock(sessionMutex);
    const auto text = StatusTextLocked();
    return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStopPlayback(JNIEnv*, jclass)
{
    std::lock_guard lock(sessionMutex);
    if (player) {
        player->Close();
        player.reset();
        backend.reset();
    }
}
#endif
