#pragma once

#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "cJSON.h"
#include "espbase/json_fwd.h"

// RAII Deleters
struct cJSONDeleter {
  void operator()(cJSON* p) const { cJSON_Delete(p); }
};
struct cJSONStringDeleter {
  void operator()(char* p) const { cJSON_free(p); }
};

// Owns the root node and handles serialization
class JsonDocument {
  unique_cjson root_;

 public:
  JsonDocument() : root_(cJSON_CreateObject()) {}

  cJSON* get() const { return root_.get(); }

  std::string to_string(bool formatted = false) const {
    unique_cjson_str str{formatted ? cJSON_Print(root_.get())
                                   : cJSON_PrintUnformatted(root_.get())};
    return str ? std::string(str.get()) : "";
  }
};

// A non-owning C++ view of a single cJSON node
class JsonNodeView {
  cJSON* node_;

 public:
  explicit JsonNodeView(cJSON* node = nullptr) : node_(node) {}

  // Check if this node exists and is valid
  explicit operator bool() const { return node_ != nullptr; }

  // Expose raw node if fallback is ever needed
  cJSON* get() const { return node_; }

  // --- Object Navigation ---
  JsonNodeView operator[](const char* key) const {
    if (!node_ || !cJSON_IsObject(node_)) return JsonNodeView(nullptr);
    return JsonNodeView(cJSON_GetObjectItem(node_, key));
  }

  // Assigns `key`'s value to `value` if it exists. Returns true if the value changed.
  bool change(std::string& value, const char* key) const {
    if (auto state = (*this)[key].as_string()) {
      if (*state != value) {
        value = *state;
        return true;
      }
    }
    return false;
  }

  // Direct Bool Overload (For standard JSON true/false)
  bool change(bool& value, const char* key) const {
    if (auto state = (*this)[key].as_bool()) {
      if (*state != value) {
        value = *state;
        return true;
      }
    }
    return false;
  }

  // Generic Numeric Overload (uint8_t, int, float, double, size_t)
  template <typename T>
    requires(std::integral<T> || std::floating_point<T>) && (!std::same_as<T, bool>)
  bool change(T& value, const char* key) const {
    // Relying on as_double() protects against cJSON's 32-bit signed valueint overflow
    if (auto state = (*this)[key].as_double()) {
      T new_value = static_cast<T>(*state);
      if (new_value != value) {
        value = new_value;
        return true;
      }
    }
    return false;
  }

  // Generic Mapper Overload (String -> Anything)
  template <typename T, std::invocable<std::string_view> Mapper>
  bool change(T& value, const char* key, Mapper mapper) const {
    if (auto state = (*this)[key].as_string()) {
      T new_value = mapper(*state);
      if (new_value != value) {
        value = new_value;
        return true;
      }
    }
    return false;
  }

  // --- Safe Value Extractors ---
  std::optional<std::string_view> as_string() const {
    if (node_ && cJSON_IsString(node_) && node_->valuestring) {
      return std::string_view(node_->valuestring);  // Zero-copy view of cJSON's internal buffer
    }
    return std::nullopt;
  }

  std::optional<int> as_int() const {
    if (node_ && cJSON_IsNumber(node_)) {
      return node_->valueint;
    }
    return std::nullopt;
  }

  std::optional<double> as_double() const {
    if (node_ && cJSON_IsNumber(node_)) {
      return node_->valuedouble;
    }
    return std::nullopt;
  }

  std::optional<bool> as_bool() const {
    if (node_ && cJSON_IsBool(node_)) {
      return cJSON_IsTrue(node_);
    }
    return std::nullopt;
  }
};

// C++20 Forward Iterator for cJSON linked lists
class JsonIterator {
  cJSON* current_;

 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = JsonNodeView;
  using difference_type = std::ptrdiff_t;
  using pointer = JsonNodeView*;
  using reference = JsonNodeView&;

  explicit JsonIterator(cJSON* ptr) : current_(ptr) {}

  JsonNodeView operator*() const { return JsonNodeView(current_); }

  JsonIterator& operator++() {
    if (current_) current_ = current_->next;
    return *this;
  }
  JsonIterator operator++(int) {
    JsonIterator tmp = *this;
    ++(*this);
    return tmp;
  }

  friend bool operator==(const JsonIterator& a, const JsonIterator& b) {
    return a.current_ == b.current_;
  }
  friend bool operator!=(const JsonIterator& a, const JsonIterator& b) {
    return a.current_ != b.current_;
  }
};

template <typename T>
concept JsonSupported = std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, const char*> ||
                        std::is_same_v<T, bool> || std::is_arithmetic_v<T>;

class JsonArrayBuilder {
  cJSON* target_;

 public:
  explicit JsonArrayBuilder(cJSON* target) : target_(target) {}

  template <JsonSupported T>
  JsonArrayBuilder& push(T val) {
    if constexpr (std::is_same_v<T, std::nullptr_t>)
      cJSON_AddItemToArray(target_, cJSON_CreateNull());
    else if constexpr (std::is_same_v<T, const char*>)
      cJSON_AddItemToArray(target_, cJSON_CreateString(val));
    else if constexpr (std::is_same_v<T, bool>)
      cJSON_AddItemToArray(target_, val ? cJSON_CreateTrue() : cJSON_CreateFalse());
    else
      cJSON_AddItemToArray(target_, cJSON_CreateNumber(static_cast<double>(val)));
    return *this;
  }

