#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <optional>

#include "espbase/esp_result.hpp"
#include "espbase/pm_lock.hpp"

typedef struct esp_timer* esp_timer_handle_t;

struct MainLoopTaskConfig {
  const char* name = "ml_task";
  bool prevent_light_sleep = false;
  bool lock_apb_freq = false;
};

class MainLoopTaskBase {
 public:
  constexpr MainLoopTaskBase() = default;
  virtual ~MainLoopTaskBase() { reset(); }

  MainLoopTaskBase(const MainLoopTaskBase&) = delete;
  MainLoopTaskBase& operator=(const MainLoopTaskBase&) = delete;

  void reset(uint32_t timeout_ms = 2000);
  void request_stop();

  // Immediately pushes a step to the main_loop queue, bypassing any timer delays.
  void notify(bool clear_stop = true);

  bool running() const { return running_.load(std::memory_order_relaxed); }
  bool is_stop_requested() const volatile { return stop_requested_; }

 protected:
  EspResult<void> start_internal(const MainLoopTaskConfig& config);

  // Virtual hooks implemented by the templated derived class
  virtual std::optional<uint32_t> on_step() = 0;
  virtual void on_stop() = 0;

 private:
  std::atomic<bool> running_{false};
  volatile bool stop_requested_ = false;
  volatile bool terminate_requested_ = false;

  // Tracks if the logical sequence is still iterating.
  // 0 = INACTIVE. >0 = ACTIVE (Version Number).
  std::atomic<uint32_t> sequence_version_{0};

  // Safety counter for UAF prevention during teardown.
  std::atomic<uint32_t> inflight_count_{0};

  esp_timer_handle_t timer_handle_ = nullptr;
  SemaphoreHandle_t join_sem_ = nullptr;

  PmLock pm_sleep_lock_{PmLock::NoLightSleep};
  PmLock pm_apb_lock_{PmLock::ApbFreqMax};

  void app_loop_step();
};

template <typename TaskData>
class MainLoopTask : public MainLoopTaskBase {
 public:
  // Run one step of the main loop. Return an optional delay in milliseconds until the next step.
  using StepFunction = std::optional<uint32_t> (*)(MainLoopTask<TaskData>& task);
  
  // Called when the task is requested to stop. Can be used to clean up resources.
  using StopFunction = void (*)(MainLoopTask<TaskData>& task);

  constexpr MainLoopTask() = default;

  EspResult<void> start(const MainLoopTaskConfig& config, TaskData* data, StepFunction step_func,
                        StopFunction stop_func = nullptr) {
    data_ = data;
    step_func_ = step_func;
    stop_func_ = stop_func;
    return start_internal(config);
  }

  EspResult<void> start(TaskData* data, StepFunction step_func, StopFunction stop_func = nullptr) {
    return start(MainLoopTaskConfig{}, data, step_func, stop_func);
  }

  TaskData* data() const { return data_; }

 protected:
  // Route the virtual base calls back to the typed function pointers
  std::optional<uint32_t> on_step() override {
    if (step_func_) return step_func_(*this);
    return std::nullopt;
  }

  void on_stop() override {
    if (stop_func_) stop_func_(*this);
  }

 private:
  TaskData* data_ = nullptr;
  StepFunction step_func_ = nullptr;
  StopFunction stop_func_ = nullptr;
};