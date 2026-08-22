#pragma once

#include <array>
#include <span>
#include <string_view>
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
  // Helper to unpack existing elements and pass them to the new constructor
  template <std::size_t... I, typename... Args>
  auto append_impl(std::index_sequence<I...>, Args... args) const {
    // Creates a new StaticPath with Depth + the number of new arguments
    return StaticPath<Depth + sizeof...(Args)>(elements_[I]..., args...);
  }

 public:
  static constexpr std::size_t static_depth = Depth;

  template <typename... Args>
  constexpr StaticPath(Args... args) : elements_{std::string_view(args)...} {
    static_assert(sizeof...(Args) == Depth, "Depth mismatch");
  }

  std::size_t depth() const override { return Depth; }
  std::string_view get_element(std::size_t index) const override { return elements_[index]; }

  // Syntactic Sugar: Extend the path
  template <typename... Args>
  auto operator()(Args... args) const {
    return append_impl(std::make_index_sequence<Depth>{}, args...);
  }
};

template <typename... Args>
auto path(Args... args) {
  return StaticPath<sizeof...(Args)>(args...);
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