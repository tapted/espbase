#include "espbase/stack_json/parser.hpp"

#include <gtest/gtest.h>

using namespace sjson;

TEST(ParserTest, LightSensorPayload) {
  std::string_view json =
      R"({"state": "ON", "brightness": "50", "color": {"r": 100, "g": 30, "b": 10}})";

  // Set some defaults
  std::string_view state = "OFF";
  int brightness = 0;
  uint8_t r = 0, g = 0, b = 0;

  auto color = path("color");

  // Notice we auto-convert the string "50" into the int brightness!
  auto parser = json_parser(bind("state", state),            //
                            bind("brightness", brightness),  //
                            bind(color("g"), g),             //
                            bind(color("r"), r),             //  order doesn't matter
                            bind(color("b"), b));

  parser.parse(json);

  EXPECT_EQ(state, "ON");
  EXPECT_EQ(brightness, 50);
  EXPECT_EQ(r, 100);
  EXPECT_EQ(g, 30);
  EXPECT_EQ(b, 10);

  // We can also check if the node at index 1 ("brightness") was present in the JSON
  EXPECT_TRUE(parser.template was_set<1>());
}

TEST(ParserTest, RawStringViewBinding) {
  // Contains an escaped newline and escaped quotes
  std::string_view json = R"({"text": "Line1\nLine2", "quote": "\"Hello\""})";

  std::string_view text, quote;

  auto parser = json_parser(bind("text", text),  //
                            bind("quote", quote));

  parser.parse(json);

  // The string views point at the raw JSON, so the slashes are still there
  EXPECT_EQ(text, "Line1\\nLine2");
  EXPECT_EQ(quote, "\\\"Hello\\\"");
}

TEST(ParserTest, AutoDecodeSpanBinding) {
  std::string_view json = R"({"text": "Line1\nLine2", "quote": "\"Hello\""})";

  // Allocate stack buffers for our strings
  char text_buf[32];
  char quote_buf[32];

  // Create full-capacity spans
  std::span<char> text_span(text_buf);
  std::span<char> quote_span(quote_buf);

  auto parser = json_parser(bind("text", text_span),  //
                            bind("quote", quote_span));

  parser.parse(json);

  // StackJson resized our spans to the exact decoded length!
  EXPECT_EQ(text_span.size(), 11);
  EXPECT_EQ(quote_span.size(), 7);

  // The data is fully un-escaped
  EXPECT_EQ(std::string_view(text_span.data(), text_span.size()), "Line1\nLine2");
  EXPECT_EQ(std::string_view(quote_span.data(), quote_span.size()), "\"Hello\"");
}

TEST(ParserTest, StdStringBinding) {
  std::string_view json = R"({
        "device_id": "espuck_beta_01",
        "description": "Board with\ttabs and \"quotes\""
    })";

  std::string device_id;
  std::string description;

  auto parser = json_parser(bind("device_id", device_id),  //
                            bind("description", description));

  parser.parse(json);

  // Extracted and fully decoded straight to the heap, safely and efficiently
  EXPECT_EQ(device_id, "espuck_beta_01");
  EXPECT_EQ(description, "Board with\ttabs and \"quotes\"");
}

TEST(ParserTest, StdStringNullBehavior) {
  std::string_view json = R"({"message": null})";

  std::string message = "Previous State";

  auto parser = json_parser(bind("message", message));

  parser.parse(json);

  // Because we implemented the `target_ = TargetT{};` reset for nulls,
  // a bound std::string will correctly be cleared to an empty string!
  EXPECT_TRUE(parser.template was_set<0>());
  EXPECT_EQ(message, "");
  EXPECT_TRUE(message.empty());
}

TEST(ParserTest, PrimitiveNullBehavior) {
  std::string_view json = R"({
        "temperature": null,
        "is_active": null,
        "label": null
    })";

  // Initialize with valid, non-zero data to prove they get overwritten
  int temperature = 22;
  bool is_active = true;
  std::string_view label = "Living Room";

  auto parser = json_parser(bind("temperature", temperature),  //
                            bind("is_active", is_active),      //
                            bind("label", label));

  parser.parse(json);

  // All of these should be reset to their default-constructed state
  EXPECT_TRUE(parser.template was_set<0>());
  EXPECT_EQ(temperature, 0);

  EXPECT_TRUE(parser.template was_set<1>());
  EXPECT_EQ(is_active, false);

  EXPECT_TRUE(parser.template was_set<2>());
  EXPECT_TRUE(label.empty());  // std::string_view default constructs to empty
}

