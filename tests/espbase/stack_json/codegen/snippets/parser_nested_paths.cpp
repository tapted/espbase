#include <string_view>
#include "espbase/stack_json/parser.hpp"

// Nested paths with depth 2 and depth 3
__attribute__((noinline))
void snippet_parser_nested_paths(std::string_view json, int& out_val1, int& out_val2) {
  auto parser = sjson::json_parser(
      sjson::bind(sjson::path("network", "ip"), out_val1),
      sjson::bind(sjson::path("device", "sensors", "temperature"), out_val2)
  );
  parser.parse(json);
}
