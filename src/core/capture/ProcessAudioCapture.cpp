#include "core/capture/ProcessAudioCapture.hpp"

#include <windows.h>
#include <Audioclient.h>
#include <audioclientactivationparams.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace vd {
namespace {

class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler,
                                public IAgileObject {
public:
    ActivationHandler() : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (riid == IID_IAgileObject)
            *out = static_cast<IAgileObject*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return refs_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (refs == 0) delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT hr = operation->GetActivateResult(&hr_, activated_.GetAddressOf());
        if (FAILED(hr)) hr_ = hr;
        if (event_) SetEvent(event_);
        return S_OK;
    }

    HANDLE waitEvent() const { return event_; }
    HRESULT result() const { return hr_; }
    IUnknown* activated() const { return activated_.Get(); }

private:
    ~ActivationHandler() {
        if (event_) CloseHandle(event_);
    }

    std::atomic<ULONG> refs_{1};
    HANDLE event_ = nullptr;
    HRESULT hr_ = E_FAIL;
    Microsoft::WRL::ComPtr<IUnknown> activated_;
};

bool writeZeros(RingBuffer& ring, std::size_t samples) {
    float zeros[1024] = {};
    while (samples > 0) {
        const std::size_t n =
            std::min<std::size_t>(samples, sizeof(zeros) / sizeof(zeros[0]));
        ring.write(zeros, n);
        samples -= n;
    }
    return true;
}

} // namespace

ProcessAudioCapture::ProcessAudioCapture() = default;

ProcessAudioCapture::~ProcessAudioCapture() {
    stopAndJoin();
}

bool ProcessAudioCapture::start(std::uint32_t pid, std::string& err) {
    if (active_.load(std::memory_order_acquire)) {
        err = "source is already active";
        return false;
    }
    state_.store(static_cast<std::uint32_t>(CaptureState::Starting), std::memory_order_relaxed);
    error_.store(static_cast<std::uint32_t>(CaptureError::None), std::memory_order_relaxed);
    pid_.store(pid, std::memory_order_relaxed);

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wasapiEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stopEvent_ || !wasapiEvent_) {
        err = "failed to create capture events";
        stopAndJoin();
        return false;
    }
    processHandle_ = OpenProcess(SYNCHRONIZE, FALSE, pid);
    // No SYNCHRONIZE access is common for anti-cheat/admin processes. Capture
    // still works; only the immediate process-close wakeup is unavailable.

    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activateParams{};
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(params);
    activateParams.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    Microsoft::WRL::ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    Microsoft::WRL::ComPtr<ActivationHandler> handler;
    handler.Attach(new ActivationHandler());
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof(IAudioClient), &activateParams,
                                             handler.Get(), operation.GetAddressOf());
    if (FAILED(hr)) {
        err = "ActivateAudioInterfaceAsync failed";
        stopAndJoin();
        return false;
    }
    const DWORD wait = WaitForSingleObject(handler->waitEvent(), 2500);
    if (wait != WAIT_OBJECT_0 || FAILED(handler->result()) || !handler->activated()) {
        err = "process loopback activation failed or timed out";
        stopAndJoin();
        return false;
    }

    Microsoft::WRL::ComPtr<IAudioClient> client;
    hr = handler->activated()->QueryInterface(IID_PPV_ARGS(client.GetAddressOf()));
    if (FAILED(hr) || !client) {
        err = "activated interface is not IAudioClient";
        stopAndJoin();
        return false;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = kChannels;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 32;
    format.nBlockAlign = WORD(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format, nullptr);
    if (FAILED(hr)) {
        err = "IAudioClient::Initialize failed for process loopback";
        stopAndJoin();
        return false;
    }
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture;
    hr = client->GetService(IID_PPV_ARGS(capture.GetAddressOf()));
    if (FAILED(hr) || !capture) {
        err = "IAudioCaptureClient is unavailable";
        stopAndJoin();
        return false;
    }
    hr = client->SetEventHandle(wasapiEvent_);
    if (FAILED(hr)) {
        err = "SetEventHandle failed";
        stopAndJoin();
        return false;
    }

    client_ = client.Detach();
    captureClient_ = capture.Detach();
    ring_.reset();
    drift_.setTargetFrames(1200);
    drift_.reset();
    smoothedGain_ = 0.0f;
    edgeFade_ = 0.0f;
    level_.store(0.0f, std::memory_order_relaxed);

    hr = client_->Start();
    if (FAILED(hr)) {
        err = "IAudioClient::Start failed";
        stopAndJoin();
        return false;
    }

    active_.store(true, std::memory_order_release);
    state_.store(static_cast<std::uint32_t>(CaptureState::Active), std::memory_order_relaxed);
    thread_ = std::thread([this] { threadMain(); });
    return true;
}

void ProcessAudioCapture::requestStop() {
    if (state_.load(std::memory_order_relaxed) !=
        static_cast<std::uint32_t>(CaptureState::Error))
        state_.store(static_cast<std::uint32_t>(CaptureState::Stopping),
                     std::memory_order_relaxed);
    targetGain_.store(0.0f, std::memory_order_relaxed);
    if (stopEvent_) SetEvent(stopEvent_);
}

void ProcessAudioCapture::setInactive() {
    active_.store(false, std::memory_order_release);
    state_.store(static_cast<std::uint32_t>(CaptureState::Idle), std::memory_order_relaxed);
    targetGain_.store(0.0f, std::memory_order_relaxed);
}

