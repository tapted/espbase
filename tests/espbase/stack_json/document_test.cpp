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

TEST(DocumentTest, PerfectForwardingReferenceProof) {
  int battery_level = 50;

  // 1. Build the inner document.
  // It captures 'battery_level' by reference.
  auto device_doc = stack_json(node("name", "espuck"),  //
                               node("battery", battery_level));

  // 2. Build the outer document.
  // It captures 'device_doc' by reference! No copies are made.
  auto root_doc = stack_json(node("state_topic", "home/state"),  //
                             node("device", device_doc));

  // 3. Mutate the original variable AFTER the entire document tree is built
  battery_level = 99;

  StackBuffer<256> buffer;
  root_doc.emit(buffer);

  // If perfect forwarding failed (or copied), this would output 50.
  // Because it works, the emission evaluates the live references and outputs 99!
  EXPECT_EQ(buffer.view(),
            R"({"state_topic":"home/state","device":{"name":"espuck","battery":99}})");
}

TEST(DocumentTest, NestedArrayForwarding) {
  // Proving it works for arrays too
  auto wifi_features = stack_array("b", "g", "n");
  auto ble_features = stack_array("mesh", "beacon");

  // Both arrays are captured by reference
  auto capabilities = stack_json(node("wifi", wifi_features),  //
                                 node("bluetooth", ble_features));

  auto root = stack_json(node("capabilities", capabilities));

  StackBuffer<256> buffer;
  root.emit(buffer);

  EXPECT_EQ(buffer.view(),
            R"({"capabilities":{"wifi":["b","g","n"],"bluetooth":["mesh","beacon"]}})");
}

TEST(FormatTest, PrintfCallbackEscaping) {
  const char* config_identifier = "espuck";
  const char* object_id = "temp_01";

  auto doc = stack_json(
      node("name", "Sensor"),

      // Formats exactly like printf, straight into the OutputBuffer!
      node("unique_id", [&](auto& print) { print("%s_%s", config_identifier, object_id); }),

      // Proving the backwards escape algorithm handles chaotic data
      node("chaotic_topic", [&](auto& print) { print("home/%s\n\"quoted\"", "state"); }),

      node("print_twice", [&](auto& print) {
        print("repeat %s", "me");
        print(" and %s", "again");
      }));

  StackBuffer<256> buffer;
  doc.emit(buffer);

  EXPECT_EQ(
      buffer.view(),
      R"({"name":"Sensor","unique_id":"espuck_temp_01","chaotic_topic":"home/state\n\"quoted\"","print_twice":"repeat me and again"})");
}

TEST(NodeIfTest, RootKeyCondition) {
  bool is_active = true;
  bool is_hidden = false;

  auto doc = stack_json(node_if(is_active, "status", "online"),  //
                        node_if(is_hidden, "secret", "dont_show"));

  StackBuffer<128> buffer;
  doc.emit(buffer);

  // Only the true condition should be emitted
  EXPECT_EQ(buffer.view(), R"({"status":"online"})");
}

TEST(NodeIfTest, PointerNullCheck) {
  const char* valid_device = "sensor";
  const char* null_device = nullptr;

  auto config = path("config");

  auto doc = stack_json(
      // Using the 2-arg root key overload
      node_if("device_class", valid_device),  //
      node_if("missing_class", null_device),

      // Using the 2-arg path overload
      node_if(config("valid_name"), valid_device),  //
      node_if(config("missing_name"), null_device));

  StackBuffer<256> buffer;
  doc.emit(buffer);

  // Both null pointers should be silently dropped!
  // And thanks to std::move, valid_device correctly prints "sensor" instead of stack garbage
  EXPECT_EQ(buffer.view(), R"({"device_class":"sensor","config":{"valid_name":"sensor"}})");
}

// A stress test specifically for the dangling reference trap!
// We create a helper function that forces the const char* to be passed by value,
// mimicking exactly what caused the bug in the first place.
auto create_test_doc(const char* val1, const char* val2) {
  return stack_json(node_if("key1", val1), node_if("key2", val2));
}

TEST(NodeIfTest, DanglingReferenceTrap) {
  const char* data = "safe_data";

  // Create the document using the helper
  auto doc = create_test_doc(data, nullptr);

  // Allocate some stack variables here to deliberately overwrite
  // any dead stack frames left behind by create_test_doc
  volatile uint32_t garbage[] = {0xDEADBEEF, 0xBADF00D, 1, 2, 3};
  (void)garbage;

  StackBuffer<128> buffer;
  doc.emit(buffer);

  // If the node_if overload hadn't used std::move(val), this would print
  // garbage memory instead of "safe_data".
  EXPECT_EQ(buffer.view(), R"({"key1":"safe_data"})");
}