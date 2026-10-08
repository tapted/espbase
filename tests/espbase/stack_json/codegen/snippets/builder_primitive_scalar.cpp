#include "espbase/stack_json/document.hpp"

// Single primitive scalar builder
__attribute__((noinline))
std::size_t snippet_builder_primitive_scalar(sjson::Buffer& buffer, int val) {
  auto doc = sjson::stack_json(sjson::node("status", val));
  return doc.emit(buffer);
}
