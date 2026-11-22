#include "core/ai/Ort.hpp"

#include "core/util/Log.hpp"

#include <windows.h>

namespace vd {

OrtLib::OrtLib() {
    // Try the exe directory first, then the default search path.
    HMODULE h = nullptr;
    char exeDir[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exeDir, MAX_PATH) > 0) {
        std::string p(exeDir);
        const std::size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos) {
            p = p.substr(0, slash + 1) + "onnxruntime.dll";
            h = LoadLibraryA(p.c_str());
        }
    }
    if (!h) h = LoadLibraryA("onnxruntime.dll");
    if (!h) return;
    dll_ = h;

    auto getBase = reinterpret_cast<const OrtApiBase* (__stdcall*)()>(
        reinterpret_cast<void*>(GetProcAddress(h, "OrtGetApiBase")));
    if (!getBase) return;
    const OrtApiBase* base = getBase();
    if (!base) return;
    api = base->GetApi(ORT_API_VERSION);
    if (!api) return;
    dmlAppend = reinterpret_cast<void*>(GetProcAddress(h, "OrtSessionOptionsAppendExecutionProvider_DML"));
    cudaAppend = reinterpret_cast<void*>(GetProcAddress(h, "OrtSessionOptionsAppendExecutionProvider_CUDA"));
    if (!dmlAppend) log::warn("onnxruntime: DirectML provider not found (CPU fallback only)");
    loaded = true;
}

} // namespace vd
