#include "espbase/mac_address.hpp"

#include <cstdio>
#include <cstring>
#include <functional>

#if LV_USE_SDL
#include <processthreadsapi.h>
#else
#include <esp_mac.h>
#endif

MacAddress::MacAddress(const uint8_t* buffer) {
  std::memcpy(bytes_.data(), buffer, 6);
}

const MacAddress& MacAddress::mine() {
  static const MacAddress my_mac = []() {
    uint8_t mac[6]{};
#if LV_USE_SDL
    DWORD pid = GetCurrentProcessId();
    mac[4] = (pid >> 8) & 0xFF;
    mac[5] = pid & 0xFF;
#else
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
    return MacAddress(mac);
  }();
  return my_mac;
}

int MacAddress::to_string(char (&buf)[18]) const {
  return std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bytes_[0], bytes_[1],
                       bytes_[2], bytes_[3], bytes_[4], bytes_[5]);
}

int MacAddress::to_id(char (&buf)[5]) const {
  return std::snprintf(buf, sizeof(buf), "%02X%02X", bytes_[4], bytes_[5]);
}

std::size_t std::hash<MacAddress>::operator()(const MacAddress& mac) const {
  // Pack the first 4 bytes into one integer, and the last 2 into another,
  // then XOR them. It's extremely fast and provides a great hash distribution.
  uint32_t a = (mac[0] << 24) | (mac[1] << 16) | (mac[2] << 8) | mac[3];
  uint32_t b = (mac[4] << 8) | mac[5];
  return hash<uint32_t>()(a) ^ hash<uint32_t>()(b);
}