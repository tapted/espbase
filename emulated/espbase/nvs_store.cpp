#include "espbase/nvs_store.hpp"

#include <cstring>
#include <string>
#include <unordered_map>

static const char* last_partition_name = nullptr;
static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> fake_store;

template <class T>
static esp_err_t set_num(const char* key, T value) {
  fake_store[last_partition_name][key] = std::to_string(value);
  return ESP_OK;
}
template <class t>
static esp_err_t get_num(const char* key, t* out_value) {
  auto it = fake_store[last_partition_name].find(key);
  if (it == fake_store[last_partition_name].end()) return ESP_ERR_NVS_NOT_FOUND;
  if constexpr (std::is_integral_v<t>) {
    if constexpr (std::is_unsigned_v<t>) {
      *out_value = static_cast<t>(std::stoul(it->second));
    } else {
      *out_value = static_cast<t>(std::stol(it->second));
    }
    return ESP_OK;
  } else {
    return ESP_ERR_NVS_TYPE_MISMATCH;
  }
}

static esp_err_t get_str(nvs_handle_t, const char* key, char* out_value, size_t* length) {
  auto it = fake_store[last_partition_name].find(key);
  if (it == fake_store[last_partition_name].end()) return ESP_ERR_NVS_NOT_FOUND;
  const std::string& stored_value = it->second;
  if (out_value == nullptr) {
    *length = stored_value.size() + 1;  // +1 for null terminator
    return ESP_OK;
  }
  if (*length < stored_value.size() + 1) return ESP_ERR_NVS_INVALID_LENGTH;
  std::strncpy(out_value, stored_value.c_str(), *length);
  return ESP_OK;
}

EspResult<NvsStore> NvsStore::open(Key, nvs_open_mode_t) {
  return EspResult<NvsStore>::ok(NvsStore(1));
}

void NvsStore::close() {
}

EspResult<void> NvsStore::set_i32(Key key, int32_t value) {
  return set_num(key, value);
}

EspResult<int32_t> NvsStore::get_i32(Key key) const {
  int32_t value;
  return get_num(key, &value) ? EspResult<int32_t>::ok(value)
                              : EspResult<int32_t>::fail(ESP_ERR_NVS_NOT_FOUND);
}

EspResult<void> NvsStore::set_u32(Key key, uint32_t value) {
  return set_num(key, value);
}

EspResult<uint32_t> NvsStore::get_u32(Key key) const {
  uint32_t value;
  return get_num(key, &value) ? EspResult<uint32_t>::ok(value)
                              : EspResult<uint32_t>::fail(ESP_ERR_NVS_NOT_FOUND);
}

EspResult<void> NvsStore::set_string(Key key, const char* value) {
  fake_store[last_partition_name][std::string(key)] = value;
  return ESP_OK;
}

EspResult<size_t> NvsStore::get_string_length(Key key) const {
  if (!handle_) return EspResult<size_t>::fail(ESP_ERR_INVALID_STATE);
  size_t len = 0;
  if (esp_err_t err = get_str(handle_, key, nullptr, &len)) return EspResult<size_t>::fail(err);
  return EspResult<size_t>::ok(len);
}

EspResult<void> NvsStore::get_string(Key key, char* buffer, size_t max_len) const {
  size_t len = max_len;
  return get_str(handle_, key, buffer, &len);
}

EspResult<void> NvsStore::set_raw_blob(Key, const void*, size_t) {
  return ESP_ERR_NOT_SUPPORTED;
  // if (!handle_) return ESP_ERR_INVALID_STATE;
  // return nvs_set_blob(handle_, key, data, len);
}

EspResult<void> NvsStore::get_raw_blob(Key, void*, size_t) const {
  return ESP_ERR_NOT_SUPPORTED;
  // if (!handle_) return ESP_ERR_INVALID_STATE;

  // size_t required_size = 0;
  // esp_err_t err = nvs_get_blob(handle_, key, nullptr, &required_size);
  // if (err != ESP_OK) return err;
  // if (required_size != len) return ESP_ERR_INVALID_SIZE;

  // return nvs_get_blob(handle_, key, data, &required_size);
}

EspResult<void> NvsStore::commit() {
  return ESP_OK;
}
