#include <android/log.h>
#include <jni.h>

#include <memory>
#include <mutex>
#include <cstdint>
#include <string>

#include <LuAudio/LuAudio.h>

namespace LuAudio::AndroidTests {

namespace {

std::mutex mixerMutex;
std::unique_ptr<Audio::AudioMixer> mixer;
std::unique_ptr<Providers::Android::Oboe::OboeBackend> backend;
Audio::AudioMixer::SourceId wavId = 0;
Audio::AudioMixer::SourceId mp3Id = 0;
std::uint32_t mixerSampleRate = 0;

std::string StartMixer(const std::string& testDirectory)
{
    using namespace Audio;

    std::lock_guard lock(mixerMutex);
    if (mixer) {
        return "FAIL: mixer is already running";
    }

    const std::string wavPath = testDirectory + "/sample_1.wav";
    const std::string mp3Path = testDirectory + "/sample_2.mp3";

    auto wavReader = std::make_unique<WavFileReader>();
    const auto wavResult = wavReader->Open(AudioFile(wavPath, AudioFileType::Wav));
    if (!wavResult.Succeeded()) {
        return "FAIL: WAV open: " + wavResult.Message();
    }

    auto mp3Reader = std::make_unique<Mp3FileReader>(
        std::make_unique<Providers::Android::AAudioDecoder>());
    const auto mp3Result = mp3Reader->Open(AudioFile(mp3Path, AudioFileType::Mp3));
    if (!mp3Result.Succeeded()) {
        return "FAIL: MP3 open: " + mp3Result.Message();
    }

    const auto& wavFormat = wavReader->Format();
    const auto& mp3Format = mp3Reader->Format();
    if (wavFormat.sampleRate != mp3Format.sampleRate ||
        wavFormat.channelCount != mp3Format.channelCount ||
        wavFormat.sampleType != mp3Format.sampleType ||
        wavFormat.channelLayout != mp3Format.channelLayout) {
        return "FAIL: WAV and MP3 formats do not match";
    }
    mixerSampleRate = wavFormat.sampleRate;

    backend = std::make_unique<Providers::Android::Oboe::OboeBackend>();
    mixer = std::make_unique<AudioMixer>(*backend, 2);
    AudioStreamConfig config;
    config.format = wavFormat;
    const auto openResult = mixer->Open(config);
    if (!openResult.Succeeded()) {
        mixer.reset();
        backend.reset();
        return "FAIL: mixer open: " + openResult.Message();
    }

    const auto addWavResult = mixer->AddSource(std::move(wavReader), wavId);
    if (!addWavResult.Succeeded()) {
        mixer->Close();
        mixer.reset();
        backend.reset();
        return "FAIL: WAV source: " + addWavResult.Message();
    }
    const auto addMp3Result = mixer->AddSource(std::move(mp3Reader), mp3Id);
    if (!addMp3Result.Succeeded()) {
        mixer->Close();
        mixer.reset();
        backend.reset();
        return "FAIL: MP3 source: " + addMp3Result.Message();
    }

    const auto startResult = mixer->Start();
    if (!startResult.Succeeded()) {
        mixer->Close();
        mixer.reset();
        backend.reset();
        return "FAIL: mixer start: " + startResult.Message();
    }
    return "PASS: WAV + MP3 mixer started";
}

std::string StopMixer()
{
    std::lock_guard lock(mixerMutex);
    if (!mixer) {
        return "PASS: mixer is already stopped";
    }

    const auto stopResult = mixer->Stop();
    mixer->Close();
    mixer.reset();
    backend.reset();
    wavId = 0;
    mp3Id = 0;
    mixerSampleRate = 0;
    if (!stopResult.Succeeded()) {
        return "FAIL: mixer stop: " + stopResult.Message();
    }
    return "PASS: mixer stopped";
}

std::string SetPaused(Audio::AudioMixer::SourceId sourceId, bool paused)
{
    std::lock_guard lock(mixerMutex);
    if (!mixer) {
        return "FAIL: mixer is not running";
    }
    const auto result = mixer->SetSourcePaused(sourceId, paused);
    return result.Succeeded() ? "PASS: source updated" : "FAIL: " + result.Message();
}

std::string SetWavPaused(bool paused)
{
    return SetPaused(wavId, paused);
}

std::string SetMp3Paused(bool paused)
{
    return SetPaused(mp3Id, paused);
}

std::string SeekSource(Audio::AudioMixer::SourceId sourceId, std::int64_t seconds)
{
    std::lock_guard lock(mixerMutex);
    if (!mixer) {
        return "FAIL: mixer is not running";
    }
    const auto result = mixer->SeekSourceRelative(
        sourceId, seconds * static_cast<std::int64_t>(mixerSampleRate));
    return result.Succeeded() ? "PASS: source seek queued" : "FAIL: " + result.Message();
}

std::string StopSource(Audio::AudioMixer::SourceId& sourceId)
{
    std::lock_guard lock(mixerMutex);
    if (!mixer) {
        return "FAIL: mixer is not running";
    }
    if (sourceId == 0) {
        return "PASS: source is already stopped";
    }
    const auto result = mixer->RemoveSource(sourceId);
    if (result.Succeeded()) {
        sourceId = 0;
    }
    return result.Succeeded() ? "PASS: source stopped" : "FAIL: " + result.Message();
}

std::string SeekWav(bool forward)
{
    return SeekSource(wavId, forward ? 5 : -5);
}

std::string SeekMp3(bool forward)
{
    return SeekSource(mp3Id, forward ? 5 : -5);
}

std::string StopWav()
{
    return StopSource(wavId);
}

std::string StopMp3()
{
    return StopSource(mp3Id);
}

}

}

namespace {

constexpr const char* kLogTag = "LuAudioAndroidMixerTests";

std::string ReadString(JNIEnv* environment, jstring value)
{
    if (value == nullptr) {
        return {};
    }
    const char* chars = environment->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return {};
    }
    std::string result(chars);
    environment->ReleaseStringUTFChars(value, chars);
    return result;
}

jstring ReturnResult(JNIEnv* environment, const std::string& result)
{
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", result.c_str());
    return environment->NewStringUTF(result.c_str());
}

}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeRunAudioMixerPlaybackTest(
    JNIEnv* environment, jclass, jstring directory)
{
    const auto directoryPath = ReadString(environment, directory);
    if (directoryPath.empty()) {
        return environment->NewStringUTF("FAIL: test directory is null");
    }
    return ReturnResult(environment, LuAudio::AndroidTests::StartMixer(directoryPath));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStopAudioMixer(JNIEnv* environment, jclass)
{
    return ReturnResult(environment, LuAudio::AndroidTests::StopMixer());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSetWavPaused(JNIEnv* environment, jclass, jboolean paused)
{
    return ReturnResult(environment, LuAudio::AndroidTests::SetWavPaused(paused));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSetMp3Paused(JNIEnv* environment, jclass, jboolean paused)
{
    return ReturnResult(environment, LuAudio::AndroidTests::SetMp3Paused(paused));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSeekWav(JNIEnv* environment, jclass, jboolean forward)
{
    return ReturnResult(environment, LuAudio::AndroidTests::SeekWav(forward));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSeekMp3(JNIEnv* environment, jclass, jboolean forward)
{
    return ReturnResult(environment, LuAudio::AndroidTests::SeekMp3(forward));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStopWav(JNIEnv* environment, jclass)
{
    return ReturnResult(environment, LuAudio::AndroidTests::StopWav());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStopMp3(JNIEnv* environment, jclass)
{
    return ReturnResult(environment, LuAudio::AndroidTests::StopMp3());
}

