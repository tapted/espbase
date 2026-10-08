#include <string_view>
#include "espbase/stack_json/parser.hpp"

// Representative IoT entity parser (e.g. sensor/light payload)
__attribute__((noinline))
void snippet_parser_entity_payload(std::string_view json, std::string_view& name, bool& state,
                                   int& brightness, float& temp, bool& online) {
  auto parser = sjson::json_parser(
      sjson::bind("name", name),
      sjson::bind("state", state),
      sjson::bind("brightness", brightness),
      sjson::bind(sjson::path("measurements", "temperature"), temp),
      sjson::bind("online", online)
  );
  parser.parse(json);
}
