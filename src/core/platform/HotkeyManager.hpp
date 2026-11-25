#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vd {

struct HotkeyAction {
    enum class Kind : std::uint8_t { Sound, StopAll, Preset, VoiceToggle, AiToggle };
    Kind kind = Kind::Sound;
    std::uint64_t id = 0; // soundId, or preset index for Kind::Preset
};

// Global hotkeys + tray icon on a dedicated Win32 message-loop thread.
//
// RegisterHotKey/UnregisterHotKey are thread-affine, so registrations from
// other threads are marshalled through a pending list processed on the Win32
// thread itself. The trigger callbacks (onHotkey/onStopAll/...) run on that
// thread and must be thread-safe (the engine command queue is).
class HotkeyManager {
public:
    using TriggerFn = std::function<void(const HotkeyAction&)>;
    using SimpleFn = std::function<void()>;

    ~HotkeyManager();

    bool start(TriggerFn onHotkey, SimpleFn onStopAll, SimpleFn onToggleBypass, SimpleFn onExit);
    void stop();

    // Bind "Ctrl+Alt+1"-style combos. Rebinding the same target replaces the
    // old combo; an empty combo clears it. Return "" on success, else a
    // human-readable error.
    std::string bindSound(std::uint64_t soundId, const std::string& combo);
    std::string bindPreset(std::uint64_t presetIndex, const std::string& combo);
    std::string bindStopAll(const std::string& combo);
    std::string bindVoiceToggle(const std::string& combo); // DSP chain on/off
    std::string bindAiToggle(const std::string& combo);    // AI voice on/off
    void unbindSound(std::uint64_t soundId);              // clears a sound's hotkey
    void unbindPreset(std::uint64_t presetIndex);          // preset removal rebinds

    // Persistence helpers (soundId/presetIndex -> combo).
    std::map<std::uint64_t, std::string> allSoundCombos() const;
    std::map<std::uint64_t, std::string> allPresetCombos() const;
    std::string stopAllCombo() const;
    std::string voiceToggleCombo() const;
    std::string aiToggleCombo() const;

    // Tray label state, updated by the CLI whenever bypass toggles.
    void setBypassLabel(bool on) { bypassLabel_.store(on, std::memory_order_relaxed); }

    // --- internal: hooks called from the Win32 window procedure ---
    void onTrayLeftClick() { if (onToggleBypass_) onToggleBypass_(); }
    void onTrayRightClick() { showTrayMenu(); }
    void processPendingRegistrations() { processPending(); }

    // Parses "Ctrl+Alt+1" / "Shift+F5" / "Win+Numpad3" into Win32 mods+vk.
    static bool parseCombo(const std::string& combo, std::uint32_t& mods, std::uint32_t& vk,
                           std::string& err);

private:
    struct Pending {
        bool add = true;
        int hotkeyId = 0;
        std::uint32_t mods = 0;
        std::uint32_t vk = 0;
        HotkeyAction action{};
    };

    std::string bindInternal(const HotkeyAction& action, const std::string& combo);
    void unbindInternal(const HotkeyAction& action);
    void threadMain();
    void processPending();
    void showTrayMenu();

    // Win32-thread-only state.
    void* hwnd_ = nullptr;
    std::map<int, HotkeyAction> bindings_;
    std::map<std::uint64_t, int> hotkeyBySound_;
    std::map<std::uint64_t, int> hotkeyByPreset_;
    int stopAllHotkeyId_ = -1;
    int voiceHotkeyId_ = -1;
    int aiHotkeyId_ = -1;
    bool trayAdded_ = false;

    // Cross-thread state.
    std::atomic<int> nextHotkeyId_{1};
    std::atomic<bool> running_{false};
    std::atomic<bool> bypassLabel_{true};
    std::mutex pendingMtx_;
    std::vector<Pending> pending_;
    mutable std::mutex combosMtx_;
    std::map<std::uint64_t, std::string> soundCombos_;
    std::map<std::uint64_t, std::string> presetCombos_;
    std::string stopAllCombo_;
    std::string voiceToggleCombo_;
    std::string aiToggleCombo_;

    TriggerFn onHotkey_;
    SimpleFn onStopAll_;
    SimpleFn onToggleBypass_;
    SimpleFn onExit_;
    std::thread thread_;
    void* readyEvent_ = nullptr;
};

} // namespace vd
