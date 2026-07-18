#pragma once

#include <cstdio>

/* Log Levels */
typedef enum {
  ESP_LOG_NONE,
  ESP_LOG_ERROR,
  ESP_LOG_WARN,
  ESP_LOG_INFO,
  ESP_LOG_DEBUG,
  ESP_LOG_VERBOSE
} esp_log_level_t;

/* ANSI Color Codes for the console */
#define LOG_CLR_E "\033[0;31m"
#define LOG_CLR_W "\033[0;33m"
#define LOG_CLR_I "\033[0;32m"
#define LOG_CLR_D "\033[0;34m"
#define LOG_CLR_RESET "\033[0m"

/* Compatible logging macros */
#define ESP_LOGE(tag, format, ...) \
  printf(LOG_CLR_E "E (%s) " format LOG_CLR_RESET "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) \
  printf(LOG_CLR_W "W (%s) " format LOG_CLR_RESET "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) \
  printf(LOG_CLR_I "I (%s) " format LOG_CLR_RESET "\n", tag, ##__VA_ARGS__)

#ifdef DEBUG_LOGGING
#define ESP_LOGD(tag, format, ...) \
  printf(LOG_CLR_D "D (%s) " format LOG_CLR_RESET "\n", tag, ##__VA_ARGS__)
#else
#define ESP_LOGD(tag, format, ...) \
  do {                             \
  } while (0)
#endif

#define ESP_LOGV(tag, format, ...) printf("V (%s) " format "\n", tag, ##__VA_ARGS__)

/* Stubs for log level control */
static inline void esp_log_level_set(const char* tag, esp_log_level_t level) {
  (void)tag;
  (void)level;
}