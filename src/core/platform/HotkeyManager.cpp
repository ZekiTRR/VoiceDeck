#include "core/platform/HotkeyManager.hpp"

#include "core/util/Log.hpp"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace vd {
namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_PROCESS = WM_APP + 2;
constexpr UINT kTrayIconId = 1;

LRESULT CALLBACK trayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<HotkeyManager*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcA(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_APP_TRAY: {
            if (wParam != kTrayIconId) return 0;
            switch (LOWORD(lParam)) {
                case WM_LBUTTONUP: // left click toggles mic passthrough
                    self->onTrayLeftClick();
                    return 0;
                case WM_RBUTTONUP:
                    self->onTrayRightClick();
                    return 0;
                default:
                    return 0;
            }
        }
        case WM_APP_PROCESS:
            self->processPendingRegistrations();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

} // namespace

HotkeyManager::~HotkeyManager() { stop(); }

bool HotkeyManager::start(TriggerFn onHotkey, SimpleFn onStopAll, SimpleFn onToggleBypass,
                          SimpleFn onExit) {
    if (running_.load(std::memory_order_relaxed)) return true;
    onHotkey_ = std::move(onHotkey);
    onStopAll_ = std::move(onStopAll);
    onToggleBypass_ = std::move(onToggleBypass);
    onExit_ = std::move(onExit);

    readyEvent_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!readyEvent_) return false;
    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread([this] { threadMain(); });
    WaitForSingleObject(static_cast<HANDLE>(readyEvent_), 2000);
    CloseHandle(static_cast<HANDLE>(readyEvent_));
    readyEvent_ = nullptr;
    return true;
}

void HotkeyManager::stop() {
    if (!running_.exchange(false)) return;
    if (hwnd_) PostMessageA(static_cast<HWND>(hwnd_), WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
}

void HotkeyManager::threadMain() {
    WNDCLASSA wc{};
    wc.style = 0;
    wc.lpfnWndProc = &trayWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "VoiceDeckTrayWnd";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "VoiceDeckTrayWnd", "VoiceDeck", WS_POPUP, 0, 0, 0, 0, nullptr,
                                nullptr, wc.hInstance, this);
    hwnd_ = hwnd;
    if (hwnd) SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    NOTIFYICONDATAA nid{};
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    strcpy_s(nid.szTip, "VoiceDeck");
    trayAdded_ = Shell_NotifyIconA(NIM_ADD, &nid) != FALSE;

    processPending(); // bindings queued while the thread was spinning up
    if (readyEvent_) SetEvent(static_cast<HANDLE>(readyEvent_));

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY && msg.hwnd == nullptr) {
            auto it = bindings_.find(static_cast<int>(msg.wParam));
            if (it != bindings_.end()) {
                if (it->second.kind == HotkeyAction::Kind::StopAll) {
                    if (onStopAll_) onStopAll_();
                } else if (onHotkey_) {
                    onHotkey_(it->second);
                }
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    if (trayAdded_) {
        NOTIFYICONDATAA d{};
        d.hWnd = hwnd;
        d.uID = kTrayIconId;
        Shell_NotifyIconA(NIM_DELETE, &d);
        trayAdded_ = false;
    }
    for (const auto& [hkId, act] : bindings_) UnregisterHotKey(nullptr, hkId);
    bindings_.clear();
    hotkeyBySound_.clear();
    hotkeyByPreset_.clear();
    stopAllHotkeyId_ = -1;
    if (hwnd) DestroyWindow(hwnd);
    hwnd_ = nullptr;
}

void HotkeyManager::processPending() {
    std::vector<Pending> todo;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        todo.swap(pending_);
    }
    for (const Pending& p : todo) {
        if (p.add) {
            if (!RegisterHotKey(nullptr, p.hotkeyId, p.mods | MOD_NOREPEAT, p.vk)) {
                log::warn("hotkey busy (taken by another app?), combo dropped");
                std::lock_guard<std::mutex> lk(combosMtx_);
                switch (p.action.kind) {
                    case HotkeyAction::Kind::Sound: soundCombos_.erase(p.action.id); break;
                    case HotkeyAction::Kind::Preset: presetCombos_.erase(p.action.id); break;
                    case HotkeyAction::Kind::StopAll: stopAllCombo_.clear(); break;
                    case HotkeyAction::Kind::VoiceToggle: voiceToggleCombo_.clear(); break;
                    case HotkeyAction::Kind::AiToggle: aiToggleCombo_.clear(); break;
                }
                continue;
            }
            bindings_[p.hotkeyId] = p.action;
            switch (p.action.kind) {
                case HotkeyAction::Kind::Sound: hotkeyBySound_[p.action.id] = p.hotkeyId; break;
                case HotkeyAction::Kind::Preset: hotkeyByPreset_[p.action.id] = p.hotkeyId; break;
                case HotkeyAction::Kind::StopAll: stopAllHotkeyId_ = p.hotkeyId; break;
                case HotkeyAction::Kind::VoiceToggle: voiceHotkeyId_ = p.hotkeyId; break;
                case HotkeyAction::Kind::AiToggle: aiHotkeyId_ = p.hotkeyId; break;
            }
        } else {
            int hkId = -1;
            switch (p.action.kind) {
                case HotkeyAction::Kind::Sound: {
                    auto it = hotkeyBySound_.find(p.action.id);
                    if (it != hotkeyBySound_.end()) hkId = it->second;
                    break;
                }
                case HotkeyAction::Kind::Preset: {
                    auto it = hotkeyByPreset_.find(p.action.id);
                    if (it != hotkeyByPreset_.end()) hkId = it->second;
                    break;
                }
                case HotkeyAction::Kind::StopAll: hkId = stopAllHotkeyId_; break;
                case HotkeyAction::Kind::VoiceToggle: hkId = voiceHotkeyId_; break;
                case HotkeyAction::Kind::AiToggle: hkId = aiHotkeyId_; break;
            }
            if (hkId >= 0) {
                UnregisterHotKey(nullptr, hkId);
                bindings_.erase(hkId);
                switch (p.action.kind) {
                    case HotkeyAction::Kind::Sound: hotkeyBySound_.erase(p.action.id); break;
                    case HotkeyAction::Kind::Preset: hotkeyByPreset_.erase(p.action.id); break;
                    case HotkeyAction::Kind::StopAll: stopAllHotkeyId_ = -1; break;
                    case HotkeyAction::Kind::VoiceToggle: voiceHotkeyId_ = -1; break;
                    case HotkeyAction::Kind::AiToggle: aiHotkeyId_ = -1; break;
                }
            }
        }
    }
}

