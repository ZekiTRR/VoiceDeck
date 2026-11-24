#include "core/ai/RvcConverter.hpp"

#include "core/util/Log.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace vd {

// ---------------------------------------------------------------------------
// GPU selection (port of Voice CTX GpuSelector.cs - manual vtable calls)
// ---------------------------------------------------------------------------

std::vector<GpuAdapter> EnumerateGpuAdapters() {
    std::vector<GpuAdapter> result;
    HMODULE dxgi = GetModuleHandleA("dxgi.dll");
    if (!dxgi) dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi) return result;

    using CreateFactoryFn = long(__stdcall*)(const void*, void**);
    auto createFactory =
        reinterpret_cast<CreateFactoryFn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
    if (!createFactory) return result;

    // IID_IDXGIFactory1
    static const GUID kIidFactory1 = {0x770aae78, 0xf26f, 0x4dba,
                                      {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
    void* factory = nullptr;
    if (createFactory(&kIidFactory1, &factory) < 0 || !factory) return result;

    using ReleaseFn = unsigned long(__stdcall*)(void*);
    auto factoryVt = *reinterpret_cast<void***>(factory);
    auto releaseFn = reinterpret_cast<ReleaseFn>(factoryVt[2]); // IUnknown::Release
    // IDXGIFactory1::EnumAdapters1 is vtable slot 12.
    using EnumAdaptersFn = long(__stdcall*)(void*, unsigned int, void**);
    auto enumAdapters1 = reinterpret_cast<EnumAdaptersFn>(factoryVt[12]);

    struct AdapterDesc1 {
        wchar_t Description[128];
        unsigned int VendorId;
        unsigned int DeviceId;
        unsigned int SubSysId;
        unsigned int Revision;
        unsigned long long DedicatedVideoMemory;
        unsigned long long DedicatedSystemMemory;
        unsigned long long SharedSystemMemory;
        long long AdapterLuid;
        unsigned int Flags;
    };

    for (unsigned int i = 0;; ++i) {
        void* adapter = nullptr;
        if (enumAdapters1(factory, i, &adapter) < 0 || !adapter) break;
        auto adapterVt = *reinterpret_cast<void***>(adapter);
        // IDXGIAdapter1::GetDesc1 is vtable slot 10.
        using GetDescFn = long(__stdcall*)(void*, AdapterDesc1*);
        auto getDesc1 = reinterpret_cast<GetDescFn>(adapterVt[10]);
        AdapterDesc1 desc{};
        if (getDesc1(adapter, &desc) >= 0 && (desc.Flags & 0x2) == 0) { // skip software
            char nameBuf[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nameBuf, sizeof(nameBuf), nullptr,
                                nullptr);
            GpuAdapter a;
            a.index = int(i);
            a.name = nameBuf;
            a.dedicatedVideoMemory = desc.DedicatedVideoMemory;
            result.push_back(std::move(a));
        }
        releaseFn(adapter);
    }
    releaseFn(factory);
    return result;
}

const char* AiProviderName(AiProvider p) {
    switch (p) {
        case AiProvider::Cuda: return "cuda";
        case AiProvider::Dml: return "dml";
        case AiProvider::Cpu: return "cpu";
        default: return "auto";
    }
}

bool ParseAiProvider(const std::string& s, AiProvider& out) {
    std::string t = s;
    for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (t == "auto") out = AiProvider::Auto;
    else if (t == "cuda" || t == "nvidia") out = AiProvider::Cuda;
    else if (t == "dml" || t == "directml") out = AiProvider::Dml;
    else if (t == "cpu") out = AiProvider::Cpu;
    else return false;
    return true;
}

// ---------------------------------------------------------------------------
// RvcRuntime
// ---------------------------------------------------------------------------

RvcRuntime::~RvcRuntime() {
    auto& lib = OrtLib::get();
    if (!lib.loaded) return;
    if (encoder) lib.api->ReleaseSession(encoder);
    if (pitch) lib.api->ReleaseSession(pitch);
    if (memInfo) lib.api->ReleaseMemoryInfo(memInfo);
    if (env) lib.api->ReleaseEnv(env);
}

std::string RvcRuntime::makeSessionOptionsFor(AiProvider p, void** optionsOut) {
    auto& lib = OrtLib::get();
    *optionsOut = nullptr;
    OrtSessionOptions* opts = nullptr;
    std::string err = OrtLib::statusMessage(lib.api->CreateSessionOptions(&opts), lib.api);
    if (!err.empty()) return err;
    lib.api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL);

    if (p == AiProvider::Cuda && lib.cudaAppend) {
        using CudaAppendFn = OrtStatus* (*)(OrtSessionOptions*, int);
        auto cuda = reinterpret_cast<CudaAppendFn>(lib.cudaAppend);
        err = OrtLib::statusMessage(cuda(opts, 0), lib.api);
    } else if (p == AiProvider::Dml && lib.dmlAppend && adapterIndex >= 0) {
        lib.api->DisableMemPattern(opts); // required by the DirectML provider
        lib.api->SetSessionExecutionMode(opts, ORT_SEQUENTIAL);
        using DmlAppendFn = OrtStatus* (*)(OrtSessionOptions*, int);
        auto dml = reinterpret_cast<DmlAppendFn>(lib.dmlAppend);
        err = OrtLib::statusMessage(dml(opts, adapterIndex), lib.api);
    } else if (p == AiProvider::Cuda || p == AiProvider::Dml) {
        err = "provider not available in this onnxruntime build";
    } else {
        // CPU inference must not spin up one thread per logical core: ORT's
        // thread pool would compete with games and the audio callback. Use at
        // most half of the PHYSICAL cores (capped at 4) and sequential
        // execution to avoid inter-op sync overhead.
        unsigned physical = 0;
        DWORD len = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && len > 0) {
            std::vector<unsigned char> buf(len);
            if (GetLogicalProcessorInformationEx(
                    RelationProcessorCore,
                    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &len)) {
                for (DWORD off = 0; off < len;) {
                    auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                        buf.data() + off);
                    if (info->Relationship == RelationProcessorCore) ++physical;
                    if (info->Size == 0) break;
                    off += info->Size;
                }
            }
        }
        if (physical == 0) {
            SYSTEM_INFO si{};
            GetSystemInfo(&si);
            physical = std::max<DWORD>(1, si.dwNumberOfProcessors / 2);
        }
        const int threads = int(std::max<unsigned>(1, std::min<unsigned>(4, physical / 2)));
        lib.api->SetIntraOpNumThreads(opts, threads);
        lib.api->SetInterOpNumThreads(opts, 1);
        lib.api->SetSessionExecutionMode(opts, ORT_SEQUENTIAL);
        log::info("AI CPU inference: %d intra-op thread(s), sequential mode (%u physical cores)",
                  threads, physical);
    }
    if (!err.empty()) {
        lib.api->ReleaseSessionOptions(opts);
        return err;
    }
    *optionsOut = opts;
    return "";
}

