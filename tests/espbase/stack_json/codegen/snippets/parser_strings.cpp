#include <span>
#include <string>
#include <string_view>
#include "espbase/stack_json/parser.hpp"

// String variations: string_view, std::string, span<char>
__attribute__((noinline))
void snippet_parser_strings(std::string_view json, std::string_view& out_view,
                            std::string& out_str, std::span<char> out_span) {
  auto parser = sjson::json_parser(
      sjson::bind("view_val", out_view),
      sjson::bind("str_val", out_str),
      sjson::bind("span_val", out_span)
  );
  parser.parse(json);
}
