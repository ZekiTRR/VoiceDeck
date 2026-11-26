#include "core/capture/AppAudioCaptureManager.hpp"

#include <windows.h>
#include <Audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <map>
#include <set>
#include <thread>

namespace vd {
namespace {

using Microsoft::WRL::ComPtr;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(std::size_t(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), bytes,
                        nullptr, nullptr);
    return out;
}

std::string queryProcessName(std::uint32_t pid) {
    if (!pid || pid == 4 || pid == GetCurrentProcessId()) return {};
    const HANDLE process =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process) return {};
    wchar_t path[1024] = {};
    DWORD size = DWORD(sizeof(path) / sizeof(path[0]));
    std::string name;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        const std::wstring full(path, size);
        const std::size_t slash = full.find_last_of(L"\\/");
        name = wideToUtf8(slash == std::wstring::npos ? full : full.substr(slash + 1));
    }
    CloseHandle(process);
    return name;
}

std::wstring windowTitle(HWND hwnd) {
    wchar_t buffer[512] = {};
    GetWindowTextW(hwnd, buffer, int(sizeof(buffer) / sizeof(buffer[0])));
    return buffer;
}

struct WindowEnumContext {
    DWORD selfPid = GetCurrentProcessId();
    std::map<DWORD, std::string> names;
    std::map<DWORD, std::wstring> titles;
};

std::string processNameCached(DWORD pid, WindowEnumContext& ctx) {
    auto it = ctx.names.find(pid);
    if (it != ctx.names.end()) return it->second;
    std::string name = queryProcessName(pid);
    ctx.names.emplace(pid, name);
    return name;
}

BOOL CALLBACK enumChildProc(HWND hwnd, LPARAM lp) {
    auto* pair = reinterpret_cast<std::pair<WindowEnumContext*, DWORD>*>(lp);
    if (!pair || !pair->first) return TRUE;
    auto& ctx = *pair->first;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == ctx.selfPid || pid == pair->second) return TRUE;
    const std::string name = processNameCached(pid, ctx);
    if (name.empty() || lower(name) == "applicationframehost.exe") return TRUE;
    const std::wstring childTitle = windowTitle(hwnd);
    auto it = ctx.titles.find(pid);
    if (it == ctx.titles.end() || it->second.empty())
        ctx.titles[pid] = childTitle.empty() ? ctx.titles[pair->second] : childTitle;
    return TRUE;
}

BOOL CALLBACK enumWindowProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<WindowEnumContext*>(lp);
    if (!ctx || !IsWindowVisible(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == ctx->selfPid) return TRUE;
    const std::wstring title = windowTitle(hwnd);
    const std::string name = processNameCached(pid, *ctx);
    if (name.empty()) {
        if (!title.empty()) ctx->titles[pid] = title;
        return TRUE;
    }
    if (lower(name) == "applicationframehost.exe") {
        auto pair = std::make_pair(ctx, pid);
        EnumChildWindows(hwnd, enumChildProc, reinterpret_cast<LPARAM>(&pair));
        return TRUE;
    }
    if (!title.empty() || ctx->titles.find(pid) == ctx->titles.end())
        ctx->titles[pid] = title;
    return TRUE;
}

std::string stateName(CaptureState state) {
    switch (state) {
        case CaptureState::Starting: return "starting";
        case CaptureState::Active: return "active";
        case CaptureState::Silent: return "silent";
        case CaptureState::Stopping: return "stopping";
        case CaptureState::Error: return "error";
        default: return "idle";
    }
}

} // namespace

AppAudioCaptureManager::~AppAudioCaptureManager() { shutdown(); }