TEST(ParserTest, ExplicitNullTracking) {
  // Case 1: Key exists, value is null
  std::string_view json1 = R"({"brightness": null})";

  int brightness = 100;
  auto brightness_node = bind("brightness", brightness);
  auto parser = json_parser(brightness_node);

  parser.parse(json1);

  EXPECT_TRUE(parser.was_set<0>());        // Found the key
  EXPECT_TRUE(parser.was_null<0>());       // It was explicit null
  EXPECT_EQ(brightness, 0);                // Target reset
  EXPECT_TRUE(brightness_node.is_null());  // Node reports null
  EXPECT_TRUE(brightness_node.is_set());   // Node reports set (value is null)

  // Case 2: Key does not exist at all
  std::string_view json2 = R"({"other": "value"})";

  brightness = 100;  // Reset to non-zero

  parser.parse(json2);

  EXPECT_FALSE(parser.was_set<0>());        // Key missing
  EXPECT_FALSE(parser.was_null<0>());       // Not null (key just didn't exist)
  EXPECT_EQ(brightness, 100);               // Value remains unchanged from previous parse
  EXPECT_FALSE(brightness_node.is_null());  // Node reports not null
  EXPECT_FALSE(brightness_node.is_set());   // Node reports not set
}

TEST(ParserTest, UnicodeHexDecoding) {
  // Tests \u00B0 (Degree symbol) which encodes to 2-byte UTF-8: 0xC2 0xB0
  // Tests \u2713 (Checkmark) which encodes to 3-byte UTF-8: 0xE2 0x9C 0x93
  std::string_view json = R"({
        "temp_unit": "24\u00B0C",
        "status": "Done \u2713"
    })";

  std::string temp_unit, status;

  auto parser = json_parser(bind("temp_unit", temp_unit),  //
                            bind("status", status));

  parser.parse(json);

  EXPECT_EQ(temp_unit, "24°C");
  EXPECT_EQ(status, "Done ✓");
}

TEST(ParserTest, TrailingCommasAndArrays) {
  // This JSON is technically invalid under strict RFC 8259:
  // 1. Trailing comma in the array
  // 2. Trailing comma in the object
  std::string_view json = R"({
        "temperature": 22,
        "history": [1, 2, 3,],
        "active": true,
    })";

  int temperature = 0;
  bool active = false;

  auto parser = json_parser(bind("temperature", temperature),  //
                            bind("active", active));

  parser.parse(json);

  // The parser successfully routes around the structural chaos
  EXPECT_TRUE(parser.was_set<0>());
  EXPECT_EQ(temperature, 22);

  EXPECT_TRUE(parser.was_set<1>());
  EXPECT_EQ(active, true);
}

TEST(ParserTest, CommentsAndJSONC) {
  std::string_view json = R"(
        {
            // The main configuration for the device
            "hostname": "espuck_01", /* Do not change this over OTA */
            "port": 8080 // Webserver port
        }
    )";

  std::string_view hostname;
  int port = 0;

  auto parser = json_parser(bind("hostname", hostname),  //
                            bind("port", port));

  parser.parse(json);

  EXPECT_TRUE(parser.was_set<0>());
  EXPECT_EQ(hostname, "espuck_01");

  EXPECT_TRUE(parser.was_set<1>());
  EXPECT_EQ(port, 8080);
}

TEST(ParserTest, OtaManifest) {
  std::string json = R"({
  "dongley": {
    "version": "46eccbf-46eccbf-r2",
    "image": "dongley.bin"
  }
})";

  std::string proj = "dongley";
  std::string new_version, image;

  // auto forbidden = path(std::string("dongley"));  // Can't bind to a temporary string.
  auto project = path(proj);
  auto parser = json_parser(bind(project("version"), new_version), bind(project("image"), image));
  parser.parse(json);

  EXPECT_STREQ(new_version.c_str(), "46eccbf-46eccbf-r2");
  EXPECT_STREQ(image.c_str(), "dongley.bin");
}

