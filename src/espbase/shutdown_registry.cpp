#include "espbase/shutdown_registry.hpp"

#include <cstddef>
#include <esp_log.h>
#include <mutex>

static constexpr size_t MAX_FUNCTIONS = 32;

static constinit ShutdownRegistry::ShutdownFn functions_[MAX_FUNCTIONS] = {nullptr};
static size_t count_ = 0;
static std::mutex registry_mutex_;  // Protects both count_ and functions_

void ShutdownRegistry::register_fn(ShutdownRegistry::ShutdownFn fn) {
  if (!fn) return;

  std::lock_guard<std::mutex> lock(registry_mutex_);

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
  std::lock_guard<std::mutex> lock(registry_mutex_);
  ESP_LOGI("Shutdown", "Executing %d shutdown callbacks...", (int)count_);

  for (int i = static_cast<int>(count_) - 1; i >= 0; --i) {
    if (functions_[i]) {
      functions_[i]();
    }
  }

  count_ = 0;
}