#pragma once

#include <cassert>
#include <esp_err.h>
#include <esp_log.h>
#include <optional>
#include <type_traits>

/**
 * @class EspResultBase
 * @brief Common base class for both EspResult<T> and EspResult<void>.
 *
 * Provides basic ESP-IDF error-code storage, boolean checking, and error logging utilities.
 * Derived classes inherit error-handling state and behavior, ensuring a consistent interface.
 */
class [[nodiscard]] EspResultBase {
 protected:
  esp_err_t err_;

  explicit constexpr EspResultBase(esp_err_t e) : err_(e) {}

 public:
  explicit operator bool() const { return err_ == ESP_OK; }
  esp_err_t error() const { return err_; }
  esp_err_t log_error(const char* tag, const char* msg) const {
    if (err_ != ESP_OK) {
      ESP_LOGE(tag, "%s: %s", msg, esp_err_to_name(err_));
    }
    return err_;
  }
};

class EspError;

/**
 * @class EspResult
 * @brief A value-or-error wrapper type optimized for ESP-IDF.
 *
 * Represents the outcome of an operation that returns a value of type T on success (ESP_OK),
 * or propagates an esp_err_t failure code. This class is designed to replace
 return-by-pointer/out-parameter
 * patterns, enforcing compiler-level checks that the result is handled (using [[nodiscard]]).
 *
 * Pattern / Rationale:
 * - On success, the wrapper holds the value T and returns true when checked as a boolean.
 * - On failure, the wrapper holds the error code and returns false when checked.
 * - Overloaded log_error() allows fluent call-chaining for logging. Use std::move to carry the
 *   result from an lvalue, or .strip() to discard the value (presumably after checking it's a
 *   failure).
 * - Implicit conversion constructors allow returning T directly for success, or returning
 *   ESP_FAIL/EspError directly for failure. If ESP_OK is returned implicitly, T is
 *   default-constructed (if default-constructible) to avoid uninitialized optional access.
 */
template <typename T = void>
class [[nodiscard]] EspResult : public EspResultBase {
 private:
  std::optional<T> value_;
  struct private_error_tag_t {};

  // Invoked from EspResult::fail(): map ESP_OK to ESP_FAIL to avoid accidental success propagation.
  constexpr EspResult(esp_err_t e, private_error_tag_t)
      : EspResultBase(e == ESP_OK ? ESP_FAIL : e) {}

 public:
  struct error_tag_t {};
  struct ok_tag_t {};

  static EspResult fail(esp_err_t e) { return EspResult(e, private_error_tag_t{}); }
  static EspResult ok(T val) { return EspResult(std::move(val), ok_tag_t{}); }

  constexpr EspResult(esp_err_t e, error_tag_t = error_tag_t{})
    requires(std::is_default_constructible_v<T>)
      : EspResultBase(e) {
    // Support, e.g., `EspResult<std::string> f() { return ESP_OK; }`. operator bool() will be true,
    // so we don't want an uninitialized optional. If T is not default-constructible, this will fail
    // to compile. Use EspResult::fail().
    if (e == ESP_OK) value_.emplace();
  }

  EspResult(T val, ok_tag_t = ok_tag_t{}) : EspResultBase(ESP_OK), value_(std::move(val)) {}
  constexpr EspResult(EspError e);

  T& operator*() { return *value_; }
  T* operator->() { return &(*value_); }
  const T& operator*() const { return *value_; }
  const T* operator->() const { return &(*value_); }

  // Allow explicit stripping of the value for easy returns of results that don't need the value.
  EspResult<void> strip() const;

  // Overload log_error to act as a pass-through. Use std::move if needed.
  EspResult<T> log_error(const char* tag, const char* msg) && {
    EspResultBase::log_error(tag, msg);
    return std::move(*this);
  }
};

/**
 * @class EspResult<void>
 * @brief Specialization of EspResult for operations that only return success or failure.
 *
 * Specialized wrapper for functions that do not return a value on success (i.e. return void),
 * but still propagate success/error status and allow chaining log_error().
 */
template <>
class EspResult<void> : public EspResultBase {
 public:
  constexpr EspResult(esp_err_t e = ESP_OK) : EspResultBase(e) {}
  constexpr EspResult(EspError e);

  // Overload log_error to act as a pass-through
  EspResult<void> log_error(const char* tag, const char* msg) const {
    EspResultBase::log_error(tag, msg);
    return *this;
  }
};

/**
 * @class EspError
 * @brief A lightweight, helper wrapper for propagating and checking esp_err_t error codes.
 *
 * Designed to work seamlessly with EspResult<T> and EspResult<void>. It allows functions to check
 * error states, log failures, and return/convert errors cleanly. It acts as an intermediate type
 * that represents a failed state, and provides static check methods for extraction.
 */
class [[nodiscard]] EspError {
  esp_err_t err_;

 public:
  // Implicitly constructible from esp_err_t or EspResultBase
  constexpr EspError(esp_err_t e) : err_(e) {}
  constexpr EspError(const EspResultBase& res) : err_(res.error()) {}

  explicit operator bool() const { return err_ != ESP_OK; }
  operator esp_err_t() const { return err_; }

  esp_err_t log(const char* tag, const char* msg) const {
    if (err_ != ESP_OK) {
      ESP_LOGE(tag, "%s: %s", msg, esp_err_to_name(err_));
    }
    return err_;
  }

  static EspError check(esp_err_t err) { return EspError(err); }
  static EspError check(EspResult<void> err) { return EspError(err); }

  template <typename T>
  static EspError check(EspResult<T>&& res, T* out_val) {
    if (res && out_val) {
      *out_val = std::move(*res);  // Extract value via move on success
    }
    return EspError(res.error());
  }
};

template <typename T>
constexpr EspResult<T>::EspResult(EspError e) : EspResultBase(e) {
  if (static_cast<esp_err_t>(e) == ESP_OK) value_.emplace();
}
constexpr EspResult<void>::EspResult(EspError e) : EspResultBase(e) {
}

template <typename T>
EspResult<void> EspResult<T>::strip() const {
  return EspResult<void>(err_);
}