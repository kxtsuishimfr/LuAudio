#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <utility>

#include <LuAudio/Providers/Windows/Wasapi/WasapiDevice.h>
#include <LuAudio/Utils/Diagnostics/Log.h>

#ifdef _WIN32
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace LuAudio::Providers::Windows::Wasapi {

#ifdef _WIN32
namespace {

using Microsoft::WRL::ComPtr;

Audio::Result BackendFailure(HRESULT result, const char* operation)
{
    return Audio::Result::Failure(
        Audio::ResultCode::BackendUnavailable,
        std::string(operation) + " failed with HRESULT " + std::to_string(static_cast<unsigned long>(result)));
}

bool IsFloat32(const WAVEFORMATEX& format)
{
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return format.wBitsPerSample == 32;
    }

    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        return extensible.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && format.wBitsPerSample == 32;
    }

    return false;
}

}
#endif

class WasapiDevice::Implementation {
public:
#ifdef _WIN32
    ~Implementation()
    {
        Close();
    }

    Audio::Result Open(const Audio::AudioStreamConfig& requestedConfig)
    {
        Utils::Log::Debug("Opening WASAPI device");
        if (!requestedConfig.IsValid()) {
            Utils::Log::Info("WASAPI device rejected an invalid configuration");
            return Audio::Result::Failure(Audio::ResultCode::InvalidArgument, "Invalid WASAPI configuration");
        }

        Close();
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult)) {
            return BackendFailure(comResult, "CoInitializeEx");
        }
        comInitialized_ = true;

        HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&deviceEnumerator_));
        if (FAILED(result)) {
            Close();
            return BackendFailure(result, "CoCreateInstance");
        }

        result = deviceEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
        if (FAILED(result)) {
            Close();
            return BackendFailure(result, "GetDefaultAudioEndpoint");
        }

        result = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient_);
        if (FAILED(result)) {
            Close();
            return BackendFailure(result, "IMMDevice::Activate");
        }

        WAVEFORMATEX* mixFormat = nullptr;
        result = audioClient_->GetMixFormat(&mixFormat);
        if (FAILED(result)) {
            Close();
            return BackendFailure(result, "IAudioClient::GetMixFormat");
        }

        if (!IsFloat32(*mixFormat)) {
            CoTaskMemFree(mixFormat);
            Close();
            return Audio::Result::Failure(
                Audio::ResultCode::BackendUnavailable,
                "The default WASAPI endpoint does not expose 32-bit float audio");
        }

        actualConfig_.format.sampleRate = mixFormat->nSamplesPerSec;
        actualConfig_.format.channelCount = mixFormat->nChannels;
        actualConfig_.framesPerBuffer = requestedConfig.framesPerBuffer;
        actualConfig_.exclusiveMode = false;

        const REFERENCE_TIME duration =
            static_cast<REFERENCE_TIME>(actualConfig_.framesPerBuffer) * 10'000'000 /
            actualConfig_.format.sampleRate;
        result = audioClient_->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            duration,
            0,
            mixFormat,
            nullptr);
        CoTaskMemFree(mixFormat);
        if (FAILED(result)) {
            Close();
            return BackendFailure(result, "IAudioClient::Initialize");
        }

        result = audioClient_->GetBufferSize(&bufferFrameCount_);
        if (FAILED(result)) {
            Close();
            return BackendFailure(result, "IAudioClient::GetBufferSize");
        }

        result = audioClient_->GetService(IID_PPV_ARGS(&renderClient_));
        if (FAILED(result)) {
            Close();
            return BackendFailure(result, "IAudioClient::GetService");
        }

        eventHandle_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        stopHandle_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (eventHandle_ == nullptr || stopHandle_ == nullptr) {
            Close();
            return Audio::Result::Failure(Audio::ResultCode::BackendUnavailable, "CreateEventW failed");
        }

        result = audioClient_->SetEventHandle(eventHandle_);
        if (FAILED(result)) {
            Close();
            return BackendFailure(result, "IAudioClient::SetEventHandle");
        }

        renderBuffer_ = Audio::AudioBuffer(actualConfig_.format, bufferFrameCount_);
        Utils::Log::Info("WASAPI device opened");
        return Audio::Result::Success();
    }

    Audio::Result Start(Audio::AudioCallback callback)
    {
        Utils::Log::Debug("Starting WASAPI device");
        if (audioClient_ == nullptr || eventHandle_ == nullptr || stopHandle_ == nullptr) {
            return Audio::Result::Failure(Audio::ResultCode::InvalidState, "WASAPI device is not open");
        }
        if (running_) {
            return Audio::Result::Failure(Audio::ResultCode::InvalidState, "WASAPI device is already running");
        }

        callback_ = std::move(callback);
        ResetEvent(stopHandle_);
        const HRESULT result = audioClient_->Start();
        if (FAILED(result)) {
            return BackendFailure(result, "IAudioClient::Start");
        }

        running_ = true;
        renderThread_ = std::thread(&Implementation::RenderLoop, this);
        Utils::Log::Info("WASAPI device started");
        return Audio::Result::Success();
    }

    Audio::Result Stop()
    {
        Utils::Log::Trace("Stopping WASAPI device");
        if (!running_) {
            return Audio::Result::Success();
        }

        running_ = false;
        SetEvent(stopHandle_);
        if (renderThread_.joinable()) {
            renderThread_.join();
        }

        const HRESULT result = audioClient_ != nullptr ? audioClient_->Stop() : S_OK;
        if (FAILED(result)) {
            return BackendFailure(result, "IAudioClient::Stop");
        }

        Utils::Log::Info("WASAPI device stopped");
        return Audio::Result::Success();
    }

    void Close() noexcept
    {
        Utils::Log::Debug("Closing WASAPI device");
        Stop();
        renderClient_.Reset();
        audioClient_.Reset();
        device_.Reset();
        deviceEnumerator_.Reset();

        if (eventHandle_ != nullptr) {
            CloseHandle(eventHandle_);
            eventHandle_ = nullptr;
        }
        if (stopHandle_ != nullptr) {
            CloseHandle(stopHandle_);
            stopHandle_ = nullptr;
        }
        if (comInitialized_) {
            CoUninitialize();
            comInitialized_ = false;
        }
    }

    const Audio::AudioStreamConfig& ActualConfig() const noexcept
    {
        return actualConfig_;
    }

