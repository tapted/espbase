#include "espbase/main_loop_task.hpp"

#include <esp_timer.h>

#include "espbase/main_loop.hpp"

void MainLoopTaskBase::politely_request_stop() {
  stop_requested_ = true;
  // Only intercept the timer and push a step if the sequence is actually still running
  // Check version > 0 instead of sequence_active_
  if (sequence_version_.load(std::memory_order_acquire) > 0 &&
      running_.load(std::memory_order_acquire)) {
    if (timer_handle_) esp_timer_stop(timer_handle_);
    inflight_count_.fetch_add(1, std::memory_order_relaxed);
    main_loop.push<&MainLoopTaskBase::app_loop_step>(this);
  }
}

void MainLoopTaskBase::notify(bool clear_stop) {
  if (!running_.load(std::memory_order_acquire) || terminate_requested_) return;

  if (clear_stop) {
    stop_requested_ = false;
  }

  // Atomically bump the sequence version to reactivate and invalidate any active teardowns
  uint32_t prev = sequence_version_.load(std::memory_order_acquire);
  uint32_t next;
  do {
    next = (prev == 0) ? 1 : prev + 1;
    if (next == 0) next = 1;  // Prevent accidental wrapping to the inactive 0 state
  } while (!sequence_version_.compare_exchange_weak(prev, next, std::memory_order_release));

  // Stop any pending timer to avoid delayed executions colliding with our immediate step.
  // This is safe to call even if the timer isn't currently running.
  if (timer_handle_) {
    esp_timer_stop(timer_handle_);
  }

  // Queue an immediate execution step
  inflight_count_.fetch_add(1, std::memory_order_relaxed);
  main_loop.push<&MainLoopTaskBase::app_loop_step>(this);
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
  sequence_version_.store(1, std::memory_order_release);
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
  // Sample the current version. If 0, the sequence is inactive.
  uint32_t current_version = sequence_version_.load(std::memory_order_acquire);

  if (terminate_requested_ || current_version == 0) {
    goto exit_decrement;
  }

  // TRANSIENT PM LOCKS: Acquire only for the duration of this function
  pm_sleep_lock_.acquire_if_enabled();
  pm_apb_lock_.acquire_if_enabled();

  if (stop_requested_) {
    on_stop();
    // Try to deactivate, but ONLY if notify() hasn't bumped the version while we were running
    sequence_version_.compare_exchange_strong(current_version, 0, std::memory_order_release);
  } else {
    std::optional<uint32_t> delay_ms = on_step();

    if (delay_ms.has_value()) {
      if (!terminate_requested_ && timer_handle_) {
        // Always stop before starting to handle rapid-fire notify steps
        esp_timer_stop(timer_handle_);
        esp_timer_start_once(timer_handle_, *delay_ms * 1000ULL);
      }
    } else {
      on_stop();  // Natural completion
      // Try to deactivate, but ONLY if notify() hasn't bumped the version while we were running
      sequence_version_.compare_exchange_strong(current_version, 0, std::memory_order_release);
    }
  }

  // Release immediately so the ESP32 can sleep during the timer delay
  pm_sleep_lock_.release();
  pm_apb_lock_.release();

exit_decrement:
  // UAF Guard: Decrement and wake the destructor if it is waiting
  if (inflight_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    if (terminate_requested_ && join_sem_) {
      xSemaphoreGive(join_sem_);
    }
  }
}