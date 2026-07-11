#include "espbase/esp_task.hpp"

#include <esp_pm.h>
#include <esp_timer.h>

void EspTaskBase::reset() {
  if (task_handle_ != nullptr) {
    terminate_requested_ = true;
    if (sync_sem_) xSemaphoreGive(sync_sem_);

    // Graceful shutdown: Wait for the thread to acknowledge termination and exit cleanly.
    if (join_sem_) {
      // Wait up to 2000ms. If it times out, the thread is deadlocked.
      if (xSemaphoreTake(join_sem_, pdMS_TO_TICKS(2000)) != pdTRUE) {
        // Fallback: The thread refuses to die. Forcefully terminate.
        vTaskDelete(task_handle_.load(std::memory_order_acquire));
      }
    }
    task_handle_.store(nullptr, std::memory_order_release);
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

void EspTaskBase::log_high_watermark() {
  if (task_handle_) {
    UBaseType_t high_watermark = uxTaskGetStackHighWaterMark(task_handle_);
    ESP_LOGI("EspTask", "Task '%s' high watermark: %u bytes", pcTaskGetName(task_handle_),
             high_watermark * sizeof(StackType_t));
  }
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
  TaskHandle_t expected = nullptr;
  TaskHandle_t starting_flag = reinterpret_cast<TaskHandle_t>(1);  // Atomic flag.

  // Attempt to atomically claim the right to start the task
  if (!task_handle_.compare_exchange_strong(expected, starting_flag, std::memory_order_acquire)) {
    // We failed to claim it. The task is either currently starting, or already running.

    if (config.notify_if_started) {
      // SPINLOCK: We MUST wait for the winning thread to finish xTaskCreate.
      // If we pass '0x1' to FreeRTOS xTaskNotify, it will instantly hardware panic.
      while (task_handle_.load(std::memory_order_acquire) == starting_flag) {
        taskYIELD();
      }

      notify(true);
      return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;  // Return early, the task is active
  }

  // We won the race! We own the initialization.
  TaskHandle_t new_handle = nullptr;
  stop_requested_ = false;
  terminate_requested_ = false;

  // Delegate to derived classes for specific hardware provisioning.
  if (EspError err = on_task_claimed(config)) {
    task_handle_.store(nullptr, std::memory_order_release);
    return err;
  }

  // Initialize PM Locks
  if (pm_sleep_lock_ == nullptr && config.prevent_light_sleep) {
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, config.name, &pm_sleep_lock_);
  }
  if (pm_apb_lock_ == nullptr && config.lock_apb_freq) {
    esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, config.name, &pm_apb_lock_);
  }
  if (sync_sem_ == nullptr) {
    sync_sem_ = xSemaphoreCreateBinary();
  } else {
    xQueueReset(sync_sem_);  // Clear any stale notifications from previous runs.
  }
  if (join_sem_ == nullptr) {
    join_sem_ = xSemaphoreCreateBinary();
  } else {
    xQueueReset(join_sem_);  // Clear any stale notifications from previous runs.
  }

  if (!sync_sem_ || !join_sem_) {
    task_handle_.store(nullptr, std::memory_order_release);
    reset();  // Safely cleans up partial allocations
    return ESP_ERR_NO_MEM;
  }

  BaseType_t res = xTaskCreatePinnedToCore(task_code, config.name, config.stack_size, arg,
                                           config.priority, &new_handle, config.core_id);
  if (res != pdPASS) {
    task_handle_.store(nullptr, std::memory_order_release);
    reset();
    return ESP_ERR_NO_MEM;
  }

  task_handle_.store(new_handle, std::memory_order_release);
  return ESP_OK;
}

void EspTaskBase::terminate_from_task() {
  // Should be a no-op - task shouldn't be holding locks if it wants to terminate itself:
  release_pm_locks();

  SemaphoreHandle_t j_sem = join_sem_;  // Copy the semaphore handle to the thread's stack.
  if (j_sem) xSemaphoreGive(j_sem);
  task_handle_.store(nullptr, std::memory_order_release);  //  Allow re-use.
  vTaskDelete(nullptr);  // Safe thread termination. Does not return.
  for (;;);  // Never reached, but needed to avoid "noreturn function does return" warnings.
}

EspResult<void> EspTaskBase::configure_hardware_timer(esp_timer_handle_t& timer_handle,
                                                      bool enable) {
  if (!enable) {
    if (timer_handle) {
      esp_timer_stop(timer_handle);
      esp_timer_delete(timer_handle);
      timer_handle = nullptr;
    }
    return ESP_OK;
  }
  if (timer_handle) {
    return ESP_OK;  // Already configured
  }
  esp_timer_create_args_t timer_args = {};
  timer_args.callback = [](void* arg) { static_cast<EspTaskBase*>(arg)->notify(); };
  timer_args.arg = this;
  timer_args.name = "yielding_hw_tmr";

  return esp_timer_create(&timer_args, &timer_handle);
}
