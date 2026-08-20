#include <android/log.h>
#include <jni.h>

#include <memory>
#include <mutex>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

#include <LuAudio/LuAudio.h>

namespace LuAudio::AndroidTests {

namespace {

std::mutex exportMutex;
std::unique_ptr<Audio::AudioMixer> mixer;
std::unique_ptr<Providers::Android::Oboe::OboeBackend> mixerBackend;
Audio::AudioMixer::SourceId wavId = 0;
Audio::AudioMixer::SourceId mp3Id = 0;
std::shared_ptr<const Audio::AudioEffectChain> wavPlaybackEffects;
std::shared_ptr<const Audio::AudioEffectChain> mp3PlaybackEffects;
Audio::AudioFormat wavFormat;
Audio::AudioFormat mp3Format;
std::thread exportThread;
std::atomic<int> exportState = 0;
std::string exportStatus = "Export idle";
std::string exportDirectory;
std::string exportPluginPath;
std::string exportOutputPath;
bool wavReverbEnabled = true;
bool mp3ReverbEnabled = true;

void SetExportStatus(int state, std::string status)
{
    exportState.store(state, std::memory_order_release);
    std::lock_guard lock(exportMutex);
    exportStatus = std::move(status);
}

Audio::Result OpenAudio(const std::string& path, std::unique_ptr<Audio::IAudioReader>& reader)
{
    if (std::filesystem::path(path).extension() == ".wav") {
        auto wavReader = std::make_unique<Audio::WavFileReader>();
        const auto result = wavReader->Open(Audio::AudioFile(path, Audio::AudioFileType::Wav));
        if (result.Succeeded()) {
            reader = std::move(wavReader);
        }
        return result;
    }

    auto mp3Reader = std::make_unique<Audio::Mp3FileReader>(
        std::make_unique<Providers::Android::AAudioDecoder>());
    const auto result = mp3Reader->Open(Audio::AudioFile(path, Audio::AudioFileType::Mp3));
    if (result.Succeeded()) {
        reader = std::move(mp3Reader);
    }
    return result;
}

Audio::Result CreateHallChain(
    const std::string& pluginPath,
    const Audio::AudioFormat& format,
    Audio::AudioEffectChain& chain)
{
    Providers::Android::APluginProvider provider;
    Plugins::PluginManager manager(provider);
    std::unique_ptr<Plugins::PluginHandle> plugin;
    const auto loadResult = manager.Load(pluginPath, plugin);
    if (!loadResult.Succeeded()) {
        return loadResult;
    }

    auto effect = std::make_unique<Plugins::PluginEffectAdapter>(
        std::move(plugin),
        Plugins::PluginInstanceConfig{format.sampleRate, format.channelCount, 4096});
    if (!effect->IsReady()) {
        return Audio::Result::Failure(
            Audio::ResultCode::ProcessingFailed,
            "Hall reverb plugin failed to initialize");
    }
    effect->SetParameter("room_size", 0.92F);
    effect->SetParameter("damping", 0.28F);
    effect->SetParameter("wet", 0.31F);
    effect->SetParameter("width", 0.9F);
    chain.Add(std::move(effect));
    return Audio::Result::Success();
}

std::unique_ptr<Audio::IAudioReader> OpenReader(
    const std::string& path, Audio::Result& result)
{
    std::unique_ptr<Audio::IAudioReader> reader;
    result = OpenAudio(path, reader);
    return reader;
}

std::shared_ptr<const Audio::AudioEffectChain> MakeEffects(
    const std::string& pluginPath, const Audio::AudioFormat& format, bool enabled, Audio::Result& result)
{
    if (!enabled) {
        result = Audio::Result::Success();
        return nullptr;
    }
    auto chain = std::make_shared<Audio::AudioEffectChain>();
    result = CreateHallChain(pluginPath, format, *chain);
    return result.Succeeded() ? chain : nullptr;
}

std::string StartPlayback(
    const std::string& directory, const std::string& pluginPath, bool wavReverb, bool mp3Reverb)
{
    std::lock_guard lock(exportMutex);
    if (mixer) {
        return "FAIL: audio is already playing";
    }

    const std::string wavPath = directory + "/sample_1.wav";
    const std::string mp3Path = directory + "/sample_2.mp3";
    Audio::Result wavResult = Audio::Result::Success();
    Audio::Result mp3Result = Audio::Result::Success();
    auto wavReader = OpenReader(wavPath, wavResult);
    auto mp3Reader = OpenReader(mp3Path, mp3Result);
    if (!wavResult.Succeeded()) {
        return "FAIL: WAV open: " + wavResult.Message();
    }
    if (!mp3Result.Succeeded()) {
        return "FAIL: MP3 open: " + mp3Result.Message();
    }
    if (wavReader->Format().sampleRate != mp3Reader->Format().sampleRate ||
        wavReader->Format().channelCount != mp3Reader->Format().channelCount) {
        return "FAIL: samples have incompatible formats";
    }
    const Audio::AudioFormat wavSourceFormat = wavReader->Format();
    const Audio::AudioFormat mp3SourceFormat = mp3Reader->Format();

    auto backend = std::make_unique<Providers::Android::Oboe::OboeBackend>();
    auto newMixer = std::make_unique<Audio::AudioMixer>(*backend, 2);
    Audio::AudioStreamConfig config;
    config.format = wavReader->Format();
    const auto openResult = newMixer->Open(config);
    if (!openResult.Succeeded()) {
        return "FAIL: Oboe open: " + openResult.Message();
    }
    const auto wavEffects = MakeEffects(pluginPath, wavReader->Format(), wavReverb, wavResult);
    const auto mp3Effects = MakeEffects(pluginPath, mp3Reader->Format(), mp3Reverb, mp3Result);
    if (!wavResult.Succeeded() || !mp3Result.Succeeded()) {
        return "FAIL: effect setup";
    }
    const auto addWav = newMixer->AddSource(std::move(wavReader), wavId);
    const auto addMp3 = newMixer->AddSource(std::move(mp3Reader), mp3Id);
    if (!addWav.Succeeded() || !addMp3.Succeeded()) {
        return "FAIL: mixer source setup";
    }
    newMixer->SetSourceEffects(wavId, wavEffects);
    newMixer->SetSourceEffects(mp3Id, mp3Effects);
    const auto startResult = newMixer->Start();
    if (!startResult.Succeeded()) {
        return "FAIL: Oboe start: " + startResult.Message();
    }

    exportDirectory = directory;
    exportPluginPath = pluginPath;
    wavReverbEnabled = wavReverb;
    mp3ReverbEnabled = mp3Reverb;
    wavFormat = wavSourceFormat;
    mp3Format = mp3SourceFormat;
    wavPlaybackEffects = wavEffects;
    mp3PlaybackEffects = mp3Effects;
    mixerBackend = std::move(backend);
    mixer = std::move(newMixer);
    exportState.store(0, std::memory_order_release);
    exportStatus = "Playing sample_1.wav + sample_2.mp3";
    return "PASS: both samples playing";
}

std::string SetWavReverb(bool enabled)
{
    std::lock_guard lock(exportMutex);
    if (!mixer) return "FAIL: audio is not playing";
    Audio::Result result = Audio::Result::Success();
    if (enabled && !wavPlaybackEffects) {
        wavPlaybackEffects = MakeEffects(exportPluginPath, wavFormat, true, result);
        if (!result.Succeeded()) return "FAIL: WAV effect setup: " + result.Message();
    }
    result = mixer->SetSourceEffects(wavId, enabled ? wavPlaybackEffects : nullptr);
    if (!result.Succeeded()) return "FAIL: " + result.Message();
    wavReverbEnabled = enabled;
    return enabled ? "PASS: WAV reverb enabled" : "PASS: WAV reverb disabled";
}

std::string SetMp3Reverb(bool enabled)
{
    std::lock_guard lock(exportMutex);
    if (!mixer) return "FAIL: audio is not playing";
    Audio::Result result = Audio::Result::Success();
    if (enabled && !mp3PlaybackEffects) {
        mp3PlaybackEffects = MakeEffects(exportPluginPath, mp3Format, true, result);
        if (!result.Succeeded()) return "FAIL: MP3 effect setup: " + result.Message();
    }
    result = mixer->SetSourceEffects(mp3Id, enabled ? mp3PlaybackEffects : nullptr);
    if (!result.Succeeded()) return "FAIL: " + result.Message();
    mp3ReverbEnabled = enabled;
    return enabled ? "PASS: MP3 reverb enabled" : "PASS: MP3 reverb disabled";
}

std::string SetPlaybackPaused(bool paused)
{
    std::lock_guard lock(exportMutex);
    if (!mixer) {
        return "FAIL: audio is not playing";
    }
    const auto wavResult = mixer->SetSourcePaused(wavId, paused);
    const auto mp3Result = mixer->SetSourcePaused(mp3Id, paused);
    const auto result = wavResult.Succeeded() ? mp3Result : wavResult;
    if (!result.Succeeded()) {
        return "FAIL: " + result.Message();
    }
    return paused ? "PASS: playback paused" : "PASS: playback resumed";
}

std::string StartOfflineExport(const std::string& outputPath)
{
    if (exportThread.joinable()) {
        exportThread.join();
    }

    std::lock_guard lock(exportMutex);
    if (!mixer) return "FAIL: start playback before exporting";
    if (exportState.load(std::memory_order_acquire) == 1) {
        return "FAIL: export is already running";
    }

    exportOutputPath = outputPath;
    const auto directory = exportDirectory;
    const auto pluginPath = exportPluginPath;
    exportState.store(1, std::memory_order_release);
    exportStatus = "Export started";
    const bool wavReverb = wavReverbEnabled;
    const bool mp3Reverb = mp3ReverbEnabled;
    exportThread = std::thread([directory, pluginPath, outputPath, wavReverb, mp3Reverb] {
        Audio::Result wavResult = Audio::Result::Success();
        Audio::Result mp3Result = Audio::Result::Success();
        auto wavReader = OpenReader(directory + "/sample_1.wav", wavResult);
        auto mp3Reader = OpenReader(directory + "/sample_2.mp3", mp3Result);
        if (!wavResult.Succeeded() || !mp3Result.Succeeded()) {
            SetExportStatus(3, "Export failed: unable to open both samples");
            return;
        }
        auto wavEffects = MakeEffects(pluginPath, wavReader->Format(), wavReverb, wavResult);
        auto mp3Effects = MakeEffects(pluginPath, mp3Reader->Format(), mp3Reverb, mp3Result);
        if (!wavResult.Succeeded() || !mp3Result.Succeeded()) {
            SetExportStatus(3, "Export effect setup failed");
            return;
        }
        class CombinedReader final : public Audio::IAudioReader {
        public:
            CombinedReader(std::unique_ptr<Audio::IAudioReader> first,
                std::unique_ptr<Audio::IAudioReader> second,
                std::shared_ptr<const Audio::AudioEffectChain> firstEffects,
                std::shared_ptr<const Audio::AudioEffectChain> secondEffects)
                : first_(std::move(first)), second_(std::move(second)),
                  firstEffects_(std::move(firstEffects)), secondEffects_(std::move(secondEffects)),
                  format_(first_->Format()), frameCount_(std::max(first_->FrameCount(), second_->FrameCount()))
            {
            }
            Audio::Result Read(Audio::AudioBuffer& destination) override
            {
                destination.Clear();
                const auto readOne = [&](Audio::IAudioReader& reader,
                    std::shared_ptr<const Audio::AudioEffectChain> effects) {
                    if (reader.EndOfFile()) return;
                    Audio::AudioBuffer source(format_, destination.FrameCount());
                    if (!reader.ReadFully(source).Succeeded()) return;
                    if (effects && !effects->Process(source)) return;
                    for (std::size_t i = 0; i < source.SampleCount(); ++i) {
                        destination.Data()[i] += source.Data()[i];
                    }
                };
                readOne(*first_, firstEffects_);
                readOne(*second_, secondEffects_);
                position_ += destination.FrameCount();
                return Audio::Result::Success();
            }
            Audio::Result Seek(std::uint64_t frame) override { first_->Seek(frame); second_->Seek(frame); position_ = frame; return Audio::Result::Success(); }
            Audio::Result Rewind() override { return Seek(0); }
            std::uint64_t Position() const noexcept override { return position_; }
            bool EndOfFile() const noexcept override { return position_ >= frameCount_; }
            bool IsOpen() const noexcept override { return first_->IsOpen() && second_->IsOpen(); }
            const Audio::AudioFormat& Format() const noexcept override { return format_; }
            std::uint64_t FrameCount() const noexcept override { return frameCount_; }
            std::uint64_t FramesRemaining() const noexcept override { return position_ < frameCount_ ? frameCount_ - position_ : 0; }
            bool CanSeek() const noexcept override { return first_->CanSeek() && second_->CanSeek(); }
        private:
            std::unique_ptr<Audio::IAudioReader> first_, second_;
            std::shared_ptr<const Audio::AudioEffectChain> firstEffects_, secondEffects_;
            Audio::AudioFormat format_;
            std::uint64_t frameCount_, position_ = 0;
        } combined(std::move(wavReader), std::move(mp3Reader), std::move(wavEffects), std::move(mp3Effects));
        Audio::WavFileWriter writer(outputPath);
        const auto renderResult = Audio::OfflineRenderer::Render(combined, writer, nullptr, 4096);
        if (!renderResult.Succeeded()) {
            SetExportStatus(3, "Export failed: " + renderResult.Message());
            return;
        }
        SetExportStatus(2, "Export finished: " + outputPath);
    });
    return "PASS: export started";
}

std::string StopExportPlayback()
{
    {
        std::lock_guard lock(exportMutex);
        if (mixer) {
            mixer->Close();
            mixer.reset();
        }
        mixerBackend.reset();
        wavPlaybackEffects.reset();
        mp3PlaybackEffects.reset();
        wavId = 0;
        mp3Id = 0;
    }
    if (exportThread.joinable()) {
        exportThread.join();
    }
    {
        std::lock_guard lock(exportMutex);
        exportState.store(0, std::memory_order_release);
        exportStatus = "Export idle";
    }
    return "PASS: playback stopped";
}

std::string ExportStatus()
{
    std::lock_guard lock(exportMutex);
    return exportStatus;
}

}

}

