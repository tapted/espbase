#include "espbase/stack_json/document.hpp"

// Builder with nested document (sub-object)
__attribute__((noinline))
std::size_t snippet_builder_nested_doc(sjson::Buffer& buffer, const char* ssid, int rssi) {
  auto doc = sjson::stack_json(
      sjson::node("system", "espuck"),
      sjson::node("wifi", sjson::stack_json(
          sjson::node("ssid", ssid),
          sjson::node("rssi", rssi)
      ))
  );
  return doc.emit(buffer);
}
