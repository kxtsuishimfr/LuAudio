#include <utility>

#include <LuAudio/Providers/Windows/Wasapi/WasapiBackend.h>
#include <LuAudio/Utils/Diagnostics/Log.h>

namespace LuAudio::Providers::Windows::Wasapi {

WasapiBackend::WasapiBackend()
    : device_(std::make_unique<WasapiDevice>())
{
    Utils::Log::Info("WASAPI backend created");
}

WasapiBackend::~WasapiBackend() = default;

Audio::Result WasapiBackend::Open(const Audio::AudioStreamConfig& requestedConfig)
{
    Utils::Log::Debug("Opening WASAPI backend");
    return device_->Open(requestedConfig);
}

Audio::Result WasapiBackend::Start()
{
    Utils::Log::Info("Starting WASAPI backend");
    return device_->Start(callback_);
}

Audio::Result WasapiBackend::Recover()
{
    return device_->Recover();
}

Audio::Result WasapiBackend::Stop()
{
    Utils::Log::Debug("Stopping WASAPI backend");
    return device_->Stop();
}

void WasapiBackend::Close() noexcept
{
    Utils::Log::Info("Closing WASAPI backend");
    device_->Close();
}

void WasapiBackend::SetCallback(Audio::AudioCallback callback)
{
    Utils::Log::Trace("WASAPI audio callback updated");
    callback_ = std::move(callback);
}

const Audio::AudioStreamConfig& WasapiBackend::ActualConfig() const noexcept
{
    return device_->ActualConfig();
}

}
