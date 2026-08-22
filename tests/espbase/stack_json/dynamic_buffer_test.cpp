#include "espbase/stack_json/dynamic_buffer.hpp"

#include <gtest/gtest.h>

#include "espbase/stack_json/document.hpp"

using namespace sjson;

TEST(DynamicBufferTest, GrowsAsNeeded) {
  auto doc = stack_json(node("system", "espuck"),
                        node("wifi", stack_json(node("ssid", "Home_Network"),  //
                                                node("rssi", -65))),
                        node("flags", stack_array(1, 2)));

  DynamicBuffer<std::vector<char>, 8> raw_buffer;

  doc.emit(raw_buffer);

  EXPECT_STREQ(
      raw_buffer.c_str(),
      R"({"system":"espuck","wifi":{"ssid":"Home_Network","rssi":-65},"flags":[1,2]})");
}