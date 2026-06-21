#include "espbase/esp_task.hpp"

#include <esp_pm.h>

void EspTaskBase::reset() {
  if (task_handle_ != nullptr) {
    terminate_requested_ = true;
    if (sync_sem_) xSemaphoreGive(sync_sem_);

    // Graceful shutdown: Wait for the thread to acknowledge termination and exit cleanly.
    if (join_sem_) {
      // Wait up to 2000ms. If it times out, the thread is deadlocked.
      if (xSemaphoreTake(join_sem_, pdMS_TO_TICKS(2000)) != pdTRUE) {
        // Fallback: The thread refuses to die. Forcefully terminate.
        vTaskDelete(task_handle_);
      }
    }
    task_handle_ = nullptr;
  }

  if (sync_sem_) {
    vSemaphoreDelete(sync_sem_);
    sync_sem_ = nullptr;
  }
  if (join_sem_) {
    vSemaphoreDelete(join_sem_);
    join_sem_ = nullptr;
  }

  if (locks_acquired_) release_pm_locks();
  if (pm_sleep_lock_) {
    esp_pm_lock_delete(pm_sleep_lock_);
    pm_sleep_lock_ = nullptr;
  }
  if (pm_apb_lock_) {
    esp_pm_lock_delete(pm_apb_lock_);
    pm_apb_lock_ = nullptr;
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

void EspTaskBase::acquire_pm_locks() {
  if (!locks_acquired_) {
    if (pm_sleep_lock_) esp_pm_lock_acquire(pm_sleep_lock_);
    if (pm_apb_lock_) esp_pm_lock_acquire(pm_apb_lock_);
    locks_acquired_ = true;
  }
}

void EspTaskBase::release_pm_locks() {
  if (locks_acquired_) {
    if (pm_sleep_lock_) esp_pm_lock_release(pm_sleep_lock_);
    if (pm_apb_lock_) esp_pm_lock_release(pm_apb_lock_);
    locks_acquired_ = false;
  }
}

EspResult<void> EspTaskBase::start_internal(const TaskConfig& config, TaskFunction_t task_code,
                                            void* arg) {
  if (task_handle_ != nullptr) return ESP_ERR_INVALID_STATE;

  stop_requested_ = false;
  terminate_requested_ = false;

  // Initialize PM Locks
  if (config.prevent_light_sleep) {
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, config.name, &pm_sleep_lock_);
  }
  if (config.lock_apb_freq) {
    esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, config.name, &pm_apb_lock_);
  }

  sync_sem_ = xSemaphoreCreateBinary();
  join_sem_ = xSemaphoreCreateBinary();
  
  if (!sync_sem_ || !join_sem_) {
    reset();  // Safely cleans up partial allocations
    return ESP_ERR_NO_MEM;
  }

  BaseType_t res = xTaskCreatePinnedToCore(task_code, config.name, config.stack_size, arg,
                                           config.priority, &task_handle_, config.core_id);
  if (res != pdPASS) {
    reset();
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void EspTaskBase::terminate_from_task() {
  // Copy the semaphore handle to the thread's stack.
  SemaphoreHandle_t j_sem = join_sem_;

  if (j_sem) xSemaphoreGive(j_sem);
  vTaskDelete(nullptr);  // Safe thread termination. Does not return.
  for (;;);  // Never reached, but needed to avoid "noreturn function does return" warnings.
}