void AppAudioCaptureManager::initialize() {
    supported_ = false;
    unsupportedReason_.clear();

    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion =
        ntdll ? reinterpret_cast<RtlGetVersionFn>(
                    GetProcAddress(ntdll, "RtlGetVersion"))
              : nullptr;
    if (!rtlGetVersion || rtlGetVersion(&version) != 0 || version.dwBuildNumber < 20348) {
        unsupportedReason_ = "Process Loopback требует Windows 10 build 20348+ или Windows 11";
        return;
    }

    const HMODULE mmdevapi = LoadLibraryW(L"mmdevapi.dll");
    if (!mmdevapi || !GetProcAddress(mmdevapi, "ActivateAudioInterfaceAsync")) {
        unsupportedReason_ = "WASAPI Process Loopback недоступен в этой версии Windows";
        return;
    }
    supported_ = true;
}

void AppAudioCaptureManager::shutdown() {
    for (auto& slot : slots_) slot.requestStop();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    for (auto& slot : slots_) {
        slot.setInactive();
        for (int i = 0; i < 100 && slot.audioBusy(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        slot.stopAndJoin();
    }
    {
        std::lock_guard<std::mutex> lock(metaMutex_);
        for (auto& meta : meta_) meta = {};
    }
}

void AppAudioCaptureManager::enumerateProcesses(std::vector<CaptureProcessInfo>& out) {
    out.clear();
    std::set<DWORD> sessionPids;
    std::set<DWORD> audiblePids;

    ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(deviceEnumerator.GetAddressOf())))) {
        ComPtr<IMMDeviceCollection> endpoints;
        if (SUCCEEDED(deviceEnumerator->EnumAudioEndpoints(
                eRender, DEVICE_STATE_ACTIVE, endpoints.GetAddressOf()))) {
            UINT count = 0;
            endpoints->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> endpoint;
                if (FAILED(endpoints->Item(i, endpoint.GetAddressOf()))) continue;
                ComPtr<IUnknown> unknown;
                if (FAILED(endpoint->Activate(
                        __uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(unknown.GetAddressOf()))))
                    continue;
                ComPtr<IAudioSessionManager2> manager;
                if (FAILED(unknown.As(&manager))) continue;
                ComPtr<IAudioSessionEnumerator> sessions;
                if (FAILED(manager->GetSessionEnumerator(sessions.GetAddressOf()))) continue;
                int sessionCount = 0;
                sessions->GetCount(&sessionCount);
                for (int s = 0; s < sessionCount; ++s) {
                    ComPtr<IAudioSessionControl> control;
                    if (FAILED(sessions->GetSession(s, control.GetAddressOf()))) continue;
                    ComPtr<IAudioSessionControl2> control2;
                    if (FAILED(control.As(&control2))) continue;
                    DWORD pid = 0;
                    if (FAILED(control2->GetProcessId(&pid)) || !pid) continue;
                    sessionPids.insert(pid);
                    AudioSessionState state = AudioSessionStateInactive;
                    if (SUCCEEDED(control->GetState(&state)) &&
                        state == AudioSessionStateActive)
                        audiblePids.insert(pid);
                }
            }
        }
    }

    WindowEnumContext windows;
    EnumWindows(enumWindowProc, reinterpret_cast<LPARAM>(&windows));

    std::set<DWORD> pids = sessionPids;
    for (const auto& [pid, title] : windows.titles) {
        (void)title;
        pids.insert(pid);
    }
    for (DWORD pid : pids) {
        if (!pid || pid == GetCurrentProcessId() || pid == 4) continue;
        CaptureProcessInfo info;
        info.pid = pid;
        const std::string name = queryProcessName(pid);
        if (lower(name) == "applicationframehost.exe") continue;
        info.name = name.empty() ? "PID " + std::to_string(pid) : name;
        auto title = windows.titles.find(pid);
        if (title != windows.titles.end()) info.title = wideToUtf8(title->second);
        info.audible = audiblePids.count(pid) != 0;
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const CaptureProcessInfo& a,
                                         const CaptureProcessInfo& b) {
        if (a.audible != b.audible) return a.audible > b.audible;
        const std::string an = lower(a.name), bn = lower(b.name);
        if (an != bn) return an < bn;
        return a.pid < b.pid;
    });
}