void ProcessAudioCapture::stopAndJoin() {
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    if (client_) {
        client_->Stop();
        client_->Release();
        client_ = nullptr;
    }
    if (captureClient_) {
        captureClient_->Release();
        captureClient_ = nullptr;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (wasapiEvent_) {
        CloseHandle(wasapiEvent_);
        wasapiEvent_ = nullptr;
    }
    if (processHandle_) {
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
    active_.store(false, std::memory_order_release);
    state_.store(static_cast<std::uint32_t>(CaptureState::Idle), std::memory_order_relaxed);
    pid_.store(0, std::memory_order_relaxed);
    targetGain_.store(1.0f, std::memory_order_relaxed);
    level_.store(0.0f, std::memory_order_relaxed);
    capturedFrames_.store(0, std::memory_order_relaxed);
    silentFrames_.store(0, std::memory_order_relaxed);
    ring_.reset();
    drift_.reset();
}

void ProcessAudioCapture::setGain(float gain) {
    targetGain_.store(std::max(0.0f, std::min(2.0f, gain)), std::memory_order_relaxed);
}

void ProcessAudioCapture::threadMain() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);

    HANDLE handles[3] = {wasapiEvent_, stopEvent_, processHandle_};
    DWORD handleCount = processHandle_ ? 3 : 2;
    bool running = true;
    while (running) {
        const DWORD wait = WaitForMultipleObjects(handleCount, handles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0 + 1) break;
        if (processHandle_ && wait == WAIT_OBJECT_0 + 2) {
            error_.store(static_cast<std::uint32_t>(CaptureError::ProcessClosed),
                         std::memory_order_relaxed);
            state_.store(static_cast<std::uint32_t>(CaptureState::Error),
                         std::memory_order_relaxed);
            break;
        }
        if (wait != WAIT_OBJECT_0) {
            error_.store(static_cast<std::uint32_t>(CaptureError::ReadFailed),
                         std::memory_order_relaxed);
            state_.store(static_cast<std::uint32_t>(CaptureState::Error),
                         std::memory_order_relaxed);
            break;
        }

        UINT32 packetFrames = 0;
        while (running) {
            const HRESULT nextHr = captureClient_->GetNextPacketSize(&packetFrames);
            if (FAILED(nextHr)) {
                error_.store(static_cast<std::uint32_t>(
                                 nextHr == AUDCLNT_E_DEVICE_INVALIDATED
                                     ? CaptureError::DeviceInvalidated
                                     : CaptureError::ReadFailed),
                             std::memory_order_relaxed);
                state_.store(static_cast<std::uint32_t>(CaptureState::Error),
                             std::memory_order_relaxed);
                running = false;
                break;
            }
            if (packetFrames == 0) break;

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            const HRESULT hr = captureClient_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                error_.store(static_cast<std::uint32_t>(
                                 hr == AUDCLNT_E_DEVICE_INVALIDATED
                                     ? CaptureError::DeviceInvalidated
                                     : CaptureError::ReadFailed),
                             std::memory_order_relaxed);
                state_.store(static_cast<std::uint32_t>(CaptureState::Error),
                             std::memory_order_relaxed);
                running = false;
                break;
            }

            if (frames > 0) {
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data) {
                    writeZeros(ring_, std::size_t(frames) * kChannels);
                    silentFrames_.fetch_add(frames, std::memory_order_relaxed);
                    level_.store(0.0f, std::memory_order_relaxed);
                } else {
                    const float* samples = reinterpret_cast<const float*>(data);
                    capturedFrames_.fetch_add(frames, std::memory_order_relaxed);
                    ring_.write(samples, std::size_t(frames) * kChannels);
                    float peak = 0.0f;
                    for (std::size_t i = 0; i < std::size_t(frames) * kChannels; ++i)
                        peak = std::max(peak, std::fabs(samples[i]));
                    level_.store(peak, std::memory_order_relaxed);
                }
            }
            captureClient_->ReleaseBuffer(frames);
        }
    }

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}

void ProcessAudioCapture::mixInto(float* out, std::size_t frames) {
    if (!active_.load(std::memory_order_acquire)) return;
    audioBusy_.store(true, std::memory_order_release);

    const CaptureState st = state();
    const bool wantsSignal = st == CaptureState::Active || st == CaptureState::Silent;
    const float target = wantsSignal ? targetGain_.load(std::memory_order_relaxed) : 0.0f;

    const std::size_t availFrames = ring_.available() / kChannels;
    if (availFrames > kHighWaterFrames)
        ring_.discard((availFrames - kHighWaterFrames) * kChannels);

    std::memset(block_, 0, frames * kChannels * sizeof(float));
    const std::size_t produced = drift_.pull(ring_, block_, frames);

    const float gainStep = 1.0f - std::exp(-1.0f / (0.020f * 48000.0f));
    const float fadeStep = 1.0f / (0.003f * 48000.0f);
    const float fadeTarget = (produced == frames && wantsSignal) ? 1.0f : 0.0f;
    for (std::size_t i = 0; i < frames; ++i) {
        smoothedGain_ += (target - smoothedGain_) * gainStep;
        if (edgeFade_ < fadeTarget)
            edgeFade_ = std::min(fadeTarget, edgeFade_ + fadeStep);
        else if (edgeFade_ > fadeTarget)
            edgeFade_ = std::max(fadeTarget, edgeFade_ - fadeStep);
        const float g = smoothedGain_ * edgeFade_;
        out[i * kChannels] += block_[i * kChannels] * g;
        out[i * kChannels + 1] += block_[i * kChannels + 1] * g;
    }
    audioBusy_.store(false, std::memory_order_release);
}

} // namespace vd