TEST(ParserTest, DynamicNodeVariantExtraction) {
  std::string_view json = R"({
        "dongley": {
            "version": "11",
            "image": "foo.bin"
        },
        "empty_val": null
    })";

  auto project = path("dongley");
  auto version = bind(project("version"));
  auto image = bind(project("image"));
  auto empty_val = bind("empty_val");
  auto not_set = bind("not_set");

  auto parser = json_parser(version, image, empty_val, not_set);
  parser.parse(json);

  // 1. State Tracking
  EXPECT_TRUE(image.is_set());
  EXPECT_FALSE(image.is_null());

  EXPECT_TRUE(empty_val.is_set());
  EXPECT_TRUE(empty_val.is_null());

  EXPECT_FALSE(not_set.is_set());
  EXPECT_FALSE(not_set.is_null());

  // 2. Extraction & Auto-Coercion
  int ver = 0;
  EXPECT_TRUE(version >> ver);
  EXPECT_EQ(ver, 11);

  std::string img;
  EXPECT_TRUE(image >> img);
  EXPECT_EQ(img, "foo.bin");

  // 3. Failed Extractions
  std::string fallback = "default";
  EXPECT_FALSE(not_set >> fallback);
  EXPECT_EQ(fallback, "default");  // Target remains untouched if not set
}

TEST(ParserTest, DynamicNodeSubParsing) {
  std::string_view json = R"({
        "dongley": {
            "info": {
                "name": "Dongley Project",
                "tags": 42
            }
        }
    })";

  auto info = bind(path("dongley", "info"));
  auto parser = json_parser(info);
  parser.parse(json);

  EXPECT_TRUE(info.is_set());

  std::string name;
  int tags = 0;

  // Spin up a sub-parser directly on the node's captured string block
  bool parsed = info.parse(bind("name", name), bind("tags", tags));

  EXPECT_TRUE(parsed);
  EXPECT_EQ(name, "Dongley Project");
  EXPECT_EQ(tags, 42);
}

TEST(ParserTest, DynamicNodeArrayIteration) {
  std::string_view json = R"({
        "tags": ["a", "b", "c"],
        "numbers": [10, 20, 30]
    })";

  auto tags = bind("tags");
  auto numbers = bind("numbers");
  auto parser = json_parser(tags, numbers);
  parser.parse(json);

  // Iterate strings
  std::vector<std::string> tag_results;
  std::string t;
  while (tags >> t) {
    tag_results.push_back(t);
  }
  ASSERT_EQ(tag_results.size(), 3);
  EXPECT_EQ(tag_results[0], "a");
  EXPECT_EQ(tag_results[1], "b");
  EXPECT_EQ(tag_results[2], "c");

  // Iterate ints
  std::vector<int> num_results;
  int n;
  while (numbers >> n) {
    num_results.push_back(n);
  }
  ASSERT_EQ(num_results.size(), 3);
  EXPECT_EQ(num_results[0], 10);
  EXPECT_EQ(num_results[1], 20);
  EXPECT_EQ(num_results[2], 30);
}

TEST(ParserTest, DynamicNodeObjectArrayIteration) {
  std::string_view json = R"({
        "sensors": [
            {"id": "temp", "val": 22.5},
            {"id": "hum", "val": 55.0}
        ]
    })";

  auto sensors = bind("sensors");
  auto parser = json_parser(sensors);
  parser.parse(json);

  std::vector<std::string> ids;
  std::vector<float> vals;

  // By extracting into a string_view, we grab the raw text of each array element!
  std::string_view obj_str;
  while (sensors >> obj_str) {
    std::string id;
    float val = 0;

    // Sub-parse the individual array element
    auto sub_parser = json_parser(bind("id", id), bind("val", val));
    sub_parser.parse(obj_str);

    ids.push_back(id);
    vals.push_back(val);
  }

  ASSERT_EQ(ids.size(), 2);
  EXPECT_EQ(ids[0], "temp");
  EXPECT_EQ(ids[1], "hum");
  EXPECT_FLOAT_EQ(vals[0], 22.5f);
  EXPECT_FLOAT_EQ(vals[1], 55.0f);
}
