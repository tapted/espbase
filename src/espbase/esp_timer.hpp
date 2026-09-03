#pragma once

#include <esp_timer.h>

#include "espbase/esp_result.hpp"

class EspTimer {
 public:
  constexpr EspTimer() = default;
  ~EspTimer() { destroy(); }

  EspTimer(const EspTimer&) = delete;
  EspTimer& operator=(const EspTimer&) = delete;

  template <auto MemberFunc, typename ClassType>
  EspResult<void> create(const char* name, ClassType* instance) {
    destroy();

    esp_timer_create_args_t args = {};
    args.callback = [](void* arg) { (static_cast<ClassType*>(arg)->*MemberFunc)(); };
    args.name = name;
    args.arg = instance;
    return esp_timer_create(&args, &handle_);
  }

  template <auto Func>
  EspResult<void> create(const char* name) {
    destroy();

    esp_timer_create_args_t args = {};
    args.callback = [](void* arg) { Func(); };
    args.name = name;
    return esp_timer_create(&args, &handle_);
  }

  void start_once(uint32_t delay_ms) {
    if (handle_) {
      esp_timer_stop(handle_);
      esp_timer_start_once(handle_, delay_ms * 1000ULL);
    }
  }

  void stop() {
    if (handle_) esp_timer_stop(handle_);
  }

 private:
  void destroy() {
    if (handle_) {
      esp_timer_stop(handle_);
      esp_timer_delete(handle_);
      handle_ = nullptr;
    }
  }

  esp_timer_handle_t handle_{nullptr};
};