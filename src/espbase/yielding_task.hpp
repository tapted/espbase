#include <optional>

#include "espbase/esp_task.hpp"

// Extension of EspTask that supports cooperative yielding with optional delays and explicit stop
// requests. Implements a "Tickless State Machine" or an "Event-Driven Sequencer".
template <typename TaskData>
class YieldingTask : public EspTaskBase {
 public:
  // The step function returns an optional delay in milliseconds until the next step. Returning
  // nullopt indicates that the sequence has naturally completed and the task should transition to
  // idle.
  using StepFunction = std::optional<uint32_t> (*)(YieldingTask<TaskData>& task);

  // Optional callback fired when a stop is requested, or when the step function returns nullopt
  // (natural completion).
  using StopFunction = void (*)(YieldingTask<TaskData>& task);

  constexpr YieldingTask() = default;

  EspResult<void> start(const TaskConfig& config, TaskData* data, StepFunction step_func,
                        StopFunction stop_func = nullptr) {
    data_ = data;
    step_func_ = step_func;
    stop_func_ = stop_func;
    return start_internal(config, trampoline, this);
  }

  EspResult<void> start(TaskData* data, StepFunction step_func, StopFunction stop_func = nullptr) {
    return start(TaskConfig{}, data, step_func, stop_func);
  }

  TaskData* data() const { return data_; }

 private:
  TaskData* data_ = nullptr;
  StepFunction step_func_ = nullptr;
  StopFunction stop_func_ = nullptr;

  static void trampoline(void* arg) {
    auto* instance = static_cast<YieldingTask<TaskData>*>(arg);
    bool is_active = false;

    while (true) {
      // 1. Wakeup: Transition from Idle to Active
      if (!is_active) {
        instance->acquire_pm_lock();
        is_active = true;
      }

      // 2. Intercept explicit stop requests
      if (instance->is_stop_requested()) {
        if (instance->stop_func_) instance->stop_func_(*instance);

        instance->release_pm_lock();
        is_active = false;

        // Sleep indefinitely until notify(true) is called to clear the stop flag
        instance->wait_for_notification(portMAX_DELAY);
        continue;
      }

      // 3. Normal step execution
      std::optional<uint32_t> delay_ms = std::nullopt;
      if (instance->step_func_) delay_ms = instance->step_func_(*instance);

      // 4. Yielding
      if (delay_ms.has_value()) {
        // Remain Active: Keep lock acquired and CPU awake during this sequence delay
        instance->wait_for_notification(pdMS_TO_TICKS(*delay_ms));
      } else {
        // Natural Completion: Transition to Idle
        if (instance->stop_func_) instance->stop_func_(*instance);

        instance->release_pm_lock();
        is_active = false;
        instance->wait_for_notification(portMAX_DELAY);
      }
    }
  }
};