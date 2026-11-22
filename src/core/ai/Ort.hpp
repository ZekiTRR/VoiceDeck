#pragma once

// Dynamic loading of ONNX Runtime (DirectML build). The library is optional:
// when onnxruntime.dll is missing, AI features degrade gracefully to
// "not available" and the rest of the app works.

#include "onnxruntime_c_api.h"

#include <string>

namespace vd {

class OrtLib {
public:
    static OrtLib& get() {
        static OrtLib inst;
        return inst;
    }

    bool loaded = false;
    const OrtApi* api = nullptr;
    // OrtSessionOptionsAppendExecutionProvider_DML(OrtSessionOptions*, int)
    void* dmlAppend = nullptr;
    // OrtSessionOptionsAppendExecutionProvider_CUDA(OrtSessionOptions*, int) -
    // only present in onnxruntime GPU builds; the app falls back gracefully.
    void* cudaAppend = nullptr;

    // Throws std::runtime_error on API misuse; returns "" on success.
    static std::string statusMessage(OrtStatus* s, const OrtApi* api) {
        if (s == nullptr) return "";
        const char* msg = api->GetErrorMessage(s);
        std::string err = msg ? msg : "unknown ONNX Runtime error";
        api->ReleaseStatus(s);
        return err;
    }

private:
    OrtLib();
    void* dll_ = nullptr;
};

} // namespace vd
