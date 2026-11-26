#pragma once

namespace vd::console {

// Attach to the launcher's console (or create one) and make the C stdio
// streams usable there. Already-redirected stdin/stdout (pipes/files) are
// preserved, so CLI automation keeps working.
void ensureConsole();

// Detach from any console and send GUI logging to NUL. Never closes the
// launcher's terminal.
void hide();

} // namespace vd::console
