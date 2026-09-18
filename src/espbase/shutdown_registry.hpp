#pragma once

// Gives a mechanism for initialized modules to register a function to be called before entering
// deep sleep. Set an RTC_DATA_ATTR for the next init if the module needs special treatment when
// resuming. Threadsafe.
class ShutdownRegistry {
 public:
  using ShutdownFn = void (*)();

  // Register a function to be called on shutdown
  static void register_fn(ShutdownFn fn);

  // Call all registered functions in reverse order
  static void shutdown_all();
};