std::string RvcRuntime::makeSessionOptions(void** optionsOut) {
    return makeSessionOptionsFor(provider, optionsOut);
}

std::unique_ptr<RvcRuntime> RvcRuntime::load(const VoiceLibrary& lib, AiProvider hint,
                                              std::string& err) {
    auto& ort = OrtLib::get();
    if (!ort.loaded) {
        err = "onnxruntime.dll not found (copy it next to voicedeck.exe)";
        return nullptr;
    }
    const OrtApi* api = ort.api;
    if (!lib.baseModelsInstalled()) {
        err = "AI Voice models are not installed (see 'aivoices')";
        return nullptr;
    }

    // Pick the adapter with the most dedicated VRAM (discrete GPU on laptops).
    GpuAdapter best{};
    bool haveBest = false;
    for (const GpuAdapter& a : EnumerateGpuAdapters()) {
        if (!haveBest || a.dedicatedVideoMemory > best.dedicatedVideoMemory) {
            best = a;
            haveBest = true;
        }
    }

    const std::wstring encPath = lib.encoderPath().wstring();
    const std::wstring pitchPath = lib.pitchPath().wstring();

    auto tryProvider = [&](AiProvider p) -> std::unique_ptr<RvcRuntime> {
        auto rt = std::make_unique<RvcRuntime>();
        rt->provider = p;
        rt->adapter = best;
        if (p == AiProvider::Dml) {
            if (!haveBest) {
                err = "no DXGI adapter for DirectML";
                return nullptr;
            }
            rt->adapterIndex = best.index;
        }
        err = OrtLib::statusMessage(
            api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "VoiceDeck", &rt->env), api);
        if (!err.empty()) return nullptr;
        err = OrtLib::statusMessage(
            api->CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &rt->memInfo), api);
        if (!err.empty()) return nullptr;

        void* opts = nullptr;
        err = rt->makeSessionOptionsFor(p, &opts);
        if (!err.empty()) return nullptr;
        err = OrtLib::statusMessage(
            api->CreateSession(rt->env, reinterpret_cast<const ORTCHAR_T*>(encPath.c_str()),
                               static_cast<OrtSessionOptions*>(opts), &rt->encoder),
            api);
        if (err.empty())
            err = OrtLib::statusMessage(
                api->CreateSession(rt->env, reinterpret_cast<const ORTCHAR_T*>(pitchPath.c_str()),
                                   static_cast<OrtSessionOptions*>(opts), &rt->pitch),
                api);
        if (opts) api->ReleaseSessionOptions(static_cast<OrtSessionOptions*>(opts));
        if (!err.empty()) return nullptr;

        switch (p) {
            case AiProvider::Cuda:
                rt->usesGpu = true;
                rt->deviceName = haveBest ? best.name + " (CUDA)" : "CUDA device 0";
                break;
            case AiProvider::Dml:
                rt->usesGpu = true;
                rt->deviceName = best.name + " (DirectML)";
                break;
            default:
                rt->usesGpu = false;
                rt->deviceName = "CPU";
                break;
        }
        return rt;
    };

    // Attempt order. Auto: CUDA (if the build has it) -> DirectML -> CPU.
    std::vector<AiProvider> order;
    switch (hint) {
        case AiProvider::Cuda: order = {AiProvider::Cuda, AiProvider::Dml, AiProvider::Cpu}; break;
        case AiProvider::Dml: order = {AiProvider::Dml, AiProvider::Cpu}; break;
        case AiProvider::Cpu: order = {AiProvider::Cpu}; break;
        default: {
            order = {AiProvider::Dml, AiProvider::Cpu};
            if (ort.cudaAppend) order.insert(order.begin(), AiProvider::Cuda);
            break;
        }
    }

    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == AiProvider::Cuda && !ort.cudaAppend) continue;
        if (order[i] == AiProvider::Dml && !ort.dmlAppend) continue;
        auto rt = tryProvider(order[i]);
        if (rt) {
            if (i > 0)
                log::warn("AI provider '%s' failed (%s) - using '%s'", AiProviderName(order[0]),
                          err.c_str(), AiProviderName(order[i]));
            err.clear();
            return rt;
        }
        log::info("AI provider '%s' unavailable (%s)", AiProviderName(order[i]), err.c_str());
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// RvcConverter
// ---------------------------------------------------------------------------

