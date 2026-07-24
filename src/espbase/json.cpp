#include "espbase/json.hpp"

#include <esp_heap_caps.h>
#include <esp_log.h>

static void* cjson_psram_malloc(size_t size) {
  // MALLOC_CAP_SPIRAM targets the external chip.
  // MALLOC_CAP_8BIT ensures the memory is byte-addressable (required for strings/structs).
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void cjson_psram_free(void* ptr) {
  // In ESP-IDF, standard free() can actually resolve PSRAM pointers,
  // but using heap_caps_free() is safer and explicitly matches the allocator.
  heap_caps_free(ptr);
}

void init_json_to_use_psram() {
  cJSON_Hooks hooks = {.malloc_fn = cjson_psram_malloc, .free_fn = cjson_psram_free};
  cJSON_InitHooks(&hooks);
  ESP_LOGI("EspbaseJson", "Initialized cJSON to use PSRAM for allocations");
}