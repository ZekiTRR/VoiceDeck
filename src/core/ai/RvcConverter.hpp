#pragma once

// Port of Voice CTX's RvcConverter.cs (MIT): RVC v2 streaming conversion.
// HuBERT content features (50 fps, repeated to 100 fps) + RMVPE pitch on the
// whole context, decoded by the voice generator for the newest frames only.
// All tensors are pre-allocated once and reused every call.

#include "core/ai/MelFrontEnd.hpp"
#include "core/ai/Ort.hpp"
#include "core/ai/VoiceLibrary.hpp"

#include <memory>
#include <string>
#include <vector>

namespace vd {

struct ConverterShape {
    int contextSamples16k = 0;
    int blockSamples16k = 0;
    int outputSampleRate = 0;
    int outputSamples = 0;
    int marginSamples = 0;

    int blockOutputSamples() const { return outputSamples - marginSamples; }
    bool operator==(const ConverterShape&) const = default;
};

struct GpuAdapter {
    int index = 0;
    std::string name;
    unsigned long long dedicatedVideoMemory = 0;
};

// Inference backends. "auto" tries CUDA first (onnxruntime GPU build),
// then DirectML (any DX12 GPU), then CPU.
enum class AiProvider { Auto, Cuda, Dml, Cpu };
const char* AiProviderName(AiProvider p);
bool ParseAiProvider(const std::string& s, AiProvider& out);

// Enumerates DXGI adapters in DirectML's device_id order (port of Voice CTX
// GpuSelector.cs). Picks the most dedicated VRAM on hybrid laptops.
std::vector<GpuAdapter> EnumerateGpuAdapters();

// ONNX Runtime sessions shared by every voice: content encoder + RMVPE.
class RvcRuntime {
public:
    ~RvcRuntime();

    static std::unique_ptr<RvcRuntime> load(const VoiceLibrary& lib, AiProvider hint,
                                            std::string& err);

    OrtEnv* env = nullptr;
    OrtSession* encoder = nullptr;
    OrtSession* pitch = nullptr;
    OrtMemoryInfo* memInfo = nullptr;
    AiProvider provider = AiProvider::Cpu;
    bool usesGpu = false;
    std::string deviceName;
    int adapterIndex = -1;
    GpuAdapter adapter{};

    // Session options matching this runtime's provider. "" on success.
    std::string makeSessionOptions(void** optionsOut);

    RvcRuntime() = default;

private:
    std::string makeSessionOptionsFor(AiProvider p, void** optionsOut);
};

// A single loaded voice: generator session + pre-bound I/O tensors.
class RvcConverter {
public:
    // "" on success, else an error message (dims/config mismatch included).
    RvcConverter(RvcRuntime& runtime, const AiVoiceInfo& voice, std::string& err);
    ~RvcConverter();
    RvcConverter(const RvcConverter&) = delete;
    RvcConverter& operator=(const RvcConverter&) = delete;

    ConverterShape shape() const { return shape_; }
    const AiVoiceInfo& voice() const { return voice_; }

    // context16k: shape_.contextSamples16k samples; output: shape_.outputSamples.
    void convert(const float* context16k, float pitchShiftSemitones, float* output);

private:
    static constexpr int kFeatureSize = 768;

    bool findInputDim(const char* name, int dimIndex, long long& dimOut) const;
    void runSingleOutput(OrtSession* session, const char* const* inNames, OrtValue* const* in,
                         int nIn, const char* const* outNames, OrtValue* out, OrtValue* myValue,
                         float* dst, size_t dstBytes);

    RvcRuntime* runtime_ = nullptr;
    AiVoiceInfo voice_;
    ConverterShape shape_{};

    int frames_ = 0;
    int tail_ = 0;
    int encoderFrames_ = 0;
    int pitchFrames_ = 0;
    int pitchPadded_ = 0;

    OrtSession* generator_ = nullptr;

    MelFrontEnd mel_;
    std::vector<float> wav_;
    std::vector<float> features_;
    std::vector<float> melBuffer_;
    std::vector<float> salience_;
    std::vector<float> f0_;
    std::vector<float> phone_;
    std::vector<long long> pitch_;
    std::vector<float> nsff0_;
    std::vector<float> audio_;

    OrtValue* wavValue_ = nullptr;
    OrtValue* featuresValue_ = nullptr;
    OrtValue* melValue_ = nullptr;
    OrtValue* salienceValue_ = nullptr;
    OrtValue* phoneValue_ = nullptr;
    OrtValue* pitchValue_ = nullptr;
    OrtValue* nsff0Value_ = nullptr;
    OrtValue* sidValue_ = nullptr;
    OrtValue* noiseValue_ = nullptr;
    OrtValue* audioValue_ = nullptr;

    std::string pitchInputName_;
    std::string pitchOutputName_;
};

} // namespace vd
