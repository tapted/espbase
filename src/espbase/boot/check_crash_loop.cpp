#include "espbase/boot/check_crash_loop.hpp"

#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <nvs_flash.h>

namespace {
static constexpr char TAG[] = "CrashLoopCheck";
}

RTC_NOINIT_ATTR struct RtcState {
  uint32_t magic_word;
  uint32_t consecutive_crashes;
} crash_loop_rtc_state;

void check_crash_loop(void (*on_threshold_reached)()) {
  // 1. Initialize on cold boot OR forced software wipe
  if (crash_loop_rtc_state.magic_word != 0xAABBCCDD) {
    // Zero out the entire struct cleanly
    memset(&crash_loop_rtc_state, 0, sizeof(RtcState));
    crash_loop_rtc_state.magic_word = 0xAABBCCDD;
  }

  esp_reset_reason_t reason = esp_reset_reason();

  if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT) {
    crash_loop_rtc_state.consecutive_crashes++;
    ESP_LOGE(TAG, "Crash detected! Count: %lu", crash_loop_rtc_state.consecutive_crashes);
  } else {
    // Normal boot (e.g. power cycled or deep sleep wake). Reset the counter.
    crash_loop_rtc_state.consecutive_crashes = 0;
  }

  // Threshold 1: Software Data Recovery
  if (crash_loop_rtc_state.consecutive_crashes == 3) {
    ESP_LOGW(TAG, "Attempting NVS Wipe Recovery...");
    nvs_flash_erase();
    // Do NOT reset the counter here. Let it boot.
    // If it succeeds, the next normal boot will reset it to 0.
  }

  // Threshold 2: Hardware Protection (The Terminal Halt)
  if (crash_loop_rtc_state.consecutive_crashes >= 5) {
    ESP_LOGE(TAG, "TERMINAL CRASH LOOP. Halting system to protect flash.");

    // Optional: Turn on a red LED here if you have one, so you know it's dead.
    // Only check == 5 in case on_threshold_reached *also* causes a reset.
    if (on_threshold_reached && crash_loop_rtc_state.consecutive_crashes == 5) {
      on_threshold_reached();
    }

    // Go to sleep forever. The CPU stops executing. The flash is safe.
    // The ONLY way to wake it up is a physical power cycle or the EN/Reset button.
    esp_deep_sleep_start();
  }
}
