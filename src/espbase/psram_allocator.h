#pragma once

#if LV_USE_SDL
#include <memory>

template <typename T>
using PSRAMAllocator = std::allocator<T>;

#else
#include <cstddef>

#include "esp_err.h"
#include "esp_heap_caps.h"

template <typename T>
struct PSRAMAllocator {
  using value_type = T;

  PSRAMAllocator() = default;
  template <class U>
  constexpr PSRAMAllocator(const PSRAMAllocator<U>&) noexcept {}

  T* allocate(std::size_t n) {
    if (n > std::size_t(-1) / sizeof(T)) {
      ESP_ERROR_CHECK(ESP_ERR_INVALID_SIZE);  // Panic: Invalid size
    }

    if (auto p = static_cast<T*>(heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM))) {
      return p;
    }

    // This will trigger the panic handler and print:
    // "ESP_ERROR_CHECK failed: esp_err_t 0x101 (ESP_ERR_NO_MEM) at psram_allocator.hpp:.."
    ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    return nullptr;  // Unreachable, but required to satisfy the compiler
  }

  void deallocate(T* p, std::size_t) noexcept { heap_caps_free(p); }
};

// Required boilerplate so the STL knows all instances of this allocator are interchangeable
template <class T, class U>
bool operator==(const PSRAMAllocator<T>&, const PSRAMAllocator<U>&) {
  return true;
}
template <class T, class U>
bool operator!=(const PSRAMAllocator<T>&, const PSRAMAllocator<U>&) {
  return false;
}
#endif  // LV_USE_SDL