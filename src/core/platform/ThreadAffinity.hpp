#pragma once

namespace vd {

// Softly restricts an audio device thread to the performance cores (highest
// EfficiencyClass) on hybrid CPUs via SetThreadSelectedCpuSets. A no-op on
// non-hybrid systems, older Windows builds, or invalid handles - never a
// hard affinity mask, so the scheduler can still migrate if needed.
void pinThreadToPerformanceCores(void* nativeThreadHandle);

} // namespace vd
