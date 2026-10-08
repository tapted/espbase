#include <cstdint>
#include <string_view>
#include "espbase/stack_json/parser.hpp"

// Multiple primitive types: int, float, bool, uint32_t
__attribute__((noinline))
void snippet_parser_primitive_multi(std::string_view json, int& out_int, float& out_float,
                                    bool& out_bool, uint32_t& out_u32) {
  auto parser = sjson::json_parser(
      sjson::bind("int_val", out_int),
      sjson::bind("float_val", out_float),
      sjson::bind("bool_val", out_bool),
      sjson::bind("u32_val", out_u32)
  );
  parser.parse(json);
}
