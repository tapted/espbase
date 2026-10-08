#include <cstdint>
#include "espbase/stack_json/document.hpp"

// Multiple primitive scalars builder
__attribute__((noinline))
std::size_t snippet_builder_primitive_multi(sjson::Buffer& buffer, int val1, float val2,
                                           bool val3, uint32_t val4) {
  auto doc = sjson::stack_json(
      sjson::node("int_val", val1),
      sjson::node("float_val", val2),
      sjson::node("bool_val", val3),
      sjson::node("u32_val", val4)
  );
  return doc.emit(buffer);
}
