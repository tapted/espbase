#include "espbase/stack_json/document.hpp"

#include <gtest/gtest.h>

using namespace sjson;

TEST(DocumentTest, DeviceTree) {
  auto device = path("device");
  auto device_prop = path("device", "prop");  // We can build composition helpers for this

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