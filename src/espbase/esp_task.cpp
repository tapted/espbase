#include "espbase/esp_task.hpp"

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

EspResult<void> EspTaskBase::start_internal(const char* name, uint32_t stack_size,
                                            UBaseType_t priority, TaskFunction_t task_code,
                                            void* arg) {
  if (task_handle_ != nullptr) return ESP_ERR_INVALID_STATE;

  stop_requested_ = false;

  sync_sem_ = xSemaphoreCreateBinary();
  if (!sync_sem_) return ESP_ERR_NO_MEM;

  BaseType_t res = xTaskCreate(task_code, name, stack_size, arg, priority, &task_handle_);
  if (res != pdPASS) {
    vSemaphoreDelete(sync_sem_);
    sync_sem_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}