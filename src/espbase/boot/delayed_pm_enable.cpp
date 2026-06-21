#include "espbase/boot/delayed_pm_enable.hpp"

#include <esp_log.h>
#include <esp_pm.h>
#include <esp_timer.h>

static esp_timer_handle_t s_pm_timer_handle = nullptr;

static void delayed_pm_enable_task(void* /*arg*/) {
  esp_pm_config_t pm_config = {.max_freq_mhz = 240, .min_freq_mhz = 40, .light_sleep_enable = true};
  ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
}

void delayed_pm_enable() {
  esp_timer_create_args_t timer_args = {
      .callback = &delayed_pm_enable_task,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "pm_boot_delay",
      .skip_unhandled_events = false,
  };

  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_pm_timer_handle));
  ESP_ERROR_CHECK(esp_timer_start_once(s_pm_timer_handle, 3000000));
}