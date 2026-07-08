#pragma once

#include <cstdint>

// Starts a background timer. If mark_ota_valid() is not called before
// timeout_ms expires, the device will reboot.
// Safely does NOTHING if the firmware is not in a pending verification state.
void start_ota_rollback_watchdog(uint32_t timeout_ms = 120000);

// Cancels the rollback watchdog and permanently validates the current firmware.
void mark_ota_valid();
