#pragma once

#include <utility>

namespace detail {
// Primary template
template <typename T>
struct mem_fn_traits;

// Partial specialization to extract the class type (C), return type (R), and Args...
template <typename R, typename C, typename... Args>
struct mem_fn_traits<R (C::*)(Args...)> {
  using class_type = C;
  using return_type = R;
  using c_func_type = R (*)(void*, Args...);
};

// Support for const methods
template <typename R, typename C, typename... Args>
struct mem_fn_traits<R (C::*)(Args...) const> {
  using class_type = C;
  using return_type = R;
  using c_func_type = R (*)(void*, Args...);
};

// Support for noexcept methods (C++17+)
template <typename R, typename C, typename... Args>
struct mem_fn_traits<R (C::*)(Args...) noexcept> {
  using class_type = C;
  using return_type = R;
  using c_func_type = R (*)(void*, Args...);
};

// Support for const noexcept methods
template <typename R, typename C, typename... Args>
struct mem_fn_traits<R (C::*)(Args...) const noexcept> {
  using class_type = C;
  using return_type = R;
  using c_func_type = R (*)(void*, Args...);
};
}  // namespace detail

/**
 * @brief Generates a zero-allocation C-style function pointer from a C++ member function.
 *
 * @tparam MemFn The member function pointer (e.g., &MyClass::my_method)
 * @return A C-style function pointer compatible with the member function's arguments.
 * @example `{ .callback = trampoline<&MyClass::my_method>(), .user_ctx = this }`
 */
template <auto MemFn>
constexpr typename detail::mem_fn_traits<decltype(MemFn)>::c_func_type trampoline() {
  using Traits = detail::mem_fn_traits<decltype(MemFn)>;
  using T = typename Traits::class_type;
  using R = typename Traits::return_type;
  using CFunc = typename Traits::c_func_type;

  // The generic lambda naturally decays to CFunc when explicitly cast.
  // The compiler deduces `auto...` to perfectly match `Args...`.
  return static_cast<CFunc>([](void* ptr, auto... args) -> R {
    return (static_cast<T*>(ptr)->*MemFn)(std::forward<decltype(args)>(args)...);
  });
}