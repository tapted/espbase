#include "espbase/stack_json/document.hpp"

#include <gtest/gtest.h>

using namespace sjson;

TEST(DocumentTest, DeviceTree) {
  auto device = path("device");
  auto device_prop = path("device", "prop");  // We can build composition helpers for this

  auto tree = stack_json(node(path("name"), "Temperatures"),
                         node(path("device", "identifier"), "my_device_id"),
                         node(path("device", "prop", "units"), "degrees")
                         // node(device_prop, 22) // (Assuming a NumberNode implementation)
  );

  StackBuffer<256> buffer;
  tree.emit(buffer);

  EXPECT_EQ(
      buffer.view(),
      R"({"name":"Temperatures","device":{"identifier":"my_device_id","prop":{"units":"degrees"}}})");
}