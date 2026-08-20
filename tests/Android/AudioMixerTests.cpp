#include <cmath>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <LuAudio/Audio/Playback/AudioMixer.h>

namespace LuAudio::AndroidTests {

namespace {

using namespace LuAudio::Audio;

class TestReader final : public IAudioReader {
public:
    explicit TestReader(AudioFormat format = {})
        : format_(format)
    {
    }

    Result Read(AudioBuffer& destination) override
    {
        ++readCount_;
        if (readFailure_) {
            return Result::Failure(ResultCode::ProcessingFailed, "test read failed");
        }
        for (std::size_t index = 0; index < destination.SampleCount(); ++index) {
            destination.Data()[index] = sampleValue_;
        }
        return Result::Success();
    }

    Result Seek(std::uint64_t frame) override
    {
        ++seekCount_;
        position_ = frame;
        return Result::Success();
    }

    Result Rewind() override
    {
        position_ = 0;
        return Result::Success();
    }

    std::uint64_t Position() const noexcept override { return position_; }
    bool EndOfFile() const noexcept override { return endOfFile_; }
    bool IsOpen() const noexcept override { return true; }
    const AudioFormat& Format() const noexcept override { return format_; }
    std::uint64_t FrameCount() const noexcept override { return 64; }
    std::uint64_t FramesRemaining() const noexcept override
    {
        return position_ < FrameCount() ? FrameCount() - position_ : 0;
    }
    bool CanSeek() const noexcept override { return canSeek_; }

    void SetCanSeek(bool value) noexcept { canSeek_ = value; }
    void SetEndOfFile(bool value) noexcept { endOfFile_ = value; }
    void SetSampleValue(float value) noexcept { sampleValue_ = value; }
    std::size_t ReadCount() const noexcept { return readCount_; }
    std::size_t SeekCount() const noexcept { return seekCount_; }

private:
    AudioFormat format_;
    std::uint64_t position_ = 0;
    bool canSeek_ = true;
    bool endOfFile_ = false;
    bool readFailure_ = false;
    float sampleValue_ = 0.0F;
    std::size_t readCount_ = 0;
    std::size_t seekCount_ = 0;
};

class TestBackend final : public IAudioBackend {
public:
    Result Open(const AudioStreamConfig& requestedConfig) override
    {
        if (!requestedConfig.IsValid()) {
            return Result::Failure(ResultCode::InvalidArgument, "invalid configuration");
        }
        actualConfig_ = requestedConfig;
        open_ = true;
        return Result::Success();
    }

    Result Start() override
    {
        if (!open_) {
            return Result::Failure(ResultCode::InvalidState, "backend is not open");
        }
        started_ = true;
        return Result::Success();
    }

    Result Stop() override
    {
        started_ = false;
        return Result::Success();
    }

    void Close() noexcept override
    {
        started_ = false;
        open_ = false;
    }

    void SetCallback(AudioCallback callback) override { callback_ = std::move(callback); }
    const AudioStreamConfig& ActualConfig() const noexcept override { return actualConfig_; }
    bool HasCallback() const noexcept { return static_cast<bool>(callback_); }

    void Render(AudioBuffer& buffer)
    {
        if (callback_) {
            callback_(buffer);
        }
    }

private:
    AudioStreamConfig actualConfig_;
    AudioCallback callback_;
    bool open_ = false;
    bool started_ = false;
};

class MultiplyEffect final : public IAudioEffect {
public:
    explicit MultiplyEffect(float multiplier)
        : multiplier_(multiplier)
    {
    }

    bool Process(AudioBuffer& buffer) noexcept override
    {
        for (std::size_t index = 0; index < buffer.SampleCount(); ++index) {
            buffer.Data()[index] *= multiplier_;
        }
        return true;
    }

private:
    float multiplier_;
};

class TestState {
public:
    void Check(bool condition, const char* description)
    {
        ++checks_;
        if (!condition) {
            ++failures_;
            if (!failureText_.empty()) {
                failureText_ += ", ";
            }
            failureText_ += description;
        }
    }

    std::string Summary() const
    {
        std::ostringstream output;
        output << (failures_ == 0 ? "PASS" : "FAIL")
               << ": " << checks_ << " checks, " << failures_ << " failures";
        if (!failureText_.empty()) {
            output << "\n" << failureText_;
        }
        return output.str();
    }