RvcConverter::RvcConverter(RvcRuntime& runtime, const AiVoiceInfo& voice, std::string& err)
    : runtime_(&runtime), voice_(voice) {
    const OrtApi* api = OrtLib::get().api;

    frames_ = voice.frames;
    tail_ = voice.tail;
    shape_.contextSamples16k = (voice.blockMs + voice.contextMs) * 16;
    shape_.blockSamples16k = voice.blockMs * 16;
    shape_.outputSampleRate = voice.sampleRate;
    const int hop = voice.sampleRate / 100;
    shape_.outputSamples = tail_ * hop;
    shape_.marginSamples = voice.marginFrames * hop;

    // Generator session (voice-specific).
    {
        void* opts = nullptr;
        err = runtime.makeSessionOptions(&opts);
        if (!err.empty()) return;
        const std::wstring model = voice.modelPath.wstring();
        err = OrtLib::statusMessage(
            api->CreateSession(runtime.env, reinterpret_cast<const ORTCHAR_T*>(model.c_str()),
                               static_cast<OrtSessionOptions*>(opts), &generator_),
            api);
        if (opts) api->ReleaseSessionOptions(static_cast<OrtSessionOptions*>(opts));
        if (!err.empty()) return;
    }

    // Validate the generator against the voice description ("expected tensor
    // dimensionalities" from the config, checked against session metadata).
    long long phoneDim = -1;
    long long nsff0Dim = -1;
    if (!findInputDim("phone", 1, phoneDim) || !findInputDim("nsff0", 1, nsff0Dim)) {
        err = "voice model '" + voice.name + "' is missing its phone/nsff0 inputs";
        return;
    }
    if ((phoneDim >= 0 && phoneDim != frames_) || (nsff0Dim >= 0 && nsff0Dim != tail_)) {
        err = "voice model '" + voice.name + "' does not match its description";
        return;
    }

    encoderFrames_ = (shape_.contextSamples16k - 400) / 320 + 1;
    pitchFrames_ = MelFrontEnd::frameCount(shape_.contextSamples16k);
    pitchPadded_ = MelFrontEnd::paddedFrameCount(pitchFrames_);
    if (encoderFrames_ * 2 != frames_ || pitchFrames_ < frames_) {
        err = "voice '" + voice.name + "' does not match its streaming configuration";
        return;
    }

    wav_.assign(std::size_t(shape_.contextSamples16k), 0.0f);
    features_.assign(std::size_t(encoderFrames_) * kFeatureSize, 0.0f);
    melBuffer_.assign(std::size_t(MelFrontEnd::kMels) * std::size_t(pitchPadded_), 0.0f);
    salience_.assign(std::size_t(pitchPadded_) * PitchMath::kSalienceBins, 0.0f);
    f0_.assign(std::size_t(pitchPadded_), 0.0f);
    phone_.assign(std::size_t(frames_) * kFeatureSize, 0.0f);
    pitch_.assign(std::size_t(frames_), 0);
    nsff0_.assign(std::size_t(tail_), 0.0f);
    audio_.assign(std::size_t(shape_.outputSamples), 0.0f);

    // Pre-allocate and bind all tensors once.
    const int64_t wavDims[2] = {1, int64_t(shape_.contextSamples16k)};
    const int64_t featDims[3] = {1, int64_t(encoderFrames_), kFeatureSize};
    const int64_t melDims[3] = {1, MelFrontEnd::kMels, int64_t(pitchPadded_)};
    const int64_t salienceDims[3] = {1, int64_t(pitchPadded_), PitchMath::kSalienceBins};
    const int64_t phoneDims[3] = {1, int64_t(frames_), kFeatureSize};
    const int64_t pitchDims[2] = {1, int64_t(frames_)};
    const int64_t nsff0Dims[2] = {1, int64_t(tail_)};
    const int64_t sidDims[1] = {1};
    const int64_t audioDims[2] = {1, int64_t(shape_.outputSamples)};

    auto mkF = [&](void* data, size_t count, const int64_t* dims, size_t nDims, OrtValue** out) {
        return OrtLib::statusMessage(
            api->CreateTensorWithDataAsOrtValue(runtime.memInfo, data, count * sizeof(float), dims,
                                                nDims, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, out),
            api);
    };
    err = mkF(wav_.data(), wav_.size(), wavDims, 2, &wavValue_);
    if (err.empty()) err = mkF(features_.data(), features_.size(), featDims, 3, &featuresValue_);
    if (err.empty()) err = mkF(melBuffer_.data(), melBuffer_.size(), melDims, 3, &melValue_);
    if (err.empty()) err = mkF(salience_.data(), salience_.size(), salienceDims, 3, &salienceValue_);
    if (err.empty()) err = mkF(phone_.data(), phone_.size(), phoneDims, 3, &phoneValue_);
    if (err.empty()) err = mkF(nsff0_.data(), nsff0_.size(), nsff0Dims, 2, &nsff0Value_);
    if (err.empty()) err = mkF(audio_.data(), audio_.size(), audioDims, 2, &audioValue_);
    if (!err.empty()) return;

    // int64 pitch tokens for the generator.
    err = OrtLib::statusMessage(
        api->CreateTensorWithDataAsOrtValue(runtime.memInfo, pitch_.data(),
                                            pitch_.size() * sizeof(long long), pitchDims, 2,
                                            ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &pitchValue_),
        api);
    if (!err.empty()) return;

    static long long sidData[1] = {0};
    err = OrtLib::statusMessage(
        api->CreateTensorWithDataAsOrtValue(runtime.memInfo, sidData, sizeof(sidData), sidDims, 1,
                                            ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &sidValue_),
        api);
    if (!err.empty()) return;
    static float noiseData[1] = {0.66666f};
    err = OrtLib::statusMessage(
        api->CreateTensorWithDataAsOrtValue(runtime.memInfo, noiseData, sizeof(noiseData), nullptr, 0,
                                            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &noiseValue_),
        api);
    if (!err.empty()) return;
    static_assert(sizeof(noiseData) == sizeof(float), "scalar noise tensor");

    // Pitch model I/O names (first input/output of rmvpe.onnx).
    {
        OrtAllocator* alloc = nullptr;
        err = OrtLib::statusMessage(api->GetAllocatorWithDefaultOptions(&alloc), api);
        if (!err.empty()) return;
        char* inName = nullptr;
        char* outName = nullptr;
        err = OrtLib::statusMessage(api->SessionGetInputName(runtime.pitch, 0, alloc, &inName), api);
        if (err.empty())
            err = OrtLib::statusMessage(api->SessionGetOutputName(runtime.pitch, 0, alloc, &outName),
                                        api);
        if (!err.empty()) return;
        if (inName) {
            pitchInputName_ = inName;
            api->AllocatorFree(alloc, inName);
        }
        if (outName) {
            pitchOutputName_ = outName;
            api->AllocatorFree(alloc, outName);
        }
        if (pitchInputName_.empty() || pitchOutputName_.empty()) {
            err = "rmvpe.onnx has no named input/output";
            return;
        }
    }
}

