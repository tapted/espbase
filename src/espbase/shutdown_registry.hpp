#pragma once

#include <cstddef>

class ShutdownRegistry {
 public:
  using ShutdownFn = void (*)();

  // Register a function to be called on shutdown
  static void register_fn(ShutdownFn fn);

  // Call all registered functions in reverse order
  static void shutdown_all();

 private:
  static constexpr size_t MAX_FUNCTIONS = 32;  // 128 bytes total RAM

  static ShutdownFn functions_[MAX_FUNCTIONS];
  static size_t count_;
};