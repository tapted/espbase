#include "espbase/stack_json/document.hpp"

// Builder with conditional nodes (boolean condition and pointer null check)
__attribute__((noinline))
std::size_t snippet_builder_conditional(sjson::Buffer& buffer, bool is_active,
                                       const char* optional_name, int status_code) {
  auto doc = sjson::stack_json(
      sjson::node_if(is_active, "status", status_code),
      sjson::node_if("name", optional_name)
  );
  return doc.emit(buffer);
}
