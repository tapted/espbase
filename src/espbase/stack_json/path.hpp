#pragma once

#include <array>
#include <string>  // for a static_assert..
#include <string_view>
#include <type_traits>
#include <utility>

namespace sjson {

namespace detail {
// Safely converts arguments to string_view, intercepting null pointers
template <typename T>
constexpr std::string_view safe_string_view(T&& arg) {
  // Use remove_reference_t instead of decay_t.
  // This ensures string literals (arrays) evaluate to false and bypass the check.
  using BaseT = std::remove_reference_t<T>;

  if constexpr (std::is_pointer_v<BaseT>) {
    if (arg == nullptr) return "null_path_error";
  }
  return std::string_view(std::forward<T>(arg));
}
}  // namespace detail

class PathBase {
 protected:
  const std::string_view* data_ = nullptr;
  std::size_t size_ = 0;

 public:
  constexpr PathBase() = default;
  constexpr PathBase(const std::string_view* data, std::size_t size) : data_(data), size_(size) {}

  constexpr std::size_t depth() const { return size_; }
  constexpr std::string_view get_element(std::size_t index) const { return data_[index]; }
  constexpr const std::string_view* data() const { return data_; }

  bool matches_parent(const PathBase& parent) const {
    if (size_ < parent.size_) return false;
    for (std::size_t i = 0; i < parent.size_; ++i) {
      if (data_[i] != parent.data_[i]) return false;
    }
    return true;
  }
};

template <std::size_t Depth>
struct StaticPathStorage {
  std::array<std::string_view, Depth> elements_{};
};

template <std::size_t Depth>
class StaticPath : private StaticPathStorage<Depth>, public PathBase {
 private:
  using Storage = StaticPathStorage<Depth>;
  using Storage::elements_;
  // Forward references safely down the chain
  template <std::size_t... I, typename... Args>
  auto append_impl(std::index_sequence<I...>, Args&&... args) const {
    // Creates a new StaticPath with Depth + the number of new arguments
    return StaticPath<Depth + sizeof...(Args)>(elements_[I]..., std::forward<Args>(args)...);
  }

 public:
  static constexpr std::size_t static_depth = Depth;

  // Standard copy/move operations updating data_ pointer to local elements_
  StaticPath(const StaticPath& other) : Storage(other), PathBase(this->elements_.data(), Depth) {}
  StaticPath(StaticPath&& other)
      : Storage(std::move(other)), PathBase(this->elements_.data(), Depth) {}
  StaticPath& operator=(const StaticPath& other) {
    if (this != &other) {
      elements_ = other.elements_;
    }
    return *this;
  }
  StaticPath& operator=(StaticPath&& other) {
    if (this != &other) {
      elements_ = std::move(other.elements_);
    }
    return *this;
  }

  // The variadic constructor with a constraint to prevent hijacking the copy constructor.
  template <typename... Args, typename = std::enable_if_t<
                                  sizeof...(Args) != 1 ||
                                  (!std::is_same_v<std::decay_t<Args>, StaticPath<Depth>> && ...)>>
  constexpr StaticPath(Args&&... args)
      : Storage{{detail::safe_string_view(std::forward<Args>(args))...}},
        PathBase(this->elements_.data(), Depth) {
    static_assert(sizeof...(Args) == Depth, "Depth mismatch");

    // Robustness Guardrail: Block temporary std::strings from being bound!
    static_assert((... && !(std::is_same_v<std::decay_t<Args>, std::string> &&
                            std::is_rvalue_reference_v<Args&&>)),
                  "StackJson: Cannot bind a path to a temporary std::string! It will dangle.");
  }

  // Forward arguments when extending the path
  template <typename... Args>
  auto operator()(Args&&... args) const {
    return append_impl(std::make_index_sequence<Depth>{}, std::forward<Args>(args)...);
  }
};

template <typename... Args>
auto path(Args&&... args) {
  return StaticPath<sizeof...(Args)>(std::forward<Args>(args)...);
}

// A lightweight view used during recursive traversal to represent the "open" parent
class PathView : public PathBase {
 public:
  constexpr PathView(const PathBase& orig, std::size_t limit) : PathBase(orig.data(), limit) {}
};

}  // namespace sjson