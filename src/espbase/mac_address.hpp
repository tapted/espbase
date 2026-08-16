#pragma once

#include <array>
#include <cstdint>

// Represents a MAC address (6 bytes) and provides utility functions for formatting and comparison.
class MacAddress {
 public:
  constexpr MacAddress() : bytes_{0} {}
  constexpr MacAddress(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
      : bytes_{b0, b1, b2, b3, b4, b5} {}
  explicit MacAddress(const uint8_t* buffer);

  int to_string(char (&buf)[18]) const;
  int to_id(char (&buf)[5]) const;

  const uint8_t* data() const { return bytes_.data(); }

  constexpr uint8_t operator[](size_t index) const { return bytes_[index]; }
  constexpr bool operator==(const MacAddress& other) const { return bytes_ == other.bytes_; }
  constexpr bool operator!=(const MacAddress& other) const { return !(*this == other); }

  constexpr bool is_zero() const { return *this == zero(); }
  constexpr bool is_broadcast() const { return *this == broadcast(); }

  static constexpr MacAddress zero() { return MacAddress(); }
  static constexpr MacAddress broadcast() { return MacAddress(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF); }

  // Returns the MAC address of the current device. Singleton initialized by esp_read_mac() or a
  // fake initialized with a process ID to simulate a mesh.
  static const MacAddress& mine();

 private:
  std::array<uint8_t, 6> bytes_;
};

namespace std {

template <class>
struct hash;

template <>
struct hash<MacAddress> {
  size_t operator()(const MacAddress& mac) const;
};

}  // namespace std
