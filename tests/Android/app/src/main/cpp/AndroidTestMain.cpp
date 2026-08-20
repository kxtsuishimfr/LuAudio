#if defined(__ANDROID__)
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <android/log.h>
#include <jni.h>

#include <LuAudio/LuAudio.h>

namespace {

constexpr const char* kLogTag = "LuAudioAndroidPlaybackTests";
constexpr const char* kAudioPath = "/sdcard/LuAudio_Tests/sample_1.wav";

std::mutex sessionMutex;
std::unique_ptr<LuAudio::Providers::Android::Oboe::OboeBackend> backend;
std::unique_ptr<LuAudio::Audio::AudioEffectChain> effects;
std::unique_ptr<LuAudio::Audio::AudioPlayer> player;

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

SmallHallReverb* reverb = nullptr;

std::string StatusTextLocked()
{
    if (!player) {
        return "Player is not open.";
    }
    std::ostringstream output;
    output << "Applied: " << player->Position()
           << "\nRequested: " << player->RequestedPosition()
           << "\nTotal: " << player->FrameCount()
           << "\nEOF: " << (player->EndOfFile() ? "yes" : "no");
    return output.str();
}

std::string StartPlaybackLocked()
{
    if (player) {
        return "Playback is already running.\n\n" + StatusTextLocked();
    }

    auto reader = std::make_unique<LuAudio::Audio::WavFileReader>();
    const auto fileResult = reader->Open(kAudioPath);
    if (!fileResult.Succeeded()) {
        return "Open sample_1.wav: " + fileResult.Message();
    }

    effects = std::make_unique<LuAudio::Audio::AudioEffectChain>();
    auto reverbEffect = std::make_unique<SmallHallReverb>(reader->Format());
    reverb = reverbEffect.get();
    effects->Add(std::move(reverbEffect));

    backend = std::make_unique<LuAudio::Providers::Android::Oboe::OboeBackend>();
    player = std::make_unique<LuAudio::Audio::AudioPlayer>(*backend);
    player->SetEffectChain(effects.get());

    LuAudio::Audio::AudioStreamConfig config;
    config.format = reader->Format();
    const auto openResult = player->Open(std::move(reader), config);
    if (!openResult.Succeeded()) {
        player.reset();
        backend.reset();
        effects.reset();
        reverb = nullptr;
        return "Open player: " + openResult.Message();
    }

    const auto startResult = player->Start();
    if (!startResult.Succeeded()) {
        player->Close();
        player.reset();
        backend.reset();
        effects.reset();
        reverb = nullptr;
        return "Start playback: " + startResult.Message();
    }
    return "Playing sample_1.wav\nReverb: on\n\n" + StatusTextLocked();
}

std::string RunPluginTests(const std::string& pluginPath)
{
    LuAudio::Providers::Android::APluginProvider provider;
    LuAudio::Plugins::PluginManager manager(provider);
    std::unique_ptr<LuAudio::Plugins::PluginHandle> plugin;
    const auto loadResult = manager.Load(pluginPath, plugin);
    if (!loadResult.Succeeded()) {
        return "Plugin test failed: " + loadResult.Message();
    }
    return plugin && std::string(plugin->Name()) == "AndroidFixturePlugin"
        ? "Plugin test passed"
        : "Plugin test failed: invalid descriptor";
}

}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStartPlayback(JNIEnv* environment, jclass)
{
    std::lock_guard lock(sessionMutex);
    const auto result = StartPlaybackLocked();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", result.c_str());
    return environment->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeToggleReverb(JNIEnv* environment, jclass, jboolean enabled)
{
    std::lock_guard lock(sessionMutex);
    if (!reverb) {
        return environment->NewStringUTF("Player is not open.");
    }
    reverb->SetEnabled(enabled == JNI_TRUE);
    const std::string result = enabled == JNI_TRUE ? "Reverb: on" : "Reverb: off";
    return environment->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeTogglePause(JNIEnv* environment, jclass)
{
    std::lock_guard lock(sessionMutex);
    if (!player) {
        return environment->NewStringUTF("Player is not open.");
    }
    const auto result = player->IsPaused() ? player->Resume() : player->Pause();
    const std::string text = result.Succeeded()
        ? (player->IsPaused() ? "Paused" : "Playing")
        : result.Message();
    return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSeek(JNIEnv* environment, jclass, jlong frame)
{
    std::lock_guard lock(sessionMutex);
    if (!player) {
        return environment->NewStringUTF("Player is not open.");
    }
    const auto target = frame == -1 ? player->FrameCount() / 2
        : frame == -2 ? player->FrameCount()
        : static_cast<std::uint64_t>(std::max<jlong>(frame, 0));
    const auto result = player->Seek(target);
    const std::string text = result.Succeeded() ? StatusTextLocked() : result.Message();
    return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSeekRelative(JNIEnv* environment, jclass, jlong seconds)
{
    std::lock_guard lock(sessionMutex);
    if (!player) {
        return environment->NewStringUTF("Player is not open.");
    }
    const auto current = player->Position();
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
    const auto result = player->Seek(target);
    const std::string text = result.Succeeded() ? StatusTextLocked() : result.Message();
    return environment->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStatus(JNIEnv* environment, jclass)
{
    std::lock_guard lock(sessionMutex);
    const auto result = StatusTextLocked();
    return environment->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeRunPluginTests(JNIEnv* environment, jclass, jstring path)
{
    const char* pluginPath = environment->GetStringUTFChars(path, nullptr);
    const auto result = RunPluginTests(pluginPath ? pluginPath : "");
    if (pluginPath) {
        environment->ReleaseStringUTFChars(path, pluginPath);
    }
    return environment->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStopPlayback(JNIEnv*, jclass)
{
    std::lock_guard lock(sessionMutex);
    if (player) {
        player->Close();
        player.reset();
        backend.reset();
        effects.reset();
        reverb = nullptr;
    }
}
#endif
