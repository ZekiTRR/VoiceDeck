#include "core/platform/ThreadAffinity.hpp"

#include <windows.h>

#include <algorithm>
#include <vector>

namespace vd {
namespace {

using GetSystemCpuSetInformationFn = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG,
                                                   HANDLE, ULONG);
using SetThreadSelectedCpuSetsFn = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);

} // namespace

void pinThreadToPerformanceCores(void* nativeThreadHandle) {
    if (nativeThreadHandle == nullptr) return;
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (!kernel) return;
    auto getInfo = reinterpret_cast<GetSystemCpuSetInformationFn>(
        GetProcAddress(kernel, "GetSystemCpuSetInformation"));
    auto setSets = reinterpret_cast<SetThreadSelectedCpuSetsFn>(
        GetProcAddress(kernel, "SetThreadSelectedCpuSets"));
    if (!getInfo || !setSets) return;

    ULONG length = 0;
    getInfo(nullptr, 0, &length, nullptr, 0);
    if (length == 0) return;
    std::vector<unsigned char> buffer(length);
    if (!getInfo(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()), length, &length,
                 nullptr, 0))
        return;

    // Windows reports a higher EfficiencyClass for P-cores. On a non-hybrid
    // CPU all classes are equal, so we end up selecting every core (a no-op).
    DWORD bestClass = 0;
    bool any = false;
    for (ULONG offset = 0; offset < length;) {
        auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data() + offset);
        if (info->Size == 0) break;
        if (info->Type == CpuSetInformation) {
            bestClass = std::max(bestClass, DWORD(info->CpuSet.EfficiencyClass));
            any = true;
        }
        offset += info->Size;
    }
    if (!any) return;

    std::vector<ULONG> ids;
    for (ULONG offset = 0; offset < length;) {
        auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data() + offset);
        if (info->Size == 0) break;
        if (info->Type == CpuSetInformation && info->CpuSet.EfficiencyClass == bestClass)
            ids.push_back(info->CpuSet.Id);
        offset += info->Size;
    }
    if (!ids.empty())
        setSets(static_cast<HANDLE>(nativeThreadHandle), ids.data(), ULONG(ids.size()));
}

} // namespace vd
