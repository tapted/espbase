#pragma once

#include <cstddef>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <type_traits>

#include "espbase/trampoline.hpp"

struct AppCommand {
  void* arg;
  void (*execute)(void*);
};

template <size_t QueueSize = 128, size_t MaxDelayedTasks = 16>
class MainLoop {
 private:
  struct DelayedTask {
    AppCommand cmd;
    MainLoop* loop;
    StaticTimer_t timer_state;
    TimerHandle_t timer_handle = nullptr;
    bool in_use = false;
  };

  DelayedTask delayed_tasks_[MaxDelayedTasks];
  portMUX_TYPE delay_mux_ = portMUX_INITIALIZER_UNLOCKED;

  static void timer_callback(TimerHandle_t xTimer) {
    DelayedTask* task = static_cast<DelayedTask*>(pvTimerGetTimerID(xTimer));
    task->loop->push_func(task->cmd.execute, task->cmd.arg);

    portENTER_CRITICAL(&task->loop->delay_mux_);
    task->in_use = false;  // Free the slot.
    portEXIT_CRITICAL(&task->loop->delay_mux_);
  }

 public:
  MainLoop() : queue_(xQueueCreateStatic(QueueSize, sizeof(AppCommand), storage_, &state_)) {
    for (auto& dt : delayed_tasks_) {
      dt.loop = this;
      // Period is 1 tick initially; it will be overwritten when dispatched
      dt.timer_handle =
          xTimerCreateStatic("delay", 1, pdFALSE, &dt, timer_callback, &dt.timer_state);
    }
  }

  template <auto MemFn, typename T>
  bool push(T* instance) {
    using ExpectedClass = typename detail::mem_fn_traits<decltype(MemFn)>::class_type;

    // Compile-time guard: Ensure T* can legally be treated as ExpectedClass*
    static_assert(
        std::is_convertible_v<T*, ExpectedClass*>,
        "Type mismatch: The instance pointer is not compatible with this member function.");

    // Pointer adjustment: Cast the pointer to the expected base class BEFORE it gets erased to a
    // void*. This guarantees the compiler applies any necessary memory offsets if T inherits from
    // ExpectedClass. Then pass the safely adjusted pointer into the erased context.
    ExpectedClass* safe_instance = static_cast<ExpectedClass*>(instance);
    return push_func(trampoline<MemFn>(), safe_instance);
  }

  bool push_func(void (*execute)(void*), void* arg = nullptr) {
    AppCommand cmd{.arg = arg, .execute = execute};
    bool pushed = false;

    if (xPortInIsrContext()) {
      // Safe to call from true hardware interrupts
      BaseType_t high_task_woken = pdFALSE;
      BaseType_t res = xQueueSendFromISR(queue_, &cmd, &high_task_woken);
      if (high_task_woken) portYIELD_FROM_ISR();
      pushed = res == pdTRUE;
    } else {
      // Standard FreeRTOS task context (including the default esp_timer task)
      pushed = xQueueSend(queue_, &cmd, 0) == pdTRUE;
    }
    if (!pushed) ESP_LOGE("MainLoop", "Failed to push command to queue. Queue may be full.");
    return pushed;
  }

  template <auto MemFn, typename T>
  bool post_delayed(uint32_t delay_ms, T* instance) {
    using ExpectedClass = typename detail::mem_fn_traits<decltype(MemFn)>::class_type;
    static_assert(
        std::is_convertible_v<T*, ExpectedClass*>,
        "Type mismatch: The instance pointer is not compatible with this member function.");

    ExpectedClass* safe_instance = static_cast<ExpectedClass*>(instance);
    return post_delayed_func(delay_ms, trampoline<MemFn>(), safe_instance);
  }

  bool post_delayed_func(uint32_t delay_ms, void (*execute)(void*), void* arg = nullptr) {
    if (delay_ms == 0) return push_func(execute, arg);

    TickType_t ticks = pdMS_TO_TICKS(delay_ms);
    if (ticks == 0) ticks = 1;

    DelayedTask* free_slot = nullptr;

    // Find an available timer slot securely
    if (xPortInIsrContext()) {
      portENTER_CRITICAL_ISR(&delay_mux_);
      for (auto& dt : delayed_tasks_) {
        if (!dt.in_use) {
          dt.in_use = true;
          free_slot = &dt;
          break;
        }
      }
      portEXIT_CRITICAL_ISR(&delay_mux_);
    } else {
      portENTER_CRITICAL(&delay_mux_);
      for (auto& dt : delayed_tasks_) {
        if (!dt.in_use) {
          dt.in_use = true;
          free_slot = &dt;
          break;
        }
      }
      portEXIT_CRITICAL(&delay_mux_);
    }

    if (!free_slot) {
      ESP_LOGE("MainLoop", "Delayed task pool is full! Increase MaxDelayedTasks.");
      return false;
    }

    free_slot->cmd = {arg, execute};
    BaseType_t success;
    if (xPortInIsrContext()) {
      BaseType_t high_task_woken = pdFALSE;
      success = xTimerChangePeriodFromISR(free_slot->timer_handle, ticks, &high_task_woken);
      if (high_task_woken) portYIELD_FROM_ISR();
    } else {
      success = xTimerChangePeriod(free_slot->timer_handle, ticks, 0);
    }

    if (success != pdPASS) {
      ESP_LOGE("MainLoop", "Failed to dispatch FreeRTOS timer.");
      // Rollback the slot if the OS failed to start the timer
      if (xPortInIsrContext()) {
        portENTER_CRITICAL_ISR(&delay_mux_);
        free_slot->in_use = false;
        portEXIT_CRITICAL_ISR(&delay_mux_);
      } else {
        portENTER_CRITICAL(&delay_mux_);
        free_slot->in_use = false;
        portEXIT_CRITICAL(&delay_mux_);
      }
      return false;
    }

    return true;
  }

  void run_forever() {
    AppCommand cmd;
    while (true) {
      if (xQueueReceive(queue_, &cmd, portMAX_DELAY)) {
        is_in_main_loop_ = true;
        cmd.execute(cmd.arg);
        is_in_main_loop_ = false;
      }
    }
  }

  bool is_executing_this_task() { return is_in_main_loop_; }

 private:
  StaticQueue_t state_;
  uint8_t storage_[QueueSize * sizeof(AppCommand)];

  QueueHandle_t queue_ = nullptr;
  inline static thread_local bool is_in_main_loop_;
};

inline MainLoop main_loop;