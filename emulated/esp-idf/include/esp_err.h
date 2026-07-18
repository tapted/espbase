#pragma once

#include <cstdio>
#include <cstdlib>

typedef int esp_err_t;

/* Definitions from esp_err.h */
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_SUPPORTED 0x106

/* Definitions from nvs.h */

#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_NAME (ESP_ERR_NVS_BASE + 0x06)
#define ESP_ERR_NVS_INVALID_HANDLE (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_REMOVE_FAILED (ESP_ERR_NVS_BASE + 0x08)
#define ESP_ERR_NVS_KEY_TOO_LONG (ESP_ERR_NVS_BASE + 0x09)
#define ESP_ERR_NVS_PAGE_FULL (ESP_ERR_NVS_BASE + 0x0a)
#define ESP_ERR_NVS_INVALID_STATE (ESP_ERR_NVS_BASE + 0x0b)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_VALUE_TOO_LONG (ESP_ERR_NVS_BASE + 0x0e)
#define ESP_ERR_NVS_PART_NOT_FOUND (ESP_ERR_NVS_BASE + 0x0f)

#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)
#define ESP_ERR_NVS_XTS_ENCR_FAILED (ESP_ERR_NVS_BASE + 0x11)
#define ESP_ERR_NVS_XTS_DECR_FAILED (ESP_ERR_NVS_BASE + 0x12)
#define ESP_ERR_NVS_XTS_CFG_FAILED (ESP_ERR_NVS_BASE + 0x13)
#define ESP_ERR_NVS_XTS_CFG_NOT_FOUND (ESP_ERR_NVS_BASE + 0x14)
#define ESP_ERR_NVS_ENCR_NOT_SUPPORTED (ESP_ERR_NVS_BASE + 0x15)
#define ESP_ERR_NVS_KEYS_NOT_INITIALIZED (ESP_ERR_NVS_BASE + 0x16)
#define ESP_ERR_NVS_CORRUPT_KEY_PART (ESP_ERR_NVS_BASE + 0x17)
#define ESP_ERR_NVS_WRONG_ENCRYPTION (ESP_ERR_NVS_BASE + 0x19)

/* Compatible check macro */
#define ESP_ERROR_CHECK(x)                                                                    \
  do {                                                                                        \
    esp_err_t __err_rc = (x);                                                                 \
    if (__err_rc != ESP_OK) {                                                                 \
      fprintf(stderr, "ESP_ERROR_CHECK failed: %d at %s:%d\n", __err_rc, __FILE__, __LINE__); \
      abort();                                                                                \
    }                                                                                         \
  } while (0)

#define esp_err_to_name(x) "FAKE_ESP_OK"