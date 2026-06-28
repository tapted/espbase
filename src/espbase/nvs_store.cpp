#include "espbase/nvs_store.hpp"

#include <esp_log.h>
#include <nvs_flash.h>

static const char* TAG = "NvsStore";

EspResult<void> NvsStore::init_flash() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition corrupted. Rebuilding...");
    if (esp_err_t erase_err = nvs_flash_erase()) return erase_err;
    err = nvs_flash_init();
  }
  return err;
}

EspResult<NvsStore> NvsStore::open(const char* namespace_name, nvs_open_mode_t mode) {
  nvs_handle_t handle;
  if (esp_err_t err = nvs_open(namespace_name, mode, &handle)) {
    return EspResult<NvsStore>::fail(err);
  }
  return EspResult<NvsStore>::ok(NvsStore(handle));
}

void NvsStore::close() {
  if (handle_) {
    nvs_close(handle_);
    handle_ = 0;
  }
}

EspResult<void> NvsStore::set_i32(Key key, int32_t value) {
  if (!handle_) return ESP_ERR_INVALID_STATE;
  return nvs_set_i32(handle_, key, value);
}

EspResult<int32_t> NvsStore::get_i32(Key key) const {
  if (!handle_) return EspResult<int32_t>::fail(ESP_ERR_INVALID_STATE);
  int32_t val;
  if (esp_err_t err = nvs_get_i32(handle_, key, &val)) return EspResult<int32_t>::fail(err);
  return EspResult<int32_t>::ok(val);
}

EspResult<void> NvsStore::set_u32(Key key, uint32_t value) {
  if (!handle_) return ESP_ERR_INVALID_STATE;
  return nvs_set_u32(handle_, key, value);
}

EspResult<uint32_t> NvsStore::get_u32(Key key) const {
  if (!handle_) return EspResult<uint32_t>::fail(ESP_ERR_INVALID_STATE);
  uint32_t val;
  if (esp_err_t err = nvs_get_u32(handle_, key, &val)) return EspResult<uint32_t>::fail(err);
  return EspResult<uint32_t>::ok(val);
}

EspResult<void> NvsStore::set_string(Key key, const char* value) {
  if (!handle_) return ESP_ERR_INVALID_STATE;
  return nvs_set_str(handle_, key, value);
}

EspResult<size_t> NvsStore::get_string_length(Key key) const {
  if (!handle_) return EspResult<size_t>::fail(ESP_ERR_INVALID_STATE);
  size_t len = 0;
  if (esp_err_t err = nvs_get_str(handle_, key, nullptr, &len)) return EspResult<size_t>::fail(err);
  return EspResult<size_t>::ok(len);
}

EspResult<void> NvsStore::get_string(Key key, char* buffer, size_t max_len) const {
  if (!handle_) return ESP_ERR_INVALID_STATE;
  size_t len = max_len;
  return nvs_get_str(handle_, key, buffer, &len);
}

EspResult<void> NvsStore::set_raw_blob(Key key, const void* data, size_t len) {
  if (!handle_) return ESP_ERR_INVALID_STATE;
  return nvs_set_blob(handle_, key, data, len);
}

EspResult<void> NvsStore::get_raw_blob(Key key, void* data, size_t len) const {
  if (!handle_) return ESP_ERR_INVALID_STATE;

  size_t required_size = 0;
  esp_err_t err = nvs_get_blob(handle_, key, nullptr, &required_size);
  if (err != ESP_OK) return err;
  if (required_size != len) return ESP_ERR_INVALID_SIZE;

  return nvs_get_blob(handle_, key, data, &required_size);
}

EspResult<void> NvsStore::commit() {
  if (!handle_) return ESP_ERR_INVALID_STATE;
  return nvs_commit(handle_);
}
