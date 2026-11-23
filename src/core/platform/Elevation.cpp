#include "core/platform/Elevation.hpp"

#include <windows.h>
#include <shellapi.h>

#include <array>

namespace vd {
namespace {

std::wstring utf8ToUtf16(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], len);
    return w;
}

std::wstring exePath() {
    std::array<wchar_t, 32768> buf{};
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    if (n == 0 || n >= buf.size()) return {};
    return std::wstring(buf.data(), n);
}

} // namespace

bool isElevated() {
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID group = nullptr;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &group))
        return false;
    BOOL member = FALSE;
    const BOOL ok = CheckTokenMembership(nullptr, group, &member);
    FreeSid(group);
    return ok && member;
}

ElevateResult relaunchElevated(const std::string& args) {
    const std::wstring exe = exePath();
    if (exe.empty()) return ElevateResult::Failed;
    std::wstring params = utf8ToUtf16(args);
    if (!params.empty()) params += L" ";
    params += L"--elevated";

    const HINSTANCE h = ShellExecuteW(nullptr, L"runas", exe.c_str(), params.c_str(), nullptr,
                                      SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) > 32) return ElevateResult::Launched;
    const DWORD err = GetLastError();
    if (err == ERROR_CANCELLED) return ElevateResult::Cancelled;
    return ElevateResult::Failed;
}

} // namespace vd