private:
    void RenderLoop()
    {
        DWORD taskIndex = 0;
        HANDLE avrtHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        const HANDLE handles[] = { stopHandle_, eventHandle_ };

        while (running_) {
            const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (waitResult != WAIT_OBJECT_0 + 1) {
                break;
            }
            RenderBuffer();
        }

        if (avrtHandle != nullptr) {
            AvRevertMmThreadCharacteristics(avrtHandle);
        }
    }

    void RenderBuffer()
    {
        UINT32 padding = 0;
        if (renderClient_ == nullptr || FAILED(audioClient_->GetCurrentPadding(&padding))) {
            return;
        }

        const UINT32 framesToRender = bufferFrameCount_ > padding ? bufferFrameCount_ - padding : 0;
        if (framesToRender == 0) {
            return;
        }

        BYTE* data = nullptr;
        if (FAILED(renderClient_->GetBuffer(framesToRender, &data))) {
            return;
        }

        renderBuffer_.Clear();
        if (callback_) {
            callback_(renderBuffer_);
        }

        const std::size_t sampleCount =
            static_cast<std::size_t>(framesToRender) * actualConfig_.format.channelCount;
        std::memcpy(data, renderBuffer_.Data(), sampleCount * sizeof(float));
        renderClient_->ReleaseBuffer(framesToRender, 0);
    }

    bool comInitialized_ = false;
    std::atomic<bool> running_ = false;
    std::thread renderThread_;
    Audio::AudioCallback callback_;
    Audio::AudioStreamConfig actualConfig_;
    Audio::AudioBuffer renderBuffer_;
    UINT32 bufferFrameCount_ = 0;
    HANDLE eventHandle_ = nullptr;
    HANDLE stopHandle_ = nullptr;
    ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> audioClient_;
    ComPtr<IAudioRenderClient> renderClient_;
#else
    ~Implementation() = default;

    Audio::Result Open(const Audio::AudioStreamConfig&)
    {
        return Audio::Result::Failure(
            Audio::ResultCode::BackendUnavailable,
            "WASAPI is only available on Windows");
    }

    Audio::Result Start(Audio::AudioCallback)
    {
        return Audio::Result::Failure(
            Audio::ResultCode::BackendUnavailable,
            "WASAPI is only available on Windows");
    }

    Audio::Result Stop()
    {
        return Audio::Result::Success();
    }

    void Close() noexcept
    {
    }

    const Audio::AudioStreamConfig& ActualConfig() const noexcept
    {
        return actualConfig_;
    }

    Audio::AudioStreamConfig actualConfig_;
#endif
};

WasapiDevice::WasapiDevice()
    : implementation_(std::make_unique<Implementation>())
{
}

WasapiDevice::~WasapiDevice() = default;

Audio::Result WasapiDevice::Open(const Audio::AudioStreamConfig& requestedConfig)
{
    return implementation_->Open(requestedConfig);
}

Audio::Result WasapiDevice::Start(Audio::AudioCallback callback)
{
    return implementation_->Start(std::move(callback));
}

Audio::Result WasapiDevice::Stop()
{
    return implementation_->Stop();
}

void WasapiDevice::Close() noexcept
{
    implementation_->Close();
}

const Audio::AudioStreamConfig& WasapiDevice::ActualConfig() const noexcept
{
    return implementation_->ActualConfig();
}

}
