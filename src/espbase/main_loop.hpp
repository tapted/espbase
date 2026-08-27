#pragma once

#include <cstddef>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <type_traits>

#include "espbase/trampoline.hpp"

struct AppCommand {
  void* instance;
  void (*execute)(void*);
};

template <size_t QueueSize = 128>
class MainLoop {
 public:
  MainLoop() : queue_(xQueueCreateStatic(QueueSize, sizeof(AppCommand), storage_, &state_)) {}

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
    bool result = push_func(trampoline<MemFn>(), safe_instance);
    if (!result) {
      ESP_LOGE("MainLoop", "Failed to push command to queue. Queue may be full.");
    }
    return result;
  }

  void run_forever() {
    AppCommand cmd;
    while (true) {
      if (xQueueReceive(queue_, &cmd, portMAX_DELAY)) {
        is_in_main_loop_ = true;
        cmd.execute(cmd.instance);
        is_in_main_loop_ = false;
      }
    }
  }

  bool is_executing_this_task() { return is_in_main_loop_; }

 private:
  bool push_func(void (*execute)(void*), void* instance) {
    AppCommand cmd{.instance = instance, .execute = execute};

    if (xPortInIsrContext()) {
      // Safe to call from true hardware interrupts
      BaseType_t high_task_woken = pdFALSE;
      BaseType_t res = xQueueSendFromISR(queue_, &cmd, &high_task_woken);
      if (high_task_woken) portYIELD_FROM_ISR();
      return res == pdTRUE;
    } else {
      // Standard FreeRTOS task context (including the default esp_timer task)
      return xQueueSend(queue_, &cmd, 0) == pdTRUE;
    }
  }

  StaticQueue_t state_;
  uint8_t storage_[QueueSize * sizeof(AppCommand)];

  QueueHandle_t queue_ = nullptr;
  inline static thread_local bool is_in_main_loop_;
};

inline MainLoop main_loop;