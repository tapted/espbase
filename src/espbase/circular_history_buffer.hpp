#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

class CircularHistoryBuffer {
 public:
  constexpr CircularHistoryBuffer() = default;
  ~CircularHistoryBuffer();

  void init(size_t size, uint32_t caps = MALLOC_CAP_SPIRAM);

  void register_listener(TaskHandle_t task);
  void remove_listener(TaskHandle_t task);
  void write(const char* data, size_t len);

  // Returns bytes read. Cursor tracks the absolute byte offset.
  size_t read_next(uint64_t& cursor, char* out_buf, size_t max_len);

 private:
  char* buffer_ = nullptr;
  size_t size_ = 0;
  uint64_t total_written_ = 0;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t listeners_[4]{nullptr};  // Supports 4 concurrent browser tabs
};