namespace {

constexpr const char* kLogTag = "LuAudioAndroidExportTests";

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
Java_com_luaudio_androidtests_MainActivity_nativeStartPlayback(
    JNIEnv* environment, jclass, jstring directory, jstring pluginPath,
    jboolean wavReverb, jboolean mp3Reverb)
{
    return ReturnResult(environment, LuAudio::AndroidTests::StartPlayback(
        ReadString(environment, directory), ReadString(environment, pluginPath), wavReverb, mp3Reverb));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSetPlaybackPaused(
    JNIEnv* environment, jclass, jboolean paused)
{
    return ReturnResult(environment, LuAudio::AndroidTests::SetPlaybackPaused(paused));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSetWavReverb(
    JNIEnv* environment, jclass, jboolean enabled)
{
    return ReturnResult(environment, LuAudio::AndroidTests::SetWavReverb(enabled));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeSetMp3Reverb(
    JNIEnv* environment, jclass, jboolean enabled)
{
    return ReturnResult(environment, LuAudio::AndroidTests::SetMp3Reverb(enabled));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStartOfflineExport(
    JNIEnv* environment, jclass, jstring outputPath)
{
    return ReturnResult(environment, LuAudio::AndroidTests::StartOfflineExport(
        ReadString(environment, outputPath)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeStopPlayback(JNIEnv* environment, jclass)
{
    return ReturnResult(environment, LuAudio::AndroidTests::StopExportPlayback());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_luaudio_androidtests_MainActivity_nativeGetExportStatus(JNIEnv* environment, jclass)
{
    return ReturnResult(environment, LuAudio::AndroidTests::ExportStatus());
}