  // Nested helpers
  template <typename F>
  JsonArrayBuilder& push_object(F&& callback);
  template <typename F>
  JsonArrayBuilder& push_array(F&& callback);
};

// ---------------------------------------------------------
// 2. GENERIC OBJECT BUILDER (CRTP Base)
// ---------------------------------------------------------
template <typename Derived>
class GenericObjectBuilder {
 protected:
  cJSON* target_;
  explicit GenericObjectBuilder(cJSON* target = nullptr) : target_(target) {}
  cJSON* existing_obj(const char* key) const {
    return cJSON_GetObjectItemCaseSensitive(target_, key);
  }
  void delete_existing(cJSON* existing) {
    if (existing) cJSON_Delete(cJSON_DetachItemViaPointer(target_, existing));
  }

 public:
  bool exists(const char* key) const { return existing_obj(key) != nullptr; }

  template <JsonSupported T>
  Derived& add(const char* key, T val) {
    if constexpr (std::is_same_v<T, std::nullptr_t>)
      cJSON_AddNullToObject(target_, key);
    else if constexpr (std::is_same_v<T, const char*>)
      cJSON_AddStringToObject(target_, key, val);
    else if constexpr (std::is_same_v<T, bool>)
      cJSON_AddBoolToObject(target_, key, val);
    else
      cJSON_AddNumberToObject(target_, key, static_cast<double>(val));
    return static_cast<Derived&>(*this);
  }
  Derived& add(const char* key, const std::string& val) { return add(key, val.c_str()); }
  Derived& add_raw(const char* key, const char* raw_json) {
    cJSON_AddRawToObject(target_, key, raw_json);
    return static_cast<Derived&>(*this);
  }
  Derived& add_graft(const char* key, cJSON* node) {
    cJSON_AddItemToObject(target_, key, cJSON_Duplicate(node, 1));
    return static_cast<Derived&>(*this);
  }

  template <JsonSupported T>
  Derived& set(const char* key, T val) {
    cJSON* existing = existing_obj(key);
    if (!existing) return add(key, val);

    if (cJSON_IsNull(existing)) {
      if constexpr (std::is_same_v<T, std::nullptr_t>) {
        return static_cast<Derived&>(*this);  // null => null
      } else {
        delete_existing(existing);
        return add(key, val);  // null => value
      }
    }

    // Except for null, type conversions are not supported here!

    if constexpr (std::is_same_v<T, std::nullptr_t>) {
      delete_existing(existing);
      return add(key, nullptr);  // value => null
    } else if constexpr (std::is_same_v<T, const char*>) {
      cJSON_SetValuestring(existing, val);
    } else if constexpr (std::is_same_v<T, bool>) {
      cJSON_SetBoolValue(existing, val);
    } else {
      cJSON_SetNumberValue(existing, static_cast<double>(val));
    }
    return static_cast<Derived&>(*this);
  }
  Derived& set(const char* key, const std::string& val) { return set(key, val.c_str()); }

  Derived& set(const char* key, const std::string_view& value) {
    // 64 bytes is virtually free on the stack but easily covers 99% of MQTT payloads
    if (value.length() < 64) {
      char buf[64];
      std::memcpy(buf, value.data(), value.length());
      buf[value.length()] = '\0';
      cJSON_AddStringToObject(target_, key, buf);
    } else {
      // Fallback for massive strings: Accepts 1 temporary heap allocation
      cJSON_AddStringToObject(target_, key, std::string(value).c_str());
    }
    return static_cast<Derived&>(*this);
  }

  Derived& set_raw(const char* key, const char* raw_json) {
    delete_existing(existing_obj(key));
    cJSON_AddRawToObject(target_, key, raw_json);
    return static_cast<Derived&>(*this);
  }

  // Spawn nested builders
  template <typename F>
  Derived& with_object(const char* key, F&& callback);
  template <typename F>
  Derived& with_array(const char* key, F&& callback);
};

// ---------------------------------------------------------
// 3. THE PROXY OBJECT
// ---------------------------------------------------------
class JsonObjectBuilder : public GenericObjectBuilder<JsonObjectBuilder> {
 public:
  explicit JsonObjectBuilder(cJSON* target) : GenericObjectBuilder(target) {}
};

// ---------------------------------------------------------
// 4. INLINE LAMBDA RESOLUTION
// ---------------------------------------------------------
template <typename Derived>
template <typename F>
Derived& GenericObjectBuilder<Derived>::with_object(const char* key, F&& callback) {
  JsonObjectBuilder proxy(cJSON_AddObjectToObject(target_, key));
  callback(proxy);
  return static_cast<Derived&>(*this);
}

template <typename Derived>
template <typename F>
Derived& GenericObjectBuilder<Derived>::with_array(const char* key, F&& callback) {
  JsonArrayBuilder proxy(cJSON_AddArrayToObject(target_, key));
  callback(proxy);
  return static_cast<Derived&>(*this);
}

template <typename F>
JsonArrayBuilder& JsonArrayBuilder::push_object(F&& callback) {
  cJSON* child = cJSON_CreateObject();
  cJSON_AddItemToArray(target_, child);
  JsonObjectBuilder proxy(child);
  callback(proxy);
  return *this;
}

template <typename F>
JsonArrayBuilder& JsonArrayBuilder::push_array(F&& callback) {
  cJSON* child = cJSON_CreateArray();
  cJSON_AddItemToArray(target_, child);
  JsonArrayBuilder proxy(child);
  callback(proxy);
  return *this;
}