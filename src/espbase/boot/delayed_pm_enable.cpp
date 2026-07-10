#include "espbase/boot/delayed_pm_enable.hpp"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

static const char* TAG = "PM_BOOT";

enum class PmState {
  BOOT_WINDOW_NO_OVERRIDE,     // Timer is running
  BOOT_WINDOW_ALLOW_OVERRIDE,  // Timer is running, button press allowed
  PM_ACTIVE_ALLOW_OVERRIDE,    // Timer fired, PM configured, button press allowed (for override)
  PM_ACTIVE_NO_OVERRIDE,       // Timer fired, PM configured, button press ignored
  PM_OVERRIDDEN                // Button pressed
};

static DRAM_ATTR volatile PmState s_state = PmState::BOOT_WINDOW_NO_OVERRIDE;
static esp_timer_handle_t s_timer_handle = nullptr;

static void apply_pm_override(void* /*arg1*/, uint32_t /*arg2*/) {
  esp_pm_lock_handle_t s_override_lock = nullptr;  // Handle "leaked". We never need it again.
  esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "pm_override", &s_override_lock);
  esp_pm_lock_acquire(s_override_lock);
  // When using native USB, it's likely this log line will never actually be seen. If power
  // management was active then the USB logging connection would have been severed already.
  ESP_LOGW(TAG, "Boot button pressed. Tickless Idle disabled until next reboot.");
}

static void IRAM_ATTR boot_button_isr(void* /*arg*/) {
  PmState was_active = s_state;
  s_state = PmState::PM_OVERRIDDEN;
  gpio_set_intr_type(GPIO_NUM_0, GPIO_INTR_DISABLE);  // Never fire again.
  if (was_active == PmState::PM_ACTIVE_ALLOW_OVERRIDE) {
    BaseType_t high_task_wakeup = pdFALSE;
    xTimerPendFunctionCallFromISR(apply_pm_override, nullptr, 0, &high_task_wakeup);
    if (high_task_wakeup) portYIELD_FROM_ISR();
  }
}

static void delete_timer_deferred(void* arg1, uint32_t /*arg2*/) {
  esp_timer_delete(static_cast<esp_timer_handle_t>(arg1));
}

static void delayed_pm_callback(void* /*arg*/) {
  if (s_state == PmState::BOOT_WINDOW_NO_OVERRIDE ||
      s_state == PmState::BOOT_WINDOW_ALLOW_OVERRIDE) {
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240, .min_freq_mhz = 40, .light_sleep_enable = true};
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    s_state = s_state == PmState::BOOT_WINDOW_NO_OVERRIDE ? PmState::PM_ACTIVE_NO_OVERRIDE
                                                          : PmState::PM_ACTIVE_ALLOW_OVERRIDE;
    ESP_LOGI(TAG, "Boot window closed. Tickless Idle enabled. You might see no more logs.");
  } else {
    ESP_LOGW(TAG, "Boot button was pressed. Aborting Tickless Idle config.");
  }
  // Delete the timer to free memory after we return. The handle has to be static though.
  xTimerPendFunctionCall(delete_timer_deferred, s_timer_handle, 0, portMAX_DELAY);
}

void delayed_pm_enable(const DelayedPmEnableConfig& config) {
  // If logging over Native USB, sleep will break the monitor connection! Provide an escape hatch
  // that keeps USB alive while still powering down the CPU to test sleep states. This should only
  // be used when debugging. But doesn't seem to achieve anything.
  if (config.keep_usb_alive) {
    // Prevent the digital/peripheral domain from powering down in sleep
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
#if CONFIG_IDF_TARGET_ESP32S3
    esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_ON);  // Keep USB crystal oscillator alive
#endif
  }

  if (config.disable_sleep_on_gpio0_press) {
    if (config.allow_override_after_boot_window) {
      s_state = PmState::BOOT_WINDOW_ALLOW_OVERRIDE;
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // Trigger exactly when pressed
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_NUM_0, boot_button_isr, nullptr);
  }

  esp_timer_create_args_t timer_args = {
      .callback = &delayed_pm_callback,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "pm_boot_delay",
      .skip_unhandled_events = false,
  };

  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_timer_handle));
  ESP_ERROR_CHECK(esp_timer_start_once(s_timer_handle, 3000000));
}