void HotkeyManager::showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING, 1,
               bypassLabel_.load(std::memory_order_relaxed)
                   ? "Bypass: ON  (turn OFF)"
                   : "Bypass: OFF  (turn ON)");
    AppendMenuA(menu, MF_STRING, 2, "Stop all sounds");
    AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(menu, MF_STRING, 3, "Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(static_cast<HWND>(hwnd_));
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0,
                                   static_cast<HWND>(hwnd_), nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case 1:
            if (onToggleBypass_) onToggleBypass_();
            break;
        case 2:
            if (onStopAll_) onStopAll_();
            break;
        case 3:
            if (onExit_) onExit_();
            break;
        default:
            break;
    }
}

std::string HotkeyManager::bindInternal(const HotkeyAction& action, const std::string& combo) {
    if (!running_.load(std::memory_order_relaxed)) return "hotkey service is not running";
    // An empty combo is the explicit unbind operation used by the UI. Keep it
    // here so every target (voice, AI, stop-all, preset and sound) has the same
    // persistence and unregistration semantics.
    if (combo.empty()) {
        unbindInternal(action);
        return "";
    }
    std::uint32_t mods = 0;
    std::uint32_t vk = 0;
    std::string err;
    if (!parseCombo(combo, mods, vk, err)) return err;

    // Replace any previous binding for the same target.
    unbindInternal(action);

    const int hkId = nextHotkeyId_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_.push_back(Pending{true, hkId, mods, vk, action});
    }
    {
        std::lock_guard<std::mutex> lk(combosMtx_);
        switch (action.kind) {
            case HotkeyAction::Kind::Sound: soundCombos_[action.id] = combo; break;
            case HotkeyAction::Kind::Preset: presetCombos_[action.id] = combo; break;
            case HotkeyAction::Kind::StopAll: stopAllCombo_ = combo; break;
            case HotkeyAction::Kind::VoiceToggle: voiceToggleCombo_ = combo; break;
            case HotkeyAction::Kind::AiToggle: aiToggleCombo_ = combo; break;
        }
    }
    PostMessageA(static_cast<HWND>(hwnd_), WM_APP_PROCESS, 0, 0);
    return "";
}

void HotkeyManager::unbindInternal(const HotkeyAction& action) {
    {
        std::lock_guard<std::mutex> lk(combosMtx_);
        switch (action.kind) {
            case HotkeyAction::Kind::Sound: soundCombos_.erase(action.id); break;
            case HotkeyAction::Kind::Preset: presetCombos_.erase(action.id); break;
            case HotkeyAction::Kind::StopAll: stopAllCombo_.clear(); break;
            case HotkeyAction::Kind::VoiceToggle: voiceToggleCombo_.clear(); break;
            case HotkeyAction::Kind::AiToggle: aiToggleCombo_.clear(); break;
        }
    }
    if (!running_.load(std::memory_order_relaxed)) return;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_.push_back(Pending{false, 0, 0, 0, action});
    }
    PostMessageA(static_cast<HWND>(hwnd_), WM_APP_PROCESS, 0, 0);
}

