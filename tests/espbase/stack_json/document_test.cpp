#include "espbase/stack_json/document.hpp"

#include <gtest/gtest.h>

using namespace sjson;

TEST(BufferTest, CStringBehavior) {
  // Capacity for exactly 11 data characters (allocates 12 internally)
  StackBuffer<11> buffer;

  buffer.write("hello");

  // First read
  EXPECT_STREQ(buffer.c_str(), "hello");
  EXPECT_EQ(buffer.view().size(), 5);

  // Write more data (overwriting the previous null terminator)
  buffer.write(" world");

  // Second read
  EXPECT_STREQ(buffer.c_str(), "hello world");  // Fills buffer

  // Try to exceed the MaxSize (should be rejected)
  EXPECT_FALSE(buffer.write("d"));

  // The null terminator should still be exactly at the MaxSize boundary
  EXPECT_STREQ(buffer.c_str(), "hello world");
}

TEST(DocumentTest, DeviceTree) {
  auto tree = stack_json(node(path("name"), "Temperatures"),  //
                         node(path("device", "identifier"), "my_device_id"),
                         node(path("device", "prop", "units"), "degrees"),
                         node(path("device", "prop", "value"), 22));

  StackBuffer<256> buffer;
  tree.emit(buffer);

  EXPECT_EQ(
      buffer.view(),
      R"({"name":"Temperatures","device":{"identifier":"my_device_id","prop":{"units":"degrees","value":22}}})");
}

TEST(DocumentTest, DeviceTreeSugar) {
  auto device = path("device");
  auto prop = device("prop");
  auto tree = stack_json(node("name", "Temperatures"),                //
                         node(device("identifier"), "my_device_id"),  //
                         node(prop("units"), "degrees"),              //
                         node(prop("value"), 22));

  StackBuffer<256> buffer;
  tree.emit(buffer);

  // Same result.
  EXPECT_EQ(
      buffer.view(),
      R"({"name":"Temperatures","device":{"identifier":"my_device_id","prop":{"units":"degrees","value":22}}})");
}

TEST(DocumentTest, ArrayOfPrimitives) {
  auto tree = stack_json(node(path("numbers"), stack_array(1, 2, 3)),
                         node(path("strings"), stack_array("a", "b", "c")));

  StackBuffer<256> buffer;
  tree.emit(buffer);

  EXPECT_EQ(buffer.view(), R"({"numbers":[1,2,3],"strings":["a","b","c"]})");
}

TEST(DocumentTest, ArrayOfObjects) {
  auto tree = stack_json(node(path("users"),                                        //
                              stack_array(stack_json(node(path("id"), 1),           //
                                                     node(path("name"), "Alice")),  //
                                          stack_json(node(path("id"), 2),           //
                                                     node(path("name"), "Bob")))));

  StackBuffer<256> buffer;
  tree.emit(buffer);

  EXPECT_EQ(buffer.view(), R"({"users":[{"id":1,"name":"Alice"},{"id":2,"name":"Bob"}]})");
}

TEST(DocumentTest, NestedArrays) {
  auto tree = stack_json(node(path("matrix"),                 //
                              stack_array(stack_array(1, 0),  //
                                          stack_array(0, 1))));

  StackBuffer<256> buffer;
  tree.emit(buffer);

  EXPECT_EQ(buffer.view(), R"({"matrix":[[1,0],[0,1]]})");
}

TEST(DocumentTest, Escaping) {
  auto tree = stack_json(node(path("text"), "Line 1\nLine 2"),       //
                         node(path("quote"), "He said, \"Hello\""),  //
                         node(path("path"), "C:\\temp\\file.txt"),   //
                         node(path("tabbed"), "Col1\tCol2"));

  StackBuffer<256> buffer;
  tree.emit(buffer);

  EXPECT_EQ(
      buffer.view(),
      R"({"text":"Line 1\nLine 2","quote":"He said, \"Hello\"","path":"C:\\temp\\file.txt","tabbed":"Col1\tCol2"})");
}

