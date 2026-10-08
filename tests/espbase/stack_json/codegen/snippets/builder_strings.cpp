#include <string>
#include <string_view>
#include "espbase/stack_json/document.hpp"

// Builder with string literal, string_view, and std::string
__attribute__((noinline))
std::size_t snippet_builder_strings(sjson::Buffer& buffer, std::string_view view_val,
                                   const std::string& str_val) {
  auto doc = sjson::stack_json(
      sjson::node("literal", "static_literal_text"),
      sjson::node("view", view_val),
      sjson::node("string", str_val)
  );
  return doc.emit(buffer);
}
