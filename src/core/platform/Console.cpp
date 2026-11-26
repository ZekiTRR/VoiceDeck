#include "core/platform/Console.hpp"

#include <windows.h>

#include <cstdio>

namespace vd::console {
namespace {

bool isConsoleHandle(HANDLE h) {
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return true;
    const DWORD type = GetFileType(h);
    return type == FILE_TYPE_CHAR || type == FILE_TYPE_UNKNOWN;
}

void configureConsole() {
    SetConsoleOutputCP(CP_UTF8);
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out != nullptr && out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode))
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

void silenceStdio() {
    FILE* ignored = nullptr;
    freopen_s(&ignored, "NUL", "w", stdout);
    freopen_s(&ignored, "NUL", "w", stderr);
    freopen_s(&ignored, "NUL", "r", stdin);
}

} // namespace

void ensureConsole() {
    if (GetConsoleWindow() != nullptr) {
        configureConsole();
        return;
    }

    const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const bool redirectIn = isConsoleHandle(in);
    const bool redirectOut = isConsoleHandle(out);
    const bool redirectErr = isConsoleHandle(err);

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();

    FILE* ignored = nullptr;
    if (redirectOut) {
        freopen_s(&ignored, "CONOUT$", "w", stdout);
        freopen_s(&ignored, "CONOUT$", "w", stderr);
    } else if (redirectErr) {
        freopen_s(&ignored, "CONOUT$", "w", stderr);
    }
    if (redirectIn) freopen_s(&ignored, "CONIN$", "r", stdin);

    configureConsole();
}

void hide() {
    // FreeConsole never closes the parent's terminal; it only detaches this
    // process. GUI logging then goes to NUL instead of an invisible buffer.
    FreeConsole();
    silenceStdio();
}

} // namespace vd::console
