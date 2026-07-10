/**
 * @file task.hpp
 * @brief RAII zero-allocation wrapper for FreeRTOS Tasks
 */

#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "espbase/esp_result.hpp"

struct esp_pm_lock;
typedef struct esp_pm_lock* esp_pm_lock_handle_t;

struct TaskConfig {
  const char* name = "task";
  uint32_t stack_size = 2048;
  UBaseType_t priority = 5;
  BaseType_t core_id = tskNO_AFFINITY;

  // Automatically manages an ESP_PM_NO_LIGHT_SLEEP lock. Currently this is only implemented for
  // YieldingTask. It causes light-sleep to be disabled so long as the task step indicates it is
  // active (by returning a non-nullopt value) or until the task is explicitly stopped.
  bool prevent_light_sleep = false;

  // Automatically manages an ESP_PM_APB_FREQ_MAX lock. This prevents the APB frequency from
  // scaling down under load, which can cause issues for timing-sensitive applications.
  bool lock_apb_freq = false;

  // If true, instead of starting a new task, start will safely check for an existing task and
  // notify it instead.
  bool notify_if_started = true;
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
  bool running() const { return task_handle_.load(std::memory_order_relaxed) != nullptr; }
  bool is_stop_requested() const volatile { return stop_requested_; }
  bool wait_for_notification(TickType_t ticks = portMAX_DELAY);

  void log_high_watermark();

 protected:
  std::atomic<TaskHandle_t> task_handle_ = nullptr;
  SemaphoreHandle_t sync_sem_ = nullptr;
  SemaphoreHandle_t join_sem_ = nullptr;
  volatile bool stop_requested_ = false;
  volatile bool terminate_requested_ = false;

  // --- PM Lock Management ---
  esp_pm_lock_handle_t pm_sleep_lock_ = nullptr;
  esp_pm_lock_handle_t pm_apb_lock_ = nullptr;
  bool locks_acquired_ = false;

  void acquire_pm_locks();
  void release_pm_locks();

  // Internal orchestrator for FreeRTOS task creation.
  EspResult<void> start_internal(const TaskConfig& config, TaskFunction_t task_code, void* arg);

  // Called by the thread's trampoline just before exiting.
  [[noreturn]] void terminate_from_task();
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
    instance->acquire_pm_locks();
    instance->func_(*instance);
    instance->release_pm_locks();
    instance->terminate_from_task();  // Never returns.
  }
};
