#pragma once

#include <utility>

typedef struct esp_pm_lock* esp_pm_lock_handle_t;

// RAII wrapper for ESP-IDF power management locks. Automatically releases the lock on destruction.
class PmLock {
 public:
  enum LockType { CpuFreqMax, ApbFreqMax, NoLightSleep };

  // Construct a PmLock with the specified type. The lock is not enabled until enable() is called.
  explicit constexpr PmLock(LockType lock_type) : lock_type_(lock_type) {}
  ~PmLock() { enable(false); }

  PmLock(const PmLock&) = delete;
  PmLock& operator=(const PmLock&) = delete;

  PmLock(PmLock&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)),
        lock_type_(other.lock_type_),
        acquired_(std::exchange(other.acquired_, false)) {}

  PmLock& operator=(PmLock&& other) noexcept {
    if (this != &other) {
      enable(false);
      handle_ = std::exchange(other.handle_, nullptr);
      lock_type_ = other.lock_type_;
      acquired_ = std::exchange(other.acquired_, false);
    }
    return *this;
  }

  // Creates the lock if not already enabled, or deletes it if disabling. Returns esp_err_t.
  int enable(bool en, const char* name = "espbase_pm_lock", int arg = 0) {
    return en != is_enabled() ? create_or_delete(en, name, arg) : 0;
  }

  int acquire_if_enabled();
  int release();

  bool is_acquired() const { return acquired_; }
  bool is_enabled() const { return handle_ != nullptr; }

 private:
  int create_or_delete(bool create, const char* name, int arg);

  esp_pm_lock_handle_t handle_ = nullptr;
  LockType lock_type_;
  bool acquired_ = false;
};