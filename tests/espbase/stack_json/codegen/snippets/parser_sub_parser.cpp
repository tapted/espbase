#include <string_view>
#include "espbase/stack_json/parser.hpp"

// DynamicNode with recursive sub-parsing
__attribute__((noinline))
void snippet_parser_sub_parser(std::string_view json, int& out_id, float& out_val) {
  auto sensor = sjson::bind("sensor");
  auto parser = sjson::json_parser(sensor);
  parser.parse(json);

  sensor.parse(
      sjson::bind("id", out_id),
      sjson::bind("val", out_val)
  );
}
