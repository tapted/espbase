#include <span>
#include "espbase/stack_json/document.hpp"

// Builder with stack_array and span_array
__attribute__((noinline))
std::size_t snippet_builder_arrays(sjson::Buffer& buffer, std::span<const int> dynamic_ints) {
  auto doc = sjson::stack_json(
      sjson::node("fixed_arr", sjson::stack_array(10, 20, 30)),
      sjson::node("span_arr", sjson::span_array(dynamic_ints))
  );
  return doc.emit(buffer);
}
