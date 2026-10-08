#include <string_view>
#include "espbase/stack_json/parser.hpp"

// Unknown path catch-all callback parser
__attribute__((noinline))
void snippet_parser_unknown_catchall(std::string_view json, int& out_fixed, int& out_dynamic_count) {
  auto parser = sjson::json_parser(
      [&out_dynamic_count](const sjson::PathBase&, sjson::DynamicNodeBase&) {
        out_dynamic_count++;
      },
      sjson::bind("fixed", out_fixed)
  );
  parser.parse(json);
}
