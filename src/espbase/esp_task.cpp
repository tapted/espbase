#include "espbase/esp_task.hpp"

#include <esp_pm.h>

void EspTaskBase::reset() {
  if (task_handle_ != nullptr) {
    stop_requested_ = true;
    if (sync_sem_) xSemaphoreGive(sync_sem_);

    vTaskDelete(task_handle_);
    task_handle_ = nullptr;
  }
  if (sync_sem_ != nullptr) {
    vSemaphoreDelete(sync_sem_);
    sync_sem_ = nullptr;
  }

  // Safely tear down the PM lock
  if (pm_lock_ != nullptr) {
    if (pm_lock_acquired_) esp_pm_lock_release(pm_lock_);
    esp_pm_lock_delete(pm_lock_);
    pm_lock_ = nullptr;
    pm_lock_acquired_ = false;
  }
}

void EspTaskBase::notify(bool clear_stop) {
  if (clear_stop) stop_requested_ = false;
  if (sync_sem_) xSemaphoreGive(sync_sem_);
}

void EspTaskBase::request_stop() {
  stop_requested_ = true;
  notify();
}

bool EspTaskBase::wait_for_notification(TickType_t ticks) {
  return xSemaphoreTake(sync_sem_, ticks) == pdTRUE;
}

void EspTaskBase::acquire_pm_lock() {
  if (pm_lock_ && !pm_lock_acquired_) {
    esp_pm_lock_acquire(pm_lock_);
    pm_lock_acquired_ = true;
  }
}

void EspTaskBase::release_pm_lock() {
  if (pm_lock_ && pm_lock_acquired_) {
    esp_pm_lock_release(pm_lock_);
    pm_lock_acquired_ = false;
  }
}

EspResult<void> EspTaskBase::start_internal(const TaskConfig& config, TaskFunction_t task_code,
                                            void* arg) {
  if (task_handle_ != nullptr) return ESP_ERR_INVALID_STATE;

  stop_requested_ = false;

  // Initialize the lock if requested by the config
  if (config.prevent_light_sleep) {
    if (esp_err_t err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, config.name, &pm_lock_)) {
      return err;
    }
  }

  sync_sem_ = xSemaphoreCreateBinary();
  if (!sync_sem_) {
    // Clean up the lock if semaphore creation fails
    if (pm_lock_) {
      esp_pm_lock_delete(pm_lock_);
      pm_lock_ = nullptr;
    }
    return ESP_ERR_NO_MEM;
  }

  BaseType_t res = xTaskCreatePinnedToCore(task_code, config.name, config.stack_size, arg,
                                           config.priority, &task_handle_, config.core_id);
  if (res != pdPASS) {
    vSemaphoreDelete(sync_sem_);
    sync_sem_ = nullptr;
    if (pm_lock_) {
      esp_pm_lock_delete(pm_lock_);
      pm_lock_ = nullptr;
    }
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}