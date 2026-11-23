#pragma once

#include <string>

namespace vd {

// Windows "Run" autostart entry for the current user (HKCU, no admin needed).
// The value stores a quoted absolute path to the current executable.
bool isAutoStartEnabled();
// Returns "" on success, else a human-readable error.
std::string setAutoStart(bool on);

} // namespace vd
