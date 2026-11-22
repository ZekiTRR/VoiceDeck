#pragma once

namespace vd {

struct App;

// Runs the Voicemod-style WebView2 front-end on the calling thread (which
// becomes the UI/STA thread with a Win32 message loop). Returns the process
// exit code.
int runUi(App& app);

} // namespace vd
