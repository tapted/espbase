#include "espbase/shutdown_registry.hpp"

#include <esp_log.h>

ShutdownRegistry::ShutdownFn ShutdownRegistry::functions_[ShutdownRegistry::MAX_FUNCTIONS] = {
    nullptr,
};
size_t ShutdownRegistry::count_ = 0;

void ShutdownRegistry::register_fn(ShutdownRegistry::ShutdownFn fn) {
  if (!fn) return;

  // Prevent duplicate registrations if a module is initialized twice
  for (size_t i = 0; i < count_; ++i) {
    if (functions_[i] == fn) {
      return;
    }
  }

  if (count_ < MAX_FUNCTIONS) {
    functions_[count_++] = fn;
  } else {
    ESP_LOGE("Shutdown", "Registry full! Increase MAX_FUNCTIONS.");
  }
}

void ShutdownRegistry::shutdown_all() {
  ESP_LOGI("Shutdown", "Executing %d shutdown callbacks...", (int)count_);

  // Iterate backwards: count_ - 1 down to 0
  for (int i = static_cast<int>(count_) - 1; i >= 0; --i) {
    if (functions_[i]) {
      functions_[i]();
    }
  }

  // Reset the counter in case we wake up without a full reboot
  count_ = 0;
}