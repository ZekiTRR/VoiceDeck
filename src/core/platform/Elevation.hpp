#pragma once

#include <string>

namespace vd {

// True when the current process token belongs to the local Administrators
// group (i.e. the app was launched "as administrator").
bool isElevated();

enum class ElevateResult { Launched, Cancelled, Failed };

// Relaunches the current exe with "runas" (UAC prompt). `args` is the original
// command line arguments (e.g. "--cli"); "--elevated" is appended so the new
// process never tries to elevate itself again.
ElevateResult relaunchElevated(const std::string& args);

} // namespace vd
