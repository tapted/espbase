#pragma once

#include <array>
#include <string>  // for a static_assert..
#include <string_view>
#include <type_traits>
#include <utility>

namespace sjson {

class PathBase {
 public:
  virtual ~PathBase() = default;
  virtual std::size_t depth() const = 0;
  virtual std::string_view get_element(std::size_t index) const = 0;

  bool matches_parent(const PathBase& parent) const {
    if (depth() < parent.depth()) return false;
    for (std::size_t i = 0; i < parent.depth(); ++i) {
      if (get_element(i) != parent.get_element(i)) return false;
    }
    return true;
  }
};

template <std::size_t Depth>
class StaticPath : public PathBase {
  std::array<std::string_view, Depth> elements_;

 private:
  // Forward references safely down the chain
  template <std::size_t... I, typename... Args>
  auto append_impl(std::index_sequence<I...>, Args&&... args) const {
    // Creates a new StaticPath with Depth + the number of new arguments
    return StaticPath<Depth + sizeof...(Args)>(elements_[I]..., std::forward<Args>(args)...);
  }

 public:
  static constexpr std::size_t static_depth = Depth;

  // Explicitly tell the compiler we want the standard copy/move operations
  StaticPath(const StaticPath&) = default;
  StaticPath(StaticPath&&) = default;
  StaticPath& operator=(const StaticPath&) = default;
  StaticPath& operator=(StaticPath&&) = default;

  // The variadic constructor with a constraint to prevent hijacking the copy constructor o_O.
  template <typename... Args, typename = std::enable_if_t<
                                  sizeof...(Args) != 1 ||
                                  (!std::is_same_v<std::decay_t<Args>, StaticPath<Depth>> && ...)>>
  constexpr StaticPath(Args&&... args) : elements_{std::string_view(std::forward<Args>(args))...} {
    static_assert(sizeof...(Args) == Depth, "Depth mismatch");

    // Robustness Guardrail: Block temporary std::strings from being bound!
    static_assert((... && !(std::is_same_v<std::decay_t<Args>, std::string> &&
                            std::is_rvalue_reference_v<Args&&>)),
                  "StackJson: Cannot bind a path to a temporary std::string! It will dangle.");
  }

  std::size_t depth() const override { return Depth; }
  std::string_view get_element(std::size_t index) const override { return elements_[index]; }

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
  const PathBase& original_;
  std::size_t limit_;

 public:
  PathView(const PathBase& orig, std::size_t limit) : original_(orig), limit_(limit) {}
  std::size_t depth() const override { return limit_; }
  std::string_view get_element(std::size_t index) const override {
    return original_.get_element(index);
  }
};

}  // namespace sjson