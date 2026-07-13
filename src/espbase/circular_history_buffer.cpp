#include "espbase/circular_history_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>

void CircularHistoryBuffer::init(size_t size, uint32_t caps) {
  size_ = size;
  buffer_ = (char*)heap_caps_malloc(size_, caps);
}

CircularHistoryBuffer::~CircularHistoryBuffer() {
  if (buffer_) heap_caps_free(buffer_);
}

void CircularHistoryBuffer::register_listener(TaskHandle_t task) {
  taskENTER_CRITICAL(&lock_);
  for (auto& listener : listeners_) {
    if (listener == nullptr) {
      listener = task;
      break;
    }
  }
  taskEXIT_CRITICAL(&lock_);
}

void CircularHistoryBuffer::remove_listener(TaskHandle_t task) {
  taskENTER_CRITICAL(&lock_);
  for (auto& listener : listeners_) {
    if (listener == task) {
      listener = nullptr;
    }
  }
  taskEXIT_CRITICAL(&lock_);
}

void CircularHistoryBuffer::write(const char* data, size_t len) {
  TaskHandle_t to_notify[4] = {nullptr};

  taskENTER_CRITICAL(&lock_);
  if (len > size_) {
    data += (len - size_);
    len = size_;
  }

  size_t offset = total_written_ % size_;
  size_t space_until_wrap = size_ - offset;

  // Fast memory wrap around
  if (len <= space_until_wrap) {
    std::memcpy(buffer_ + offset, data, len);
  } else {
    std::memcpy(buffer_ + offset, data, space_until_wrap);
    std::memcpy(buffer_, data + space_until_wrap, len - space_until_wrap);
  }
  total_written_ += len;

  // Copy listeners to avoid triggering FreeRTOS APIs inside a spinlock
  for (int i = 0; i < 4; ++i) to_notify[i] = listeners_[i];
  taskEXIT_CRITICAL(&lock_);

  // Broadcast instant wakeup to all connected HTTP sockets
  for (auto task : to_notify) {
    if (task) xTaskNotifyGive(task);
  }
}

// Returns bytes read. Cursor tracks the absolute byte offset.
size_t CircularHistoryBuffer::read_next(uint64_t& cursor, char* out_buf, size_t max_len) {
  taskENTER_CRITICAL(&lock_);

  // Initial dump: Start at the oldest available byte
  if (cursor == 0 && total_written_ > 0) {
    cursor = (total_written_ > size_) ? (total_written_ - size_) : 0;
  }

  // Snap forward if the writer lapped this reader
  if (total_written_ > size_ && cursor < (total_written_ - size_)) {
    cursor = total_written_ - size_;
  }

  size_t available = total_written_ - cursor;
  size_t to_read = std::min<size_t>(available, max_len);

  if (to_read > 0) {
    size_t offset = cursor % size_;
    size_t space_until_wrap = size_ - offset;

    if (to_read <= space_until_wrap) {
      std::memcpy(out_buf, buffer_ + offset, to_read);
    } else {
      std::memcpy(out_buf, buffer_ + offset, space_until_wrap);
      std::memcpy(out_buf + space_until_wrap, buffer_, to_read - space_until_wrap);
    }
    cursor += to_read;
  }

  taskEXIT_CRITICAL(&lock_);
  return to_read;
}