    bool Passed() const noexcept { return failures_ == 0; }

private:
    std::size_t checks_ = 0;
    std::size_t failures_ = 0;
    std::string failureText_;
};

std::unique_ptr<TestReader> MakeReader(AudioFormat format = {})
{
    return std::make_unique<TestReader>(format);
}

void TestLifecycle(TestState& state)
{
    TestBackend backend;
    AudioMixer mixer(backend, 2);
    AudioMixer::SourceId sourceId = 0;
    state.Check(mixer.Start().Code() == ResultCode::InvalidState, "start before open");
    state.Check(mixer.AddSource(MakeReader(), sourceId).Code() == ResultCode::InvalidState, "add before open");
    state.Check(mixer.Open({}).Succeeded(), "open succeeds");
    state.Check(backend.HasCallback(), "callback registered");
    state.Check(mixer.Start().Succeeded(), "start succeeds");
    mixer.Close();
    state.Check(!backend.HasCallback(), "callback cleared on close");
    mixer.Close();
}

void TestMixing(TestState& state)
{
    TestBackend backend;
    AudioMixer mixer(backend, 2);
    state.Check(mixer.Open({}).Succeeded(), "mixing open");

    auto mono = MakeReader(AudioFormat{48000, 1});
    mono->SetSampleValue(0.25F);
    auto stereo = MakeReader();
    stereo->SetSampleValue(0.5F);
    AudioMixer::SourceId monoId = 0;
    AudioMixer::SourceId stereoId = 0;
    state.Check(mixer.AddSource(std::move(mono), monoId).Succeeded(), "add mono");
    state.Check(mixer.AddSource(std::move(stereo), stereoId).Succeeded(), "add stereo");
    state.Check(mixer.SetSourceGain(stereoId, 0.5F).Succeeded(), "set gain");

    AudioBuffer output({}, 2);
    backend.Render(output);
    const float expected = 0.5F;
    for (std::size_t index = 0; index < output.SampleCount(); ++index) {
        state.Check(std::fabs(output.Data()[index] - expected) < 0.00001F, "mixed sample");
    }
}

void TestPauseSeekAndEffects(TestState& state)
{
    TestBackend backend;
    AudioMixer mixer(backend, 1);
    state.Check(mixer.Open({}).Succeeded(), "control open");
    auto reader = MakeReader();
    reader->SetSampleValue(0.25F);
    TestReader* readerPointer = reader.get();
    AudioMixer::SourceId sourceId = 0;
    state.Check(mixer.AddSource(std::move(reader), sourceId).Succeeded(), "control add");
    state.Check(mixer.SetSourcePaused(sourceId, true).Succeeded(), "pause source");
    AudioBuffer output({}, 2);
    backend.Render(output);
    state.Check(readerPointer->ReadCount() == 0, "paused source not read");
    state.Check(mixer.SetSourcePaused(sourceId, false).Succeeded(), "resume source");
    state.Check(mixer.SeekSource(sourceId, 12).Succeeded(), "queue seek");

    auto sourceEffects = std::make_shared<AudioEffectChain>();
    sourceEffects->Add(std::make_unique<MultiplyEffect>(2.0F));
    state.Check(mixer.SetSourceEffects(sourceId, sourceEffects).Succeeded(), "set source effects");
    auto masterEffects = std::make_shared<AudioEffectChain>();
    masterEffects->Add(std::make_unique<MultiplyEffect>(2.0F));
    mixer.SetMasterEffectChain(masterEffects);
    backend.Render(output);
    state.Check(readerPointer->SeekCount() == 1, "seek applied before read");
    state.Check(readerPointer->Position() == 12, "seek position applied");
    state.Check(std::fabs(output.Data()[0] - 1.0F) < 0.00001F, "effects and mixing");
    readerPointer->SetCanSeek(false);
    state.Check(mixer.SeekSource(sourceId, 12).Code() == ResultCode::InvalidArgument, "nonseekable rejection");
    readerPointer->SetEndOfFile(true);
    state.Check(mixer.IsSourceFinished(sourceId), "eof polling");
}

void TestCapacity(TestState& state)
{
    TestBackend backend;
    AudioMixer mixer(backend, 2);
    state.Check(mixer.Open({}).Succeeded(), "capacity open");
    AudioMixer::SourceId first = 0;
    AudioMixer::SourceId second = 0;
    AudioMixer::SourceId third = 0;
    state.Check(mixer.AddSource(MakeReader(), first).Succeeded(), "first source");
    state.Check(mixer.AddSource(MakeReader(), second).Succeeded(), "second source");
    state.Check(first != second, "distinct source ids");
    state.Check(mixer.AddSource(MakeReader(), third).Code() == ResultCode::InvalidState, "capacity rejection");
    state.Check(mixer.RemoveSource(first).Succeeded(), "remove source");
    state.Check(mixer.AddSource(MakeReader(), third).Succeeded(), "reuse capacity");
}

}

std::string RunAudioMixerTests()
{
    TestState state;
    TestLifecycle(state);
    TestMixing(state);
    TestPauseSeekAndEffects(state);
    TestCapacity(state);
    return state.Summary();
}

}