TEST(DocumentTest, EscapedKeys) {
  auto tricky_path = path("weird\"key\nname");

  auto tree = stack_json(node(tricky_path, "value"));

  StackBuffer<128> buffer;
  tree.emit(buffer);

  EXPECT_EQ(buffer.view(), R"({"weird\"key\nname":"value"})");
}

TEST(PathTest, PathComposition) {
  auto device = path("device");
  auto device_prop = device("prop");

  // You can append single elements
  auto value_path = device_prop("value");
  EXPECT_EQ(value_path.depth(), 3);
  EXPECT_EQ(value_path.get_element(2), "value");

  // Or append multiple elements at once
  auto deep_path = device("prop", "sensors", "temperature");
  EXPECT_EQ(deep_path.depth(), 4);
  EXPECT_EQ(deep_path.get_element(3), "temperature");
}

TEST(DocumentTest, SugarSyntax) {
  auto device = path("device");
  auto device_prop = device("prop");

  auto tree = stack_json(node(path("name"), "Temperatures"),          //
                         node(device("identifier"), "my_device_id"),  //
                         node(device_prop("units"), "degrees"),       //
                         node(device_prop("value"), 22));

  StackBuffer<256> buffer;
  tree.emit(buffer);

  EXPECT_EQ(
      buffer.view(),
      R"({"name":"Temperatures","device":{"identifier":"my_device_id","prop":{"units":"degrees","value":22}}})");
}

TEST(DocumentTest, BooleanValues) {
  auto tree = stack_json(node(path("is_active"), true),  //
                         node(path("has_errors"), false));

  StackBuffer<128> buffer;
  tree.emit(buffer);

  EXPECT_EQ(buffer.view(), R"({"is_active":true,"has_errors":false})");
}

TEST(DocumentTest, NullValues) {
  auto tree = stack_json(
      // C++ nullptr translates perfectly to JSON null
      node(path("pending_command"), nullptr),  //
      node(path("calibration_data"), nullptr));

  StackBuffer<128> buffer;
  tree.emit(buffer);

  EXPECT_EQ(buffer.view(), R"({"pending_command":null,"calibration_data":null})");
}

TEST(DocumentTest, MixedDeviceState) {
  auto device = path("device");

  // A realistic payload mixing all our implemented types
  auto tree = stack_json(node(device("name"), "espuck_node_01"),  //
                         node(device("online"), true),            //
                         node(device("battery_level"), 98),       //
                         node(device("signal_strength"), -55.5),  //
                         node(device("last_error"), nullptr),
                         node(device("features"), stack_array("touch", "ble", "wifi")));

  StackBuffer<256> buffer;
  tree.emit(buffer);

  EXPECT_EQ(
      buffer.view(),
      R"({"device":{"name":"espuck_node_01","online":true,"battery_level":98,"signal_strength":-55.5,"last_error":null,"features":["touch","ble","wifi"]}})");
}

TEST(DocumentTest, SpanArrayOptions) {
  // Simulating your Select Entity configuration
  const char* const raw_options[] = {"Auto", "Heat", "Cool", "Off"};
  std::span<const char* const> options(raw_options);

  auto tree = stack_json(node(path("name"), "Thermostat Mode"),  //
                         node(path("options"), span_array(options)));

  StackBuffer<256> buffer;
  tree.emit(buffer);

  EXPECT_EQ(buffer.view(), R"({"name":"Thermostat Mode","options":["Auto","Heat","Cool","Off"]})");
}

TEST(DocumentTest, SpanArrayNumbers) {
  // Bonus: It automatically works for any type write_json_value supports!
  int raw_temps[] = {18, 20, 22, 24};
  std::span<int> temps(raw_temps);

  auto tree = stack_json(node(path("supported_temps"), span_array(temps)));

  StackBuffer<128> buffer;
  tree.emit(buffer);

  EXPECT_EQ(buffer.view(), R"({"supported_temps":[18,20,22,24]})");
}