RvcConverter::~RvcConverter() {
    auto& lib = OrtLib::get();
    if (!lib.loaded) return;
    const OrtApi* api = lib.api;
    for (OrtValue* v : {wavValue_, featuresValue_, melValue_, salienceValue_, phoneValue_,
                        pitchValue_, nsff0Value_, sidValue_, noiseValue_, audioValue_})
        if (v) api->ReleaseValue(v);
    if (generator_) api->ReleaseSession(generator_);
}

bool RvcConverter::findInputDim(const char* name, int dimIndex, long long& dimOut) const {
    auto& lib = OrtLib::get();
    if (!lib.loaded) return false;
    const OrtApi* api = lib.api;

    size_t count = 0;
    if (api->SessionGetInputCount(generator_, &count) != nullptr) return false;
    OrtAllocator* alloc = nullptr;
    if (api->GetAllocatorWithDefaultOptions(&alloc) != nullptr) return false;

    for (size_t i = 0; i < count; ++i) {
        char* nm = nullptr;
        if (api->SessionGetInputName(generator_, i, alloc, &nm) != nullptr) continue;
        std::string cur = nm ? nm : "";
        if (nm) api->AllocatorFree(alloc, nm);
        if (cur != name) continue;

        OrtTypeInfo* typeInfo = nullptr;
        if (api->SessionGetInputTypeInfo(generator_, i, &typeInfo) != nullptr) return false;
        const OrtTensorTypeAndShapeInfo* tensorInfo = nullptr;
        bool ok = api->CastTypeInfoToTensorInfo(typeInfo, &tensorInfo) == nullptr && tensorInfo;
        if (ok) {
            size_t dimCount = 0;
            ok = api->GetDimensionsCount(tensorInfo, &dimCount) == nullptr &&
                 dimCount > size_t(dimIndex);
            if (ok) {
                int64_t dims[8] = {};
                ok = api->GetDimensions(tensorInfo, dims, dimCount) == nullptr;
                if (ok) dimOut = long long(dims[size_t(dimIndex)]);
            }
        }
        api->ReleaseTypeInfo(typeInfo);
        return ok;
    }
    return false;
}

