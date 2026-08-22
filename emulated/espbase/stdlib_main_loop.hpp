#pragma once

#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>

#include "espbase/trampoline.hpp"

struct AppCommand {
  void* instance;
  void (*execute)(void*);
};

// We keep the template argument so it matches your production signature,
// even though std::queue allocates dynamically.
template <size_t QueueSize = 128>
class MainLoop {
 public:
  MainLoop() {
    // Start the background worker thread immediately
    worker_ = std::thread([this]() {
      while (true) {
        AppCommand cmd;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          // Wait until there is work, or the destructor tells us to stop
          cv_.wait(lock, [this] { return !queue_.empty() || stop_requested_; });

          if (stop_requested_ && queue_.empty()) {
            break;  // Drain the queue completely before dying
          }

          cmd = queue_.front();
          queue_.pop();
        }

        // Execute outside the lock so tasks can push new tasks
        if (cmd.execute) {
          cmd.execute(cmd.instance);
        }
      }
    });
  }

  ~MainLoop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_requested_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  template <auto MemFn, typename T>
  bool push(T* instance) {
    using ExpectedClass = typename detail::mem_fn_traits<decltype(MemFn)>::class_type;

    static_assert(
        std::is_convertible_v<T*, ExpectedClass*>,
        "Type mismatch: The instance pointer is not compatible with this member function.");

    ExpectedClass* safe_instance = static_cast<ExpectedClass*>(instance);
    return push_func(trampoline<MemFn>(), safe_instance);
  }

  void run_forever() {
    // In production, this blocks the main task and processes the queue.
    // In the mock, we already spawned a background worker, so we just
    // block the caller to simulate the main thread halting.
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return stop_requested_; });
  }

  // --- MOCK ONLY UTILITIES ---

  // Tests can call this to block until all pending tasks are executed.
  // Example:
  //   device.poke();
  //   main_loop.wait_idle();
  //   REQUIRE(device.is_idle() == true);
  void wait_idle() {
    while (true) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return;
      }
      std::this_thread::yield();
    }
  }

 private:
  bool push_func(void (*execute)(void*), void* instance) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.size() >= QueueSize) {
        // Mimic the FreeRTOS xQueueSend returning pdFALSE
        std::cerr << "[Mock] MainLoop queue full!" << std::endl;
        return false;
      }
      queue_.push(AppCommand{.instance = instance, .execute = execute});
    }
    cv_.notify_one();  // Wake up the worker thread
    return true;
  }

  std::queue<AppCommand> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  bool stop_requested_ = false;
};

inline MainLoop main_loop;