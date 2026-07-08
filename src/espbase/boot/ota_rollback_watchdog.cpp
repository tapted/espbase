#include "espbase/boot/ota_rollback_watchdog.hpp"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>

static const char* TAG = "OTA_WDG";
static esp_timer_handle_t s_watchdog_timer = nullptr;

static void watchdog_timer_callback(void* /*arg*/) {
  esp_ota_img_states_t ota_state;
  const esp_partition_t* running = esp_ota_get_running_partition();

  // Double-check we are still in a pending state before pulling the plug
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      ESP_LOGE(TAG, "OTA Rollback Watchdog expired! Wi-Fi/MQTT failed to connect.");
      ESP_LOGE(TAG, "Rebooting to trigger hardware rollback to previous firmware...");
      esp_restart();
    }
  }
}

void start_ota_rollback_watchdog(uint32_t timeout_ms) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;

  esp_err_t err = esp_ota_get_state_partition(running, &ota_state);

  // 1. Safety Check: If App Rollback is disabled in menuconfig, or if the
  // firmware is already fully validated, silently return. No boot loops!
  if (err != ESP_OK || ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }

  ESP_LOGW(TAG, "Unconfirmed OTA firmware detected. Starting %lu ms rollback watchdog...",
           timeout_ms);

  esp_timer_create_args_t timer_args = {
      .callback = &watchdog_timer_callback,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "ota_rollback_wdg",
      .skip_unhandled_events = false,
  };

  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_watchdog_timer));

  // Convert ms to microseconds for esp_timer
  ESP_ERROR_CHECK(esp_timer_start_once(s_watchdog_timer, timeout_ms * 1000ULL));
}

void mark_ota_valid() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;

  // Only attempt validation if we actually need to
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      ESP_LOGI(TAG, "Network connection successful. Marking OTA update as VALID.");
      esp_ota_mark_app_valid_cancel_rollback();

      // Clean up the timer so it doesn't waste memory
      if (s_watchdog_timer) {
        esp_timer_stop(s_watchdog_timer);
        esp_timer_delete(s_watchdog_timer);
        s_watchdog_timer = nullptr;
      }
    }
  }
}