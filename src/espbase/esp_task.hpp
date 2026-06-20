/**
 * @file task.hpp
 * @brief RAII zero-allocation wrapper for FreeRTOS Tasks
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "espbase/esp_result.hpp"

struct TaskConfig {
  const char* name = "task";
  uint32_t stack_size = 2048;
  UBaseType_t priority = 5;
  BaseType_t core_id = tskNO_AFFINITY;
};

// Abstraction over FreeRTOS tasks that provides RAII semantics and safe shutdown capabilities.
class EspTaskBase {
 public:
  constexpr EspTaskBase() = default;
  ~EspTaskBase() { reset(); }

  // Tasks cannot be copied or moved (must remain pinned in memory)
  EspTaskBase(const EspTaskBase&) = delete;
  EspTaskBase& operator=(const EspTaskBase&) = delete;
  EspTaskBase(EspTaskBase&&) = delete;
  EspTaskBase& operator=(EspTaskBase&&) = delete;

  // Safely stops the task and releases all resources. Can be called multiple times.
  void reset();

  // Notifies the task to wake up and optionally clears the stop request flag.
  void notify(bool clear_stop = false);
  void request_stop();
  bool is_stop_requested() const volatile { return stop_requested_; }
  bool wait_for_notification(TickType_t ticks = portMAX_DELAY);

 protected:
  TaskHandle_t task_handle_ = nullptr;
  SemaphoreHandle_t sync_sem_ = nullptr;
  volatile bool stop_requested_ = false;

  // Internal orchestrator for FreeRTOS task creation
  EspResult<void> start_internal(const TaskConfig& config, TaskFunction_t task_code, void* arg);
};

template <typename TaskData>
class EspTask : public EspTaskBase {
 public:
  using TaskFunction = void (*)(EspTask<TaskData>& task);

  constexpr EspTask() = default;

  EspResult<void> start(const TaskConfig& config, TaskData* data, TaskFunction func) {
    data_ = data;
    func_ = func;
    return start_internal(config, trampoline, this);
  }

  EspResult<void> start(TaskData* data, TaskFunction func) {
    return start(TaskConfig{}, data, func);
  }

  TaskData* data() const { return data_; }

 private:
  TaskData* data_ = nullptr;
  TaskFunction func_ = nullptr;

  static void trampoline(void* arg) {
    auto* instance = static_cast<EspTask<TaskData>*>(arg);

    if (instance->func_) {
      instance->func_(*instance);
    }

    // Safely clear the handle before natural thread exit to prevent ~TaskBase
    // from attempting a double-delete on a dead thread.
    instance->task_handle_ = nullptr;
    vTaskDelete(nullptr);
  }
};
