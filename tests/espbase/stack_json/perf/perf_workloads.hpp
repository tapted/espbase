#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/document.hpp"
#include "espbase/stack_json/parser.hpp"

namespace sjson::perf {

template <typename T>
inline void do_not_optimize(T&& val) {
#if defined(__clang__) || defined(__GNUC__)
  asm volatile("" : : "r,m"(val) : "memory");
#else
  volatile auto sink = val;
  (void)sink;
#endif
}

class FastBenchmarkBuffer : public Buffer {
  std::string buffer_;
  std::size_t head_ = 0;

 public:
  explicit FastBenchmarkBuffer(std::size_t capacity = 0) {
    if (capacity > 0) {
      buffer_.resize(capacity);
    }
  }

  void reset() {
    clear_fail();
    head_ = 0;
  }

  size_t write(std::string_view str) override {
    if (head_ + str.size() > buffer_.size()) {
      buffer_.resize(std::max(buffer_.size() * 2, head_ + str.size() + 65536));
    }
    std::copy(str.begin(), str.end(), buffer_.data() + head_);
    head_ += str.size();
    return str.size();
  }

  std::span<char> get_write_span() override {
    if (buffer_.size() - head_ < 256) {
      buffer_.resize(std::max(buffer_.size() * 2, head_ + 65536));
    }
    return std::span<char>(buffer_.data() + head_, buffer_.size() - head_);
  }

  void commit(std::size_t count) override { head_ += count; }
  char* head() override { return buffer_.data() + head_; }
  size_t length() const override { return head_; }

  std::string_view view() const { return {buffer_.data(), head_}; }

  void finalize() {
    if (head_ >= buffer_.size()) {
      buffer_.resize(head_ + 1);
    }
    buffer_[head_] = '\0';
  }
};

struct BenchmarkResult {
  std::string name;
  std::string description;
  std::size_t target_bytes = 0;
  std::size_t actual_bytes = 0;
  std::size_t num_objects = 0;
  double build_time_ms = 0.0;
  double parse_time_ms = 0.0;
  double build_mbs = 0.0;
  double parse_mbs = 0.0;
  double build_ops_sec = 0.0;
  double parse_ops_sec = 0.0;
  bool verified = false;
};

// 1. Primitives: Flat Integers & Booleans
struct PrimitivesWorkload {
  static constexpr const char* name = "primitives";
  static constexpr const char* description = "Integers & Booleans (id, timestamp, battery, flags, active, count)";

  static void build_one(Buffer& buf, std::size_t i) {
    uint32_t id = static_cast<uint32_t>(1000000 + i);
    uint64_t ts = 1712345678900ULL + i;
    int battery = static_cast<int>(100 - (i % 100));
    int flags = static_cast<int>(i % 256);
    bool active = (i % 2 == 0);
    int count = -static_cast<int>(i % 500);

    auto doc = stack_json(
        node("id", id),
        node("timestamp", ts),
        node("battery", battery),
        node("flags", flags),
        node("active", active),
        node("count", count)
    );
    doc.emit(buf);
  }

  static bool parse_all(std::string_view json, std::size_t expected_count) {
    auto items = bind("items");
    auto root_parser = json_parser(items);
    root_parser.parse(json);
    if (!items.is_set()) return false;

    uint32_t id = 0;
    uint64_t ts = 0;
    int battery = 0;
    int flags = 0;
    bool active = false;
    int count = 0;

    auto item_parser = json_parser(
        bind("id", id),
        bind("timestamp", ts),
        bind("battery", battery),
        bind("flags", flags),
        bind("active", active),
        bind("count", count)
    );

    std::string_view obj_str;
    std::size_t parsed = 0;
    uint64_t sum_id = 0;

    while (items >> obj_str) {
      item_parser.parse(obj_str);
      sum_id += id;
      parsed++;
    }

    do_not_optimize(sum_id);
    return parsed == expected_count;
  }
};

// 2. Floats: GPS & IMU Telemetry
struct FloatsWorkload {
  static constexpr const char* name = "floats";
  static constexpr const char* description = "GPS & IMU Telemetry (lat, lon, alt, speed, temp, ax, ay, az)";

  static void build_one(Buffer& buf, std::size_t i) {
    double lat = -33.868820 + (i * 0.00001);
    double lon = 151.209296 + (i * 0.00001);
    float alt = 42.15f + static_cast<float>(i % 1000) * 0.1f;
    float speed = 12.345f;
    float temp = 23.456f + static_cast<float>(i % 50) * 0.2f;
    float ax = 0.012f;
    float ay = -0.981f;
    float az = 0.045f;

    auto doc = stack_json(
        node("lat", lat),
        node("lon", lon),
        node("alt", alt),
        node("speed", speed),
        node("temp", temp),
        node("ax", ax),
        node("ay", ay),
        node("az", az)
    );
    doc.emit(buf);
  }

