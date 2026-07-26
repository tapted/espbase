#pragma once

#include <cstddef>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <type_traits>

#include "espbase/trampoline.hpp"

struct AppCommand {
  void* instance;
  void (*execute)(void*);
};

template <size_t QueueSize = 50>
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
    return push_func(trampoline<MemFn>(), safe_instance);
  }

  bool push_func(void (*execute)(void*), void* instance) {
    AppCommand cmd{.instance = instance, .execute = execute};

    // Note: If you ever push from a hardware interrupt, you must use
    // xQueueSendFromISR with a higherPriorityTaskWoken check instead.
    return xQueueSend(queue_, &cmd, 0) == pdTRUE;
  }

  void run_forever() {
    AppCommand cmd;
    while (true) {
      if (xQueueReceive(queue_, &cmd, portMAX_DELAY)) {
        if (cmd.execute) cmd.execute(cmd.instance);
      }
    }
  }

 private:
  StaticQueue_t state_;
  uint8_t storage_[QueueSize * sizeof(AppCommand)];

  QueueHandle_t queue_ = nullptr;
};

inline MainLoop main_loop;