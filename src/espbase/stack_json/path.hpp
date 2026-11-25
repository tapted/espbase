#pragma once

#include <string_view>
#include <type_traits>
#include <utility>

namespace sjson {

/**
 * @brief Runtime path view used in parser callbacks.
 *
 * @details Represents a non-owning view over an array of `std::string_view`
 * key segments. Users encounter `const PathBase&` in `bind_unknown()` /
 * catch-all parser callbacks to inspect the full key path of an unmatched
 * JSON property.
 */
class PathBase {
 protected:
  const std::string_view* data_ = nullptr;  ///< Pointer to path segment array.
  std::size_t size_ = 0;                    ///< Number of segments in the path.

 public:
  constexpr PathBase() = default;

  /// @brief Constructs a `PathBase` over @p data with @p size segments.
  constexpr PathBase(const std::string_view* data, std::size_t size) : data_(data), size_(size) {}

  /// @brief Returns the number of segments in the path.
  constexpr std::size_t depth() const { return size_; }

  /// @brief Returns the key segment at @p index (0-based, root to leaf).
  constexpr std::string_view get_element(std::size_t index) const { return data_[index]; }

  /// @brief Returns a pointer to the underlying array of key segments.
  constexpr const std::string_view* data() const { return data_; }

  /**
   * @brief Returns `true` if this path starts with all segments of @p parent.
   *
   * @param parent Prefix path to test against.
   * @return `true` if `depth() >= parent.depth()` and all leading segments match.
   */
  bool matches_parent(const PathBase& parent) const {
    if (size_ < parent.size_) return false;
    for (std::size_t i = 0; i < parent.size_; ++i) {
      if (data_[i] != parent.data_[i]) return false;
    }
    return true;
  }
};

}  // namespace sjson

// Pull in StaticPath, PathView, and sjson::detail::safe_string_view
#include "espbase/stack_json/detail/path_impl.hpp"

namespace sjson {

/**
 * @brief Creates a compile-time JSON object path from one or more string keys.
 *
 * @details Returns a `StaticPath<Depth>` whose segments can be extended via
 * `operator()` chaining to group related keys cleanly without runtime overhead.
 * Temporary `std::string` rvalues are rejected at compile time to prevent dangling.
 *
 * @tparam Args String-like key types (`const char*`, `std::string_view`, lvalue `std::string`).
 * @param args Key segments from root to leaf.
 * @return A `StaticPath<sizeof...(Args)>` instance.
 *
 * @code{.cpp}
 * auto device = sjson::path("device");
 * auto batt   = device("power", "battery");  // => ["device", "power", "battery"]
 *
 * auto doc = sjson::stack_json(
 *     sjson::node(device("name"), "espuck"),
 *     sjson::node(batt, 98)
 * );
 * @endcode
 */
template <typename... Args>
auto path(Args&&... args) {
  return StaticPath<sizeof...(Args)>(std::forward<Args>(args)...);
}

}  // namespace sjson
