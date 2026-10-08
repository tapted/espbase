#include <string_view>
#include "espbase/stack_json/parser.hpp"

// DynamicNode binding and dynamic extraction via >> and as<T>()
__attribute__((noinline))
void snippet_parser_dynamic_node(std::string_view json, int& out_int, std::string_view& out_str) {
  auto info = sjson::bind("info");
  auto parser = sjson::json_parser(info);
  parser.parse(json);

  if (info.is_set()) {
    info >> out_int;
    out_str = info.raw();
  }
}
