#include <gtest/gtest.h>

#include "espbase/stack_json/builder.hpp"
#include "espbase/stack_json/document.hpp"

using namespace sjson;

TEST(BuilderTest, BasicAssembly) {
  StackBuilder<16> builder;

  auto doc1 = stack_json(node(path("sensor_type"), "temperature"));
  auto doc2 = stack_json(node(path("value"), 22.5));
  auto doc3 = stack_json(node_if(false, path("hidden"), "secret"));  // Should be omitted

  // Add them in sequence
  builder.add(doc1).add(doc2).add(doc3);

  StackBuffer<128> buffer;
  builder.emit(buffer);

  EXPECT_EQ(buffer.view(), R"({"sensor_type":"temperature","value":22.5})");
}

TEST(BuilderTest, CallStackSimulation) {
  StackBuffer<256> buffer;

  // Simulate the bottom layer (Device)
  auto inject_device = [&](Buffer& buf, Builder& b) {
    auto device_doc = stack_json(node(path("device", "name"), "espuck_01"),
                                 node(path("device", "manufacturer"), "espuck"));
    b.add(device_doc);
    b.emit(buf);  // Emit at the bottom
  };

  // Simulate the middle layer (Entity)
  auto inject_entity = [&](Buffer& buf, Builder& b) {
    char local_buf[] = "sensor.my_button";  // Stack-allocated string
    auto entity_doc = stack_json(node(path("name"), "My Button"),
                                 node(path("unique_id"), (const char*)local_buf));
    b.add(entity_doc);
    inject_device(buf, b);  // Pass down
  };

  // Simulate the top layer (Button::get_discovery_payload)
  StackBuilder<32> top_builder;
  auto top_doc =
      stack_json(node(path("command_topic"), "home/cmd"), node(path("icon"), "mdi:button"));
  top_builder.add(top_doc);

  // Kick off the chain
  inject_entity(buffer, top_builder);

  // The output should perfectly merge all three scopes
  EXPECT_EQ(
      buffer.view(),
      R"({"command_topic":"home/cmd","icon":"mdi:button","name":"My Button","unique_id":"sensor.my_button","device":{"name":"espuck_01","manufacturer":"espuck"}})");
}

TEST(BuilderTest, CapacityOverflowProtection) {
  // Only allocate space for 2 node pointers
  StackBuilder<2> builder;

  auto doc = stack_json(node(path("first"), 1), node(path("second"), 2),
                        node(path("third"), 3)  // This should be safely ignored
  );

  builder.add(doc);

  StackBuffer<128> buffer;
  builder.emit(buffer);

  // Because count maxes out at dest.size(), the third node is dropped
  EXPECT_EQ(buffer.view(), R"({"first":1,"second":2})");
}

TEST(BuilderTest, MergedObjectPaths) {
  StackBuilder<16> builder;

  // Proving that paths merge correctly even when added from different documents
  auto doc1 = stack_json(node(path("config", "retries"), 3));
  auto doc2 = stack_json(node(path("config", "timeout"), 1000));

  builder.add(doc1).add(doc2);

  StackBuffer<128> buffer;
  builder.emit(buffer);

  // Both should sit cleanly under the "config" object
  EXPECT_EQ(buffer.view(), R"({"config":{"retries":3,"timeout":1000}})");
}