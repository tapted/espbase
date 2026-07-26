#pragma once

namespace detail {
// Primary template
template <typename T>
struct mem_fn_traits;

// Partial specialization to extract the class type (C)
template <typename R, typename C, typename... Args>
struct mem_fn_traits<R (C::*)(Args...)> {
  using class_type = C;
  using return_type = R;
};

// Support for const methods
template <typename R, typename C, typename... Args>
struct mem_fn_traits<R (C::*)(Args...) const> {
  using class_type = C;
  using return_type = R;
};

// Support for noexcept methods (C++17+)
template <typename R, typename C, typename... Args>
struct mem_fn_traits<R (C::*)(Args...) noexcept> {
  using class_type = C;
  using return_type = R;
};

// Support for const noexcept methods
template <typename R, typename C, typename... Args>
struct mem_fn_traits<R (C::*)(Args...) const noexcept> {
  using class_type = C;
  using return_type = R;
};
}  // namespace detail

template <typename R>
using trampoline_func_t = R (*)(void*);

/**
 * @brief Generates a zero-allocation C-style function pointer from a C++ member function.
 *
 * @tparam MemFn The member function pointer (e.g., &MyClass::my_method)
 * @return `R (*)(void*)` A C-style function pointer compatible with FreeRTOS queues.
 * @example `{ .callback = trampoline<&MyClass::my_method>(), .user_ctx = this }`
 */
template <auto MemFn>
trampoline_func_t<typename detail::mem_fn_traits<decltype(MemFn)>::return_type> trampoline() {
  using T = typename detail::mem_fn_traits<decltype(MemFn)>::class_type;
  using R = typename detail::mem_fn_traits<decltype(MemFn)>::return_type;

  // The '+' forces the lambda to decay to `R (*)(void*)`
  return +[](void* ptr) -> R { return (static_cast<T*>(ptr)->*MemFn)(); };
}