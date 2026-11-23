#include "core/platform/AutoStart.hpp"

#include <windows.h>

#include <array>

namespace vd {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"VoiceDeck";

std::wstring exePath() {
    std::array<wchar_t, 32768> buf{};
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    if (n == 0 || n >= buf.size()) return {};
    return std::wstring(buf.data(), n);
}

} // namespace

bool isAutoStartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    wchar_t value[2048] = {};
    DWORD type = REG_SZ;
    DWORD size = sizeof(value);
    const LONG rc = RegQueryValueExW(key, kValueName, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && size > 0 && value[0] != L'\0';
}

std::string setAutoStart(bool on) {
    if (!on) {
        HKEY key = nullptr;
        const LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
        if (rc != ERROR_SUCCESS) return "не удалось открыть раздел автозапуска";
        const LONG del = RegDeleteValueW(key, kValueName);
        RegCloseKey(key);
        if (del != ERROR_SUCCESS && del != ERROR_FILE_NOT_FOUND)
            return "не удалось отключить автозапуск";
        return "";
    }

    const std::wstring path = exePath();
    if (path.empty()) return "не удалось определить путь к приложению";
    const std::wstring command = L"\"" + path + L"\"";

    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return "не удалось открыть раздел автозапуска";
    rc = RegSetValueExW(key, kValueName, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(command.c_str()),
                        DWORD((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) return "не удалось записать автозапуск";
    return "";
}

} // namespace vd
