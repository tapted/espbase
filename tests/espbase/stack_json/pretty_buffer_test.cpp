#include "espbase/stack_json/pretty_buffer.hpp"

#include <gtest/gtest.h>

#include "espbase/stack_json/document.hpp"

using namespace sjson;

TEST(PrettyBufferTest, PrettyPrintDecorator) {
  auto doc = stack_json(node("system", "espuck"),
                        node("wifi", stack_json(node("ssid", "Home_Network"),  //
                                                node("rssi", -65))),
                        node("flags", stack_array(1, 2)));

  StackBuffer<256> raw_buffer;
  PrettyBuffer pretty(raw_buffer, 2);  // 2 spaces per indent

  doc.emit(pretty);

  EXPECT_EQ(raw_buffer.view(),
            R"({
  "system": "espuck",
  "wifi": {
    "ssid": "Home_Network",
    "rssi": -65
  },
  "flags": [
    1,
    2
  ]
})");
}