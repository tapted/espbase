#pragma once

// Utility to detect and mitigate crash loops by tracking the frequency of resets and optionally
// invoking a user-provided callback when a threshold is exceeded.
void check_crash_loop(void (*on_threshold_reached)() = nullptr);