void AppAudioCaptureManager::refreshProcesses() {
    if (!supported_) {
        processes_.clear();
        return;
    }
    enumerateProcesses(processes_);
}

nlohmann::json AppAudioCaptureManager::processesJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& process : processes_) {
        arr.push_back({{"pid", process.pid},
                       {"name", process.name},
                       {"title", process.title},
                       {"audible", process.audible}});
    }
    return arr;
}

std::size_t AppAudioCaptureManager::findFreeSlot() const {
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].state() == CaptureState::Idle) return i;
    return kMaxSources;
}

bool AppAudioCaptureManager::add(std::uint32_t pid, std::string& err) {
    if (!supported_) {
        err = unsupportedReason_;
        return false;
    }
    if (!pid || pid == GetCurrentProcessId()) {
        err = "нельзя захватывать VoiceDeck";
        return false;
    }
    for (const auto& slot : slots_) {
        if (slot.state() != CaptureState::Idle && slot.pid() == pid) {
            err = "это приложение уже захватывается";
            return false;
        }
    }
    const std::size_t index = findFreeSlot();
    if (index >= kMaxSources) {
        err = "достигнут лимит 8 захватов";
        return false;
    }
    if (!slots_[index].start(pid, err)) return false;
    slots_[index].setGain(1.0f);
    {
        std::lock_guard<std::mutex> lock(metaMutex_);
        meta_[index].pid = pid;
        meta_[index].name = queryProcessName(pid);
        for (const auto& process : processes_) {
            if (process.pid == pid) {
                meta_[index].name = process.name;
                meta_[index].title = process.title;
                break;
            }
        }
    }
    return true;
}

void AppAudioCaptureManager::remove(std::size_t slot) {
    if (slot >= kMaxSources) return;
    slots_[slot].requestStop();
    std::this_thread::sleep_for(std::chrono::milliseconds(45));
    slots_[slot].setInactive();
    for (int i = 0; i < 100 && slots_[slot].audioBusy(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    slots_[slot].stopAndJoin();
    std::lock_guard<std::mutex> lock(metaMutex_);
    meta_[slot] = {};
}

bool AppAudioCaptureManager::reapFinished() {
    bool removed = false;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].state() != CaptureState::Error) continue;
        slots_[i].setInactive();
        for (int n = 0; n < 100 && slots_[i].audioBusy(); ++n)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        slots_[i].stopAndJoin();
        std::lock_guard<std::mutex> lock(metaMutex_);
        meta_[i] = {};
        removed = true;
    }
    return removed;
}

void AppAudioCaptureManager::setGain(std::size_t slot, float gain) {
    if (slot < kMaxSources) slots_[slot].setGain(gain);
}

nlohmann::json AppAudioCaptureManager::sourcesJson() const {
    nlohmann::json arr = nlohmann::json::array();
    std::lock_guard<std::mutex> lock(metaMutex_);
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const auto state = slots_[i].state();
        if (state == CaptureState::Idle && meta_[i].pid == 0) continue;
        arr.push_back({{"id", i},
                       {"pid", meta_[i].pid},
                       {"name", meta_[i].name},
                       {"title", meta_[i].title},
                       {"gain", slots_[i].gain()},
                       {"level", slots_[i].level()},
                       {"frames", slots_[i].capturedFrames()},
                       {"silentFrames", slots_[i].silentFrames()},
                       {"state", stateName(state)},
                       {"error", static_cast<std::uint32_t>(slots_[i].error())}});
    }
    return arr;
}

nlohmann::json AppAudioCaptureManager::metersJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const auto state = slots_[i].state();
        if (state == CaptureState::Idle) continue;
        arr.push_back({{"id", i},
                       {"level", slots_[i].level()},
                       {"state", stateName(state)}});
    }
    return arr;
}

void AppAudioCaptureManager::mixInto(float* out, std::size_t frames) {
    for (auto& slot : slots_) slot.mixInto(out, frames);
}

} // namespace vd