std::string HotkeyManager::bindSound(std::uint64_t soundId, const std::string& combo) {
    if (soundId == 0) return "bad sound id";
    return bindInternal(HotkeyAction{HotkeyAction::Kind::Sound, soundId}, combo);
}

std::string HotkeyManager::bindPreset(std::uint64_t presetIndex, const std::string& combo) {
    return bindInternal(HotkeyAction{HotkeyAction::Kind::Preset, presetIndex}, combo);
}

std::string HotkeyManager::bindStopAll(const std::string& combo) {
    return bindInternal(HotkeyAction{HotkeyAction::Kind::StopAll, 0}, combo);
}

std::string HotkeyManager::bindVoiceToggle(const std::string& combo) {
    return bindInternal(HotkeyAction{HotkeyAction::Kind::VoiceToggle, 0}, combo);
}

std::string HotkeyManager::bindAiToggle(const std::string& combo) {
    return bindInternal(HotkeyAction{HotkeyAction::Kind::AiToggle, 0}, combo);
}

void HotkeyManager::unbindSound(std::uint64_t soundId) {
    unbindInternal(HotkeyAction{HotkeyAction::Kind::Sound, soundId});
}

void HotkeyManager::unbindPreset(std::uint64_t presetIndex) {
    unbindInternal(HotkeyAction{HotkeyAction::Kind::Preset, presetIndex});
}

std::map<std::uint64_t, std::string> HotkeyManager::allSoundCombos() const {
    std::lock_guard<std::mutex> lk(combosMtx_);
    return soundCombos_;
}

std::map<std::uint64_t, std::string> HotkeyManager::allPresetCombos() const {
    std::lock_guard<std::mutex> lk(combosMtx_);
    return presetCombos_;
}

std::string HotkeyManager::stopAllCombo() const {
    std::lock_guard<std::mutex> lk(combosMtx_);
    return stopAllCombo_;
}

std::string HotkeyManager::voiceToggleCombo() const {
    std::lock_guard<std::mutex> lk(combosMtx_);
    return voiceToggleCombo_;
}

std::string HotkeyManager::aiToggleCombo() const {
    std::lock_guard<std::mutex> lk(combosMtx_);
    return aiToggleCombo_;
}

bool HotkeyManager::parseCombo(const std::string& combo, std::uint32_t& modsOut,
                                std::uint32_t& vkOut, std::string& err) {
    // Tokenize by '+' with quote-free, space-tolerant lowercasing.
    std::vector<std::string> tokens;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty()) tokens.push_back(cur);
        cur.clear();
    };
    for (char ch : combo) {
        if (ch == '+') flush();
        else if (ch != ' ') cur.push_back(char(std::tolower(static_cast<unsigned char>(ch))));
    }
    flush();

    if (tokens.size() < 2) {
        err = "combo must be like Ctrl+Alt+1 (at least one modifier + one key)";
        return false;
    }

    std::uint32_t mods = 0;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string& m = tokens[i];
        if (m == "ctrl" || m == "control") mods |= MOD_CONTROL;
        else if (m == "alt") mods |= MOD_ALT;
        else if (m == "shift") mods |= MOD_SHIFT;
        else if (m == "win") mods |= MOD_WIN;
        else {
            err = "unknown modifier: " + m;
            return false;
        }
    }
    if (mods == 0) {
        err = "combo needs at least one modifier (Ctrl/Alt/Shift/Win)";
        return false;
    }

    const std::string& key = tokens.back();
    std::uint32_t vk = 0;
    if (key.size() == 1) {
        const char c = char(std::toupper(static_cast<unsigned char>(key[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) vk = static_cast<std::uint32_t>(c);
    } else if (key.size() >= 2 && key[0] == 'f' && key.size() <= 3) {
        const int n = std::atoi(key.c_str() + 1);
        if (n >= 1 && n <= 24) vk = VK_F1 + static_cast<std::uint32_t>(n - 1);
    } else if (key.size() == 7 && key.rfind("numpad", 0) == 0) {
        const char d = key[6];
        if (d >= '0' && d <= '9') vk = VK_NUMPAD0 + static_cast<std::uint32_t>(d - '0');
    }
    if (vk == 0) {
        err = "unsupported key: " + key + " (use 0-9, A-Z, F1-F24 or Numpad0-9)";
        return false;
    }

    modsOut = mods;
    vkOut = vk;
    return true;
}

} // namespace vd