void RvcConverter::runSingleOutput(OrtSession* session, const char* const* inNames,
                                   OrtValue* const* in, int nIn, const char* const* outNames,
                                   OrtValue* out, OrtValue* myValue, float* dst, size_t dstBytes) {
    auto& lib = OrtLib::get();
    const OrtApi* api = lib.api;

    OrtValue* outVals[1] = {out};
    OrtStatus* st = api->Run(session, nullptr, inNames, in, size_t(nIn), outNames, size_t(1), outVals);
    if (st != nullptr) {
        log::error("inference failed: %s", api->GetErrorMessage(st));
        api->ReleaseStatus(st);
        std::memset(dst, 0, dstBytes);
        return;
    }
    if (outVals[0] != myValue) {
        // ORT allocated its own output tensor: copy, then release it.
        void* data = nullptr;
        if (api->GetTensorMutableData(outVals[0], &data) == nullptr && data)
            std::memcpy(dst, data, dstBytes);
        api->ReleaseValue(outVals[0]);
        outVals[0] = myValue;
    }
}

void RvcConverter::convert(const float* context16k, float pitchShiftSemitones, float* output) {
    const OrtApi* api = OrtLib::get().api;

    std::memcpy(wav_.data(), context16k, wav_.size() * sizeof(float));

    // 1. Content encoder: wav -> HuBERT features (50 fps).
    {
        const char* inNames[1] = {"wav16k"};
        const char* outNames[1] = {"features"};
        OrtValue* in[1] = {wavValue_};
        runSingleOutput(runtime_->encoder, inNames, in, 1, outNames, featuresValue_, featuresValue_,
                        features_.data(), features_.size() * sizeof(float));
    }
    // 2. Repeat features to 100 fps ("phone" frames).
    for (int f = 0; f < frames_; ++f)
        std::memcpy(phone_.data() + std::size_t(f) * kFeatureSize,
                    features_.data() + std::size_t(f / 2) * kFeatureSize, kFeatureSize * sizeof(float));

    // 3. RMVPE pitch on the whole context.
    mel_.compute(wav_.data(), int(wav_.size()), melBuffer_.data());
    {
        const char* inNames[1] = {pitchInputName_.c_str()};
        const char* outNames[1] = {pitchOutputName_.c_str()};
        OrtValue* in[1] = {melValue_};
        runSingleOutput(runtime_->pitch, inNames, in, 1, outNames, salienceValue_, salienceValue_,
                        salience_.data(), salience_.size() * sizeof(float));
    }
    PitchMath::DecodeSalience(salience_.data(), pitchFrames_, f0_.data());

    // 4. Pitch tokens + fine f0 for the newest frames only.
    const float ratio = std::pow(2.0f, pitchShiftSemitones / 12.0f);
    const int offset = pitchFrames_ - frames_;
    for (int f = 0; f < frames_; ++f) {
        const float hz = f0_[std::size_t(offset + f)] * ratio;
        pitch_[std::size_t(f)] = PitchMath::Coarse(hz);
        if (f >= frames_ - tail_) nsff0_[std::size_t(f - (frames_ - tail_))] = hz;
    }

    // 5. Generator: decode the newest frames.
    {
        const char* inNames[5] = {"phone", "pitch", "nsff0", "sid", "noise_scale"};
        const char* outNames[1] = {"audio"};
        OrtValue* in[5] = {phoneValue_, pitchValue_, nsff0Value_, sidValue_, noiseValue_};
        runSingleOutput(generator_, inNames, in, 5, outNames, audioValue_, audioValue_,
                        audio_.data(), audio_.size() * sizeof(float));
    }
    std::memcpy(output, audio_.data(), audio_.size() * sizeof(float));
}

} // namespace vd
