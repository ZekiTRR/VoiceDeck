#include "core/ai/AiVoiceService.hpp"

#include "core/util/Log.hpp"

#include <utility>

namespace vd {

std::string AiVoiceService::loadVoice(const AiVoiceInfo& voice) {
    // Voice switching from the UI can fire several times in quick succession.
    // Serialize the whole load (model creation + GPU warm-up + swap) so two
    // threads can never race on runtime_/converter_ and crash ONNX Runtime.
    std::lock_guard<std::mutex> loadLock(loadMtx_);
    return loadVoiceLocked(voice);
}

std::string AiVoiceService::loadVoiceLocked(const AiVoiceInfo& voice) {
    auto& ort = OrtLib::get();
    if (!ort.loaded) return "onnxruntime.dll not found (copy it next to voicedeck.exe)";
    if (!library_.baseModelsInstalled())
        return "AI Voice models are not installed (see 'aivoices')";

    setStatus("loading " + voice.name + "...");
    try {
        if (!runtime_) {
            std::string err;
            auto rt = RvcRuntime::load(library_, hint_, err);
            if (!rt) {
                setStatus("model load failed: " + err);
                return err;
            }
            {
                std::lock_guard<std::mutex> lk(sync_);
                runtime_ = std::move(rt);
            }
        }

        std::string err;
        auto converter = std::make_unique<RvcConverter>(*runtime_, voice, err);
        if (!err.empty()) {
            setStatus("could not load voice: " + err);
            return err;
        }
        // Warm up the kernels so the first real block isn't slow.
        ConverterShape shape = converter->shape();
        std::vector<float> warmCtx(std::size_t(shape.contextSamples16k), 0.0f);
        std::vector<float> warmOut(std::size_t(shape.outputSamples), 0.0f);
        converter->convert(warmCtx.data(), 0.0f, warmOut.data());

        {
            std::lock_guard<std::mutex> lk(sync_);
            converter_ = std::move(converter);
            ready_.store(true, std::memory_order_relaxed);
        }

        {
            std::lock_guard<std::mutex> lk(statusMtx_);
            voiceName_ = voice.name;
            deviceName_ = runtime_ ? runtime_->deviceName : "CPU";
            usesGpu_ = runtime_ && runtime_->usesGpu;
            statusMessage_ = usesGpu_ ? ("running on " + deviceName_)
                                      : "running on CPU (slow - a GPU is recommended)";
        }
        lastVoice_ = voice;
        hasLastVoice_ = true;
        return "";
    } catch (...) {
        setStatus("could not load voice (onnxruntime failure)");
        return "onnxruntime failure";
    }
}

void AiVoiceService::setProviderHint(AiProvider p) {
    std::lock_guard<std::mutex> loadLock(loadMtx_);
    {
        std::lock_guard<std::mutex> lk(sync_);
        hint_ = p;
    }
    const bool reload = hasLastVoice_ && ready_.load(std::memory_order_relaxed);
    const AiVoiceInfo v = lastVoice_;
    if (reload) {
        unloadLocked();
        loadVoiceLocked(v);
    }
}

void AiVoiceService::unload() {
    std::lock_guard<std::mutex> loadLock(loadMtx_);
    unloadLocked();
}

void AiVoiceService::unloadLocked() {
    ready_.store(false, std::memory_order_relaxed);
    {
        // Drop the converter BEFORE the runtime it borrows. Both happen under
        // sync_ so the inference worker can never use a freed ORT session.
        std::lock_guard<std::mutex> lk(sync_);
        converter_.reset();
        runtime_.reset();
    }
    {
        std::lock_guard<std::mutex> lk(statusMtx_);
        voiceName_.clear();
        statusMessage_ = "no voice loaded";
    }
}

bool AiVoiceService::tryConvert(const float* context16k, float pitchShift, float* output,
                                ConverterShape& shape) {
    std::lock_guard<std::mutex> lk(sync_);
    if (!converter_ || !ready_.load(std::memory_order_relaxed)) return false;
    shape = converter_->shape();
    converter_->convert(context16k, pitchShift, output);
    return true;
}

bool AiVoiceService::currentShape(ConverterShape& out) {
    std::lock_guard<std::mutex> lk(sync_);
    if (!converter_ || !ready_.load(std::memory_order_relaxed)) return false;
    out = converter_->shape();
    return true;
}

std::string AiVoiceService::statusMessage() const {
    std::lock_guard<std::mutex> lk(statusMtx_);
    return statusMessage_;
}

std::string AiVoiceService::voiceName() const {
    std::lock_guard<std::mutex> lk(statusMtx_);
    return voiceName_;
}

std::string AiVoiceService::deviceName() const {
    std::lock_guard<std::mutex> lk(statusMtx_);
    return deviceName_;
}

bool AiVoiceService::usesGpu() const {
    std::lock_guard<std::mutex> lk(statusMtx_);
    return usesGpu_;
}

void AiVoiceService::setStatus(std::string msg) {
    std::lock_guard<std::mutex> lk(statusMtx_);
    statusMessage_ = std::move(msg);
}

} // namespace vd
