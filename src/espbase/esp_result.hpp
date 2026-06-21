#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <optional>

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

template <typename T = void>
class [[nodiscard]] EspResult : public EspResultBase {
 private:
  std::optional<T> value_;

 public:
  struct error_tag_t {};
  struct ok_tag_t {};
  static EspResult fail(esp_err_t e) { return EspResult(e, error_tag_t{}); }
  static EspResult ok(T val) { return EspResult(std::move(val), ok_tag_t{}); }

  constexpr EspResult(esp_err_t e, error_tag_t error_tag = error_tag_t{}) : EspResultBase(e) {}
  EspResult(T val, ok_tag_t ok_tag = ok_tag_t{}) : EspResultBase(ESP_OK), value_(std::move(val)) {}
  constexpr EspResult(EspError e);

  T& operator*() { return *value_; }
  T* operator->() { return &(*value_); }
  const T& operator*() const { return *value_; }
  const T* operator->() const { return &(*value_); }
};

template <>
class EspResult<void> : public EspResultBase {
 public:
  constexpr EspResult(esp_err_t e = ESP_OK) : EspResultBase(e) {}
  constexpr EspResult(EspError e);
};

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
}
constexpr EspResult<void>::EspResult(EspError e) : EspResultBase(e) {
}