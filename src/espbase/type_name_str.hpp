#pragma once

#include <array>
#include <string_view>

template <typename T>
consteval std::string_view type_name_view() {
#if defined(__clang__) || defined(__GNUC__)
  // GCC yields: "... [with T = halpp::LedStrip]"
  // Clang yields: "... [T = halpp::LedStrip]"
  std::string_view name = __PRETTY_FUNCTION__;

  std::size_t start = name.find("T = ") + 4;
  std::size_t end = name.find_first_of("];", start);

  return name.substr(start, end - start);
#else
  return "UnknownType";
#endif
}

template <typename T>
consteval auto type_name_array() {
  constexpr std::string_view sv = type_name_view<T>();
  std::array<char, sv.size() + 1> arr{};
  for (std::size_t i = 0; i < sv.size(); ++i) {
    arr[i] = sv[i];
  }
  arr[sv.size()] = '\0';  // Guarantee null-termination for C-strings
  return arr;
}

// Inline constexpr variable holding the statically allocated char array.
// `type_name_str<T>.data()` gives a null-terminated C-string representation of the type name.
template <typename T>
inline constexpr auto type_name_str = type_name_array<T>();
