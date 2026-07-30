#include "espbase/main_loop_task.hpp"

#include <esp_timer.h>

#include "espbase/main_loop.hpp"

void MainLoopTaskBase::request_stop() {
  stop_requested_ = true;
  // Only intercept the timer and push a step if the sequence is actually still running
  if (sequence_active_ && running_.load(std::memory_order_acquire)) {
    if (timer_handle_) esp_timer_stop(timer_handle_);
    inflight_count_.fetch_add(1, std::memory_order_relaxed);
    main_loop.push<&MainLoopTaskBase::app_loop_step>(this);
  }
}

void MainLoopTaskBase::reset(uint32_t timeout_ms) {
  // Atomically claim the teardown routine
  if (!running_.exchange(false, std::memory_order_acquire)) {
    return;
  }

  terminate_requested_ = true;

  // 1. Kill the timer to prevent new steps from entering the queue
  if (timer_handle_) {
    esp_timer_stop(timer_handle_);
    esp_timer_delete(timer_handle_);
    timer_handle_ = nullptr;
  }

  // 2. Block until the main_loop queue has completely drained our events
  if (inflight_count_.load(std::memory_order_acquire) > 0) {
    if (join_sem_) xSemaphoreTake(join_sem_, pdMS_TO_TICKS(timeout_ms));
  }

  if (join_sem_) {
    vSemaphoreDelete(join_sem_);
    join_sem_ = nullptr;
  }

  pm_sleep_lock_.enable(false);
  pm_apb_lock_.enable(false);
}

EspResult<void> MainLoopTaskBase::start_internal(const MainLoopTaskConfig& config) {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
    return ESP_ERR_INVALID_STATE;  // Already running
  }

  stop_requested_ = false;
  terminate_requested_ = false;
  sequence_active_ = true;
  inflight_count_.store(0, std::memory_order_release);

  pm_sleep_lock_.enable(config.prevent_light_sleep, config.name);
  pm_apb_lock_.enable(config.lock_apb_freq, config.name);

  if (!join_sem_) {
    join_sem_ = xSemaphoreCreateBinary();
    if (!join_sem_) {
      reset();
      return ESP_ERR_NO_MEM;
    }
  }

  esp_timer_create_args_t timer_args = {};
  timer_args.arg = this;
  timer_args.name = config.name;
  timer_args.callback = [](void* arg) {
    MainLoopTaskBase* task = static_cast<MainLoopTaskBase*>(arg);
    task->inflight_count_.fetch_add(1, std::memory_order_relaxed);
    main_loop.push<&MainLoopTaskBase::app_loop_step>(task);
  };

  if (esp_timer_create(&timer_args, &timer_handle_) != ESP_OK) {
    reset();
    return ESP_FAIL;
  }

  // Kick off the first execution step immediately
  inflight_count_.fetch_add(1, std::memory_order_relaxed);
  if (!main_loop.push<&MainLoopTaskBase::app_loop_step>(this)) {
    inflight_count_.fetch_sub(1, std::memory_order_relaxed);
    reset();
    return ESP_FAIL;
  }

  return ESP_OK;
}

void MainLoopTaskBase::app_loop_step() {
  // Execute only if not tearing down and the sequence hasn't naturally finished
  if (!terminate_requested_ && sequence_active_) {
    // TRANSIENT PM LOCKS: Acquire only for the duration of this function
    pm_sleep_lock_.acquire_if_enabled();
    pm_apb_lock_.acquire_if_enabled();

    if (stop_requested_) {
      on_stop();
      sequence_active_ = false;
    } else {
      std::optional<uint32_t> delay_ms = on_step();

      if (delay_ms.has_value()) {
        if (!terminate_requested_ && timer_handle_) {
          esp_timer_start_once(timer_handle_, *delay_ms * 1000ULL);
        }
      } else {
        on_stop();  // Natural completion
        sequence_active_ = false;
      }
    }

    // Release immediately so the ESP32 can sleep during the timer delay
    pm_sleep_lock_.release();
    pm_apb_lock_.release();
  }

  // UAF Guard: Decrement and wake the destructor if it is waiting
  if (inflight_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    if (terminate_requested_ && join_sem_) {
      xSemaphoreGive(join_sem_);
    }
  }
}