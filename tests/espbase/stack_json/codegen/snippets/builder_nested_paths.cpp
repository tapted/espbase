#include <string_view>
#include "espbase/stack_json/document.hpp"

// Builder with nested static paths (depth 2 and depth 3)
__attribute__((noinline))
std::size_t snippet_builder_nested_paths(sjson::Buffer& buffer, int ip_val, int temp_val) {
  auto doc = sjson::stack_json(
      sjson::node(sjson::path("network", "ip"), ip_val),
      sjson::node(sjson::path("device", "sensors", "temperature"), temp_val)
  );
  return doc.emit(buffer);
}
