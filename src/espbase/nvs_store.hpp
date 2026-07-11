/**
 * @file store.hpp
 * @brief Zero-allocation RAII wrapper for ESP-IDF Non-Volatile Storage
 */

#pragma once

#include <cstdint>
#include <nvs.h>

#include "espbase/esp_result.hpp"

class NvsStore {
 public:
  // Compile-time string length validation
  class Key {
   public:
    template <size_t N>
    constexpr Key(const char (&key)[N]) : key_str_(key) {
      static_assert(N <= 16, "NVS key exceeds the strict 15 character limit!");
    }
    constexpr operator const char*() const { return key_str_; }

   private:
    const char* key_str_;
  };

  // Rule of Zero: Pure constexpr default state
  constexpr NvsStore() = default;
  ~NvsStore() { close(); }

  // Strict ownership via move semantics
  NvsStore(const NvsStore&) = delete;
  NvsStore& operator=(const NvsStore&) = delete;

  NvsStore(NvsStore&& other) noexcept {
    handle_ = other.handle_;
    other.handle_ = 0;
  }

  NvsStore& operator=(NvsStore&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = other.handle_;
      other.handle_ = 0;
    }
    return *this;
  }

  explicit operator bool() const { return handle_ != 0; }

  // --- Static Factories & Initialization ---

  // Must be called once during boot (e.g., in app_main)
  static EspResult<void> init_flash();
  static EspResult<NvsStore> open(Key namespace_name, nvs_open_mode_t mode = NVS_READWRITE);

  void close();

  // --- Core API ---
  EspResult<void> set_i32(Key key, int32_t value);
  EspResult<int32_t> get_i32(Key key) const;

  EspResult<void> set_u32(Key key, uint32_t value);
  EspResult<uint32_t> get_u32(Key key) const;

  // --- Zero-Allocation Strings ---
  EspResult<void> set_string(Key key, const char* value);

  // Returns the exact size needed (including null terminator)
  EspResult<size_t> get_string_length(Key key) const;

  // Reads directly into a user-provided stack buffer
  EspResult<void> get_string(Key key, char* buffer, size_t max_len) const;

  EspResult<void> set_raw_blob(Key key, const void* data, size_t len);
  EspResult<void> get_raw_blob(Key key, void* data, size_t len) const;

  template <typename T>
  EspResult<void> set_blob(Key key, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "NVS Blobs must be trivially copyable POD types.");
    return set_raw_blob(key, &value, sizeof(T));
  }

  template <typename T>
  EspResult<T> get_blob(Key key) const {
    static_assert(std::is_trivially_copyable_v<T>,
                  "NVS Blobs must be trivially copyable POD types.");
    T value{};  // Zero-initialize safety
    auto res = get_raw_blob(key, &value, sizeof(T));
    return res ? EspResult<T>::ok(value) : EspResult<T>::fail(res.error());
  }

  EspResult<void> commit();

  // --- Schema Migration ---
  // Uses a template functor instead of std::function to avoid heap allocation
  template <typename F>
  EspResult<void> migrate_schema(uint32_t target_version, F migration_func) {
    if (!handle_) return ESP_ERR_INVALID_STATE;

    uint32_t current_version = 0;
    EspResult<uint32_t> version_res = get_u32("_schema_ver");

    if (version_res) {
      current_version = *version_res;
    } else if (version_res.error() != ESP_ERR_NVS_NOT_FOUND) {
      return version_res.error();  // Hardware error
    }

    if (current_version < target_version) {
      // Execute the user-provided lambda
      migration_func(*this, current_version);

      if (EspError err = set_u32("_schema_ver", target_version)) return err;
      if (EspError err = commit()) return err;
    }

    return ESP_OK;
  }

 private:
  nvs_handle_t handle_ = 0;

  // Private constructor invoked by the factory
  explicit NvsStore(nvs_handle_t handle) : handle_(handle) {}
};
