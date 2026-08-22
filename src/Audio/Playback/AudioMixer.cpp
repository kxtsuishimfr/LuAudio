#include <algorithm>
#include <cmath>

#include <LuAudio/Audio/Playback/AudioMixer.h>

namespace LuAudio::Audio {

namespace {

bool FormatsMatch(const AudioFormat& left, const AudioFormat& right)
{
    return left.sampleRate == right.sampleRate &&
        left.channelCount == right.channelCount &&
        left.sampleType == right.sampleType &&
        left.channelLayout == right.channelLayout;
}

}

AudioMixer::AudioMixer(IAudioBackend& backend, std::size_t maxSources)
    : backend_(backend), maxSources_(maxSources)
{
    sources_.reserve(maxSources_);
}

AudioMixer::~AudioMixer()
{
    Close();
}

Result AudioMixer::Open(const AudioStreamConfig& requestedConfig)
{
    if (open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio mixer is already open");
    }
    if (maxSources_ == 0) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer requires at least one source slot");
    }
    if (!requestedConfig.IsValid()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer requires a valid stream configuration");
    }

    const auto result = backend_.Open(requestedConfig);
    if (!result.Succeeded()) {
        return result;
    }

    const auto actualConfig = backend_.ActualConfig();
    if (!actualConfig.IsValid()) {
        backend_.Close();
        return Result::Failure(ResultCode::BackendUnavailable, "Backend returned an invalid stream configuration");
    }
    if (!FormatsMatch(requestedConfig.format, actualConfig.format) ||
        requestedConfig.framesPerBuffer != actualConfig.framesPerBuffer) {
        backend_.Close();
        return Result::Failure(ResultCode::BackendUnavailable, "Backend configuration does not match the requested mixer configuration");
    }

    {
        std::lock_guard lock(mutex_);
        masterConfig_ = actualConfig;
        open_ = true;
    }
    backend_.SetCallback([this](AudioBuffer& buffer) { Render(buffer); });
    return Result::Success();
}

Result AudioMixer::Start()
{
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio mixer is not open");
    }
    return backend_.Start();
}

Result AudioMixer::Stop()
{
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio mixer is not open");
    }
    return backend_.Stop();
}

void AudioMixer::Close() noexcept
{
    if (!open_) {
        return;
    }

    backend_.Stop();
    backend_.Close();
    backend_.SetCallback({});

    std::vector<std::shared_ptr<Entry>> detachedSources;
    std::shared_ptr<const AudioEffectChain> detachedMasterEffects;
    {
        std::lock_guard lock(mutex_);
        detachedSources.swap(sources_);
        retiredSources_.clear();
        detachedMasterEffects = std::move(masterEffects_);
        open_ = false;
        masterConfig_ = {};
        nextId_ = 0;
    }
}

Result AudioMixer::AddSource(std::unique_ptr<IAudioReader> reader, SourceId& outId)
{
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio mixer is not open");
    }
    if (!reader || !reader->IsOpen()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer requires an open reader");
    }

    const auto sourceFormat = reader->Format();
    if (!sourceFormat.IsValid() || !sourceFormat.CanMixInto(masterConfig_.format)) {
        return Result::Failure(ResultCode::InvalidArgument, "Reader format is not supported by the mixer");
    }

    auto entry = std::make_shared<Entry>();
    entry->reader = std::move(reader);
    entry->sourceScratch = AudioBuffer(sourceFormat, masterConfig_.framesPerBuffer);
    entry->masterScratch = AudioBuffer(masterConfig_.format, masterConfig_.framesPerBuffer);

    std::lock_guard lock(mutex_);
    if (!open_) {
        return Result::Failure(ResultCode::InvalidState, "Audio mixer is not open");
    }
    if (sources_.size() >= maxSources_) {
        return Result::Failure(ResultCode::InvalidState, "Audio mixer has reached its source capacity");
    }

    SourceId id = nextId_++;
    if (id == 0) {
        id = nextId_++;
    }
    entry->id = id;
    outId = id;
    sources_.push_back(std::move(entry));
    return Result::Success();
}

Result AudioMixer::RemoveSource(SourceId id)
{
    std::shared_ptr<Entry> detachedSource;
    {
        std::lock_guard lock(mutex_);
        const auto iterator = std::find_if(sources_.begin(), sources_.end(),
            [id](const auto& source) { return source->id == id; });
        if (iterator == sources_.end()) {
            return Result::Failure(ResultCode::InvalidArgument, "Audio mixer source was not found");
        }

        detachedSource = std::move(*iterator);
        sources_.erase(iterator);
        retiredSources_.push_back(std::move(detachedSource));
    }
    return Result::Success();
}

