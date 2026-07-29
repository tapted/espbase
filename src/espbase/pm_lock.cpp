#include "espbase/pm_lock.hpp"

#include <esp_err.h>
#include <esp_pm.h>

static esp_pm_lock_type_t convert_lock_type(PmLock::LockType lock_type) {
  switch (lock_type) {
    case PmLock::CpuFreqMax:
      return ESP_PM_CPU_FREQ_MAX;
    case PmLock::ApbFreqMax:
      return ESP_PM_APB_FREQ_MAX;
    case PmLock::NoLightSleep:
      return ESP_PM_NO_LIGHT_SLEEP;
    default:
      return ESP_PM_NO_LIGHT_SLEEP;
  }
}

esp_err_t PmLock::create_or_delete(bool create, const char* name, int arg) {
  if (create) return esp_pm_lock_create(convert_lock_type(lock_type_), arg, name, &handle_);

  if (acquired_) {
    esp_pm_lock_release(handle_);
    acquired_ = false;
  }
  esp_err_t err = esp_pm_lock_delete(handle_);
  handle_ = nullptr;
  return err;
}

esp_err_t PmLock::acquire_if_enabled() {
  if (!handle_ || acquired_) return ESP_OK;
  esp_err_t err = esp_pm_lock_acquire(handle_);
  if (err == ESP_OK) acquired_ = true;
  return err;
}

esp_err_t PmLock::release() {
  if (!handle_ || !acquired_) return ESP_OK;
  esp_err_t err = esp_pm_lock_release(handle_);
  if (err == ESP_OK) acquired_ = false;
  return err;
}