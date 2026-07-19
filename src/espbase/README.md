# espbase library source directory

This directory contains core hardware abstraction wrappers, network boot utilities, and zero-allocation utility types for the ESP32-S3.

## Technical Overview

### 1. Error Handling (`esp_result.hpp`)

`EspResult<T>` and `EspError` provide a modern C++ type-safe approach to value-or-error propagation, replacing standard output parameters and unsafe return codes.

- **`EspResultBase`**: Base class holding the underlying `esp_err_t`.
- **`EspResult<T>`**: Wrapper representing success (holding value of type `T` and returning true) or failure (holding error code). Overloaded `log_error` allows fluent logging chaining, falling back to `esp_err_t` on lvalues if `T` is non-copyable/non-movable to preserve safety.
- **`EspResult<void>`**: Specialization for functions returning no value on success.
- **`EspError`**: Lightweight wrapper representing a failed state, providing extraction helpers (`EspError::check(...)`).
- **`.strip()`**: Discards the success value to retrieve an `EspResult<void>`, which is recommended when returning/chaining error states for move-only or immovable types.

### 2. Task Management (`esp_task.hpp`, `yielding_task.hpp`)
*Placeholder: Core ESP-IDF FreeRTOS task wrappers and cooperative yielding task scheduling implementations.*

### 3. Non-Volatile Storage (`nvs_store.hpp`)
*Placeholder: Zero-allocation RAII wrappers for ESP-IDF Non-Volatile Storage (NVS).*

### 4. Memory Allocators (`psram_allocator.h`)
*Placeholder: Custom C++ allocators targeting ESP32 PSRAM.*

### 5. Serialization (`json.h`)
*Placeholder: Optimized compile-time and runtime JSON serialization utilities.*

### 6. Boot Utilities (`boot/`)
*Placeholder: Network loggers and boot configuration helpers.*