Result AudioMixer::SeekSource(SourceId id, std::uint64_t frame)
{
    std::lock_guard lock(mutex_);
    Entry* entry = Find(id);
    if (entry == nullptr) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer source was not found");
    }
    if (!entry->reader->CanSeek()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer source does not support seeking");
    }
    if (frame > entry->reader->FrameCount()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer seek position is outside the source");
    }

    entry->pendingSeekFrame = frame;
    entry->seekPending = true;
    return Result::Success();
}

Result AudioMixer::SeekSourceRelative(SourceId id, std::int64_t frameDelta)
{
    std::lock_guard lock(mutex_);
    Entry* entry = Find(id);
    if (entry == nullptr) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer source was not found");
    }
    if (!entry->reader->CanSeek()) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer source does not support seeking");
    }

    const auto current = entry->reader->Position();
    const auto total = entry->reader->FrameCount();
    std::uint64_t target = current;
    if (frameDelta < 0) {
        const auto distance = static_cast<std::uint64_t>(-(frameDelta + 1)) + 1;
        target = distance > current ? 0 : current - distance;
    } else {
        const auto distance = static_cast<std::uint64_t>(frameDelta);
        target = distance > total - current ? total : current + distance;
    }

    entry->pendingSeekFrame = target;
    entry->seekPending = true;
    return Result::Success();
}

Result AudioMixer::SetSourceGain(SourceId id, float gain)
{
    if (!std::isfinite(gain)) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer gain must be finite");
    }

    std::lock_guard lock(mutex_);
    Entry* entry = Find(id);
    if (entry == nullptr) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer source was not found");
    }
    entry->gain = gain;
    return Result::Success();
}

Result AudioMixer::SetSourcePaused(SourceId id, bool paused)
{
    std::lock_guard lock(mutex_);
    Entry* entry = Find(id);
    if (entry == nullptr) {
        return Result::Failure(ResultCode::InvalidArgument, "Audio mixer source was not found");
    }
    entry->paused = paused;
    return Result::Success();
}

bool AudioMixer::IsSourceFinished(SourceId id) const
{
    std::lock_guard lock(mutex_);
    const Entry* entry = Find(id);
    return entry != nullptr && entry->reader->EndOfFile();
}

std::uint64_t AudioMixer::SourcePosition(SourceId id) const noexcept
{
    std::lock_guard lock(mutex_);
    const Entry* entry = Find(id);
    return entry == nullptr ? 0 : entry->renderedPosition.load(std::memory_order_acquire);
}

std::size_t AudioMixer::ActiveSourceCount() const noexcept
{
    std::lock_guard lock(mutex_);
    return sources_.size();
}

Result AudioMixer::SetSourceEffects(SourceId id, std::shared_ptr<const AudioEffectChain> chain)
{
    std::shared_ptr<const AudioEffectChain> previousChain;
    {
        std::lock_guard lock(mutex_);
        Entry* entry = Find(id);
        if (entry == nullptr) {
            return Result::Failure(ResultCode::InvalidArgument, "Audio mixer source was not found");
        }
        previousChain = std::move(entry->effects);
        entry->effects = std::move(chain);
    }
    return Result::Success();
}

void AudioMixer::SetMasterEffectChain(std::shared_ptr<const AudioEffectChain> chain) noexcept
{
    std::shared_ptr<const AudioEffectChain> previousChain;
    {
        std::lock_guard lock(mutex_);
        previousChain = std::move(masterEffects_);
        masterEffects_ = std::move(chain);
    }
}

AudioMixer::Entry* AudioMixer::Find(SourceId id) noexcept
{
    const auto iterator = std::find_if(sources_.begin(), sources_.end(),
        [id](const auto& source) { return source->id == id; });
    return iterator == sources_.end() ? nullptr : iterator->get();
}

const AudioMixer::Entry* AudioMixer::Find(SourceId id) const noexcept
{
    const auto iterator = std::find_if(sources_.begin(), sources_.end(),
        [id](const auto& source) { return source->id == id; });
    return iterator == sources_.end() ? nullptr : iterator->get();
}

}