  static bool parse_all(std::string_view json, std::size_t expected_count) {
    auto items = bind("items");
    auto root_parser = json_parser(items);
    root_parser.parse(json);
    if (!items.is_set()) return false;

    double lat = 0.0, lon = 0.0;
    float alt = 0.0f, speed = 0.0f, temp = 0.0f, ax = 0.0f, ay = 0.0f, az = 0.0f;

    auto item_parser = json_parser(
        bind("lat", lat),
        bind("lon", lon),
        bind("alt", alt),
        bind("speed", speed),
        bind("temp", temp),
        bind("ax", ax),
        bind("ay", ay),
        bind("az", az)
    );

    std::string_view obj_str;
    std::size_t parsed = 0;
    double sum_lat = 0.0;

    while (items >> obj_str) {
      item_parser.parse(obj_str);
      sum_lat += lat;
      parsed++;
    }

    do_not_optimize(sum_lat);
    return parsed == expected_count;
  }
};

// 3. Strings: Text & Escaped Strings
struct StringsWorkload {
  static constexpr const char* name = "strings";
  static constexpr const char* description = "Text & Escaped Strings (level, source, message, url, tag)";

  static void build_one(Buffer& buf, std::size_t i) {
    static constexpr const char* levels[] = {"INFO", "WARN", "DEBUG", "ERROR"};
    const char* lvl = levels[i % 4];
    const char* src = "network_task";
    const char* msg = "Payload processed with \"status\": ok\n";
    const char* url = "https://api.espuck.io/v1/telemetry/report";
    const char* tag = "firmware_v2";

    auto doc = stack_json(
        node("level", lvl),
        node("source", src),
        node("message", msg),
        node("url", url),
        node("tag", tag)
    );
    doc.emit(buf);
  }

  static bool parse_all(std::string_view json, std::size_t expected_count) {
    auto items = bind("items");
    auto root_parser = json_parser(items);
    root_parser.parse(json);
    if (!items.is_set()) return false;

    std::string_view lvl, src, msg, url, tag;

    auto item_parser = json_parser(
        bind("level", lvl),
        bind("source", src),
        bind("message", msg),
        bind("url", url),
        bind("tag", tag)
    );

    std::string_view obj_str;
    std::size_t parsed = 0;
    std::size_t char_sum = 0;

    while (items >> obj_str) {
      item_parser.parse(obj_str);
      char_sum += lvl.size() + src.size() + msg.size() + url.size() + tag.size();
      parsed++;
    }

    do_not_optimize(char_sum);
    return parsed == expected_count;
  }
};

// 4. Nested Paths: Hierarchical Objects
struct NestedPathsWorkload {
  static constexpr const char* name = "nested_paths";
  static constexpr const char* description = "Multi-level Tree Paths (device.info.id/name, device.net.wifi.ssid/ip/rssi)";

  static void build_one(Buffer& buf, std::size_t i) {
    auto dev = path("device");
    auto info = dev("info");
    auto wifi = dev("net", "wifi");

    int id = static_cast<int>(1000 + (i % 10000));
    const char* name = "smart_puck";
    const char* ssid = "HomeMesh_5G";
    const char* ip = "192.168.1.100";
    int rssi = -65 - static_cast<int>(i % 30);

    auto doc = stack_json(
        node(info("id"), id),
        node(info("name"), name),
        node(wifi("ssid"), ssid),
        node(wifi("ip"), ip),
        node(wifi("rssi"), rssi)
    );
    doc.emit(buf);
  }

  static bool parse_all(std::string_view json, std::size_t expected_count) {
    auto items = bind("items");
    auto root_parser = json_parser(items);
    root_parser.parse(json);
    if (!items.is_set()) return false;

    auto dev = path("device");
    auto info = dev("info");
    auto wifi = dev("net", "wifi");

    int id = 0;
    std::string_view name;
    std::string_view ssid;
    std::string_view ip;
    int rssi = 0;

    auto item_parser = json_parser(
        bind(info("id"), id),
        bind(info("name"), name),
        bind(wifi("ssid"), ssid),
        bind(wifi("ip"), ip),
        bind(wifi("rssi"), rssi)
    );

    std::string_view obj_str;
    std::size_t parsed = 0;
    int sum_rssi = 0;

    while (items >> obj_str) {
      item_parser.parse(obj_str);
      sum_rssi += rssi;
      parsed++;
    }

    do_not_optimize(sum_rssi);
    return parsed == expected_count;
  }
};

// 5. Mixed IoT Payload: Composite Payload
struct MixedIotWorkload {
  static constexpr const char* name = "mixed_iot";
  static constexpr const char* description = "Realistic IoT Composite (id, name, active, sensors.temp/hum, status.code/msg, bat)";

