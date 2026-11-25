#include <string_view>
#include "espbase/stack_json/parse.hpp"

// Single primitive scalar bind: isolates baseline parser template overhead
__attribute__((noinline))
void snippet_parser_primitive_scalar(std::string_view json, int& out_int) {
  auto parser = sjson::json_parser(sjson::bind("value", out_int));
  parser.parse(json);
}