  static void build_one(Buffer& buf, std::size_t i) {
    auto sensors = path("sensors");
    auto status = path("status");

    uint32_t id = static_cast<uint32_t>(200000 + i);
    const char* name = "puck_sensor_node";
    bool active = (i % 2 == 0);
    float temp = 22.5f + static_cast<float>(i % 20) * 0.1f;
    float hum = 55.0f + static_cast<float>(i % 30) * 0.2f;
    int code = 200;
    const char* msg = "OK";
    int bat = static_cast<int>(100 - (i % 100));

    auto doc = stack_json(
        node("id", id),
        node("name", name),
        node("active", active),
        node(sensors("temp"), temp),
        node(sensors("hum"), hum),
        node(status("code"), code),
        node(status("msg"), msg),
        node("battery", bat)
    );
    doc.emit(buf);
  }

  static bool parse_all(std::string_view json, std::size_t expected_count) {
    auto items = bind("items");
    auto root_parser = json_parser(items);
    root_parser.parse(json);
    if (!items.is_set()) return false;

    auto sensors = path("sensors");
    auto status = path("status");

    uint32_t id = 0;
    std::string_view name;
    bool active = false;
    float temp = 0.0f;
    float hum = 0.0f;
    int code = 0;
    std::string_view msg;
    int bat = 0;

    auto item_parser = json_parser(
        bind("id", id),
        bind("name", name),
        bind("active", active),
        bind(sensors("temp"), temp),
        bind(sensors("hum"), hum),
        bind(status("code"), code),
        bind(status("msg"), msg),
        bind("battery", bat)
    );

    std::string_view obj_str;
    std::size_t parsed = 0;
    uint32_t sum_id = 0;

    while (items >> obj_str) {
      item_parser.parse(obj_str);
      sum_id += id;
      parsed++;
    }

    do_not_optimize(sum_id);
    return parsed == expected_count;
  }
};

template <typename Workload>
BenchmarkResult run_benchmark(std::size_t target_bytes) {
  BenchmarkResult res;
  res.name = Workload::name;
  res.description = Workload::description;
  res.target_bytes = target_bytes;

  // Pre-allocate buffer to avoid allocation noise during benchmark
  std::size_t alloc_size = target_bytes + (target_bytes / 5) + 65536;
  FastBenchmarkBuffer buf(alloc_size);

  // --- BUILD PHASE ---
  auto t_build_start = std::chrono::high_resolution_clock::now();

  buf.write("{\"items\":[");
  std::size_t obj_count = 0;
  while (buf.length() < target_bytes) {
    if (obj_count > 0) buf.write(",");
    Workload::build_one(buf, obj_count);
    obj_count++;
  }
  buf.write("]}");
  buf.finalize();

  auto t_build_end = std::chrono::high_resolution_clock::now();

  res.actual_bytes = buf.length();
  res.num_objects = obj_count;
  res.build_time_ms = std::chrono::duration<double, std::milli>(t_build_end - t_build_start).count();
  res.build_mbs = (static_cast<double>(res.actual_bytes) / (1024.0 * 1024.0)) / (res.build_time_ms / 1000.0);
  res.build_ops_sec = static_cast<double>(res.num_objects) / (res.build_time_ms / 1000.0);

  // --- PARSE PHASE ---
  auto t_parse_start = std::chrono::high_resolution_clock::now();

  bool ok = Workload::parse_all(buf.view(), obj_count);

  auto t_parse_end = std::chrono::high_resolution_clock::now();

  res.parse_time_ms = std::chrono::duration<double, std::milli>(t_parse_end - t_parse_start).count();
  res.parse_mbs = (static_cast<double>(res.actual_bytes) / (1024.0 * 1024.0)) / (res.parse_time_ms / 1000.0);
  res.parse_ops_sec = static_cast<double>(res.num_objects) / (res.parse_time_ms / 1000.0);
  res.verified = ok;

  return res;
}

}  // namespace sjson::perf
