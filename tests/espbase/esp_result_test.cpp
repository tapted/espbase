#include "espbase/esp_result.hpp"

#include <gtest/gtest.h>
#include <string>

// Test case structure mirroring the src tree

// 1. Helper functions for the bug reproduction
EspResult<std::string> string_success() {
  return std::string("success");
}

EspResult<std::string> call_string_success() {
  return string_success().log_error("TAG", "foo failed");
}

EspResult<std::string> string_fail() {
  return ESP_FAIL;
}

EspResult<std::string> call_string_fail() {
  return string_fail().log_error("TAG", "expected failure");
}

// 2. Test the specific bug and chaining behaviour
TEST(EspResultTest, BugReproduction_StringSuccess) {
  auto result = call_string_success();
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "success");
}

TEST(EspResultTest, Chaining_StringFail) {
  auto result = call_string_fail();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), ESP_FAIL);
}

// 3. Test EspResult<void>
TEST(EspResultTest, VoidSuccess) {
  EspResult<void> res;  // default is ESP_OK
  EXPECT_TRUE(res);
  EXPECT_EQ(res.error(), ESP_OK);
}

TEST(EspResultTest, VoidFail) {
  EspResult<void> res(ESP_FAIL);
  EXPECT_FALSE(res);
  EXPECT_EQ(res.error(), ESP_FAIL);
}

TEST(EspResultTest, VoidChaining) {
  EspResult<void> res(ESP_FAIL);
  auto chained = res.log_error("TAG", "void err");
  EXPECT_FALSE(chained);
  EXPECT_EQ(chained.error(), ESP_FAIL);
}

// 4. Test EspResult<T> Basic Operations
TEST(EspResultTest, T_Success) {
  auto res = EspResult<int>::ok(42);
  EXPECT_TRUE(res);
  EXPECT_EQ(res.error(), ESP_OK);
  EXPECT_EQ(*res, 42);

  // Test const access
  const auto const_res = EspResult<int>::ok(100);
  EXPECT_TRUE(const_res);
  EXPECT_EQ(*const_res, 100);
}

struct Dummy {
  std::string value;
};

TEST(EspResultTest, T_ArrowOperator) {
  auto res = EspResult<Dummy>::ok(Dummy{"hello"});
  EXPECT_TRUE(res);
  EXPECT_EQ(res->value, "hello");

  const auto const_res = EspResult<Dummy>::ok(Dummy{"world"});
  EXPECT_TRUE(const_res);
  EXPECT_EQ(const_res->value, "world");
}

// 5. Test EspError
TEST(EspErrorTest, Basic) {
  EspError err1(ESP_FAIL);
  EXPECT_TRUE(err1);
  EXPECT_EQ(static_cast<esp_err_t>(err1), ESP_FAIL);

  EspError err2(ESP_OK);
  EXPECT_FALSE(err2);
  EXPECT_EQ(static_cast<esp_err_t>(err2), ESP_OK);
}

TEST(EspErrorTest, CheckMethods) {
  // Test check(esp_err_t)
  auto err1 = EspError::check(ESP_FAIL);
  EXPECT_TRUE(err1);

  // Test check(EspResult<void>)
  EspResult<void> res_void(ESP_FAIL);
  auto err2 = EspError::check(res_void);
  EXPECT_TRUE(err2);

  // Test check(EspResult<T>&&, T*)
  auto res_int = EspResult<int>::ok(1337);
  int val = 0;
  auto err3 = EspError::check(std::move(res_int), &val);
  EXPECT_FALSE(err3);
  EXPECT_EQ(val, 1337);
}

// 6. Test default construction on ESP_OK
TEST(EspResultTest, DefaultConstructOnOk) {
  // Creating an EspResult<int> with ESP_OK via error constructor should default-construct value_ to 0
  EspResult<int> res(ESP_OK, EspResult<int>::error_tag_t{});
  EXPECT_TRUE(res);
  EXPECT_EQ(res.error(), ESP_OK);
  EXPECT_EQ(*res, 0);

  // Creating an EspResult<std::string> via EspError with ESP_OK should default construct value_ to ""
  EspError ok_err(ESP_OK);
  EspResult<std::string> res_str(ok_err);
  EXPECT_TRUE(res_str);
  EXPECT_EQ(res_str.error(), ESP_OK);
  EXPECT_EQ(*res_str, "");
}

struct NonDefaultConstructible {
  int value;
  explicit NonDefaultConstructible(int v) : value(v) {}
};

TEST(EspResultTest, NonDefaultConstructible) {
  EspResult<NonDefaultConstructible> res(ESP_FAIL, EspResult<NonDefaultConstructible>::error_tag_t{});
  EXPECT_FALSE(res);
  EXPECT_EQ(res.error(), ESP_FAIL);
}

struct Immovable {
  Immovable() = default;
  Immovable(const Immovable&) = delete;
  Immovable& operator=(const Immovable&) = delete;
  Immovable(Immovable&&) = delete;
  Immovable& operator=(Immovable&&) = delete;
};

TEST(EspResultTest, ImmovableTypeChainingFallback) {
  // Test lvalue
  EspResult<Immovable> res(ESP_FAIL, EspResult<Immovable>::error_tag_t{});
  esp_err_t err1 = res.log_error("TAG", "Immovable lvalue error");
  EXPECT_EQ(err1, ESP_FAIL);

  // Test const lvalue
  const EspResult<Immovable> const_res(ESP_FAIL, EspResult<Immovable>::error_tag_t{});
  esp_err_t err2 = const_res.log_error("TAG", "Immovable const lvalue error");
  EXPECT_EQ(err2, ESP_FAIL);

  // Test rvalue
  esp_err_t err3 = EspResult<Immovable>(ESP_FAIL, EspResult<Immovable>::error_tag_t{}).log_error("TAG", "Immovable rvalue error");
  EXPECT_EQ(err3, ESP_FAIL);
}

struct MoveOnly {
  MoveOnly() = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;
};

TEST(EspResultTest, MoveOnlyTypeChaining) {
  // Test lvalue (returns esp_err_t)
  EspResult<MoveOnly> res(ESP_FAIL, EspResult<MoveOnly>::error_tag_t{});
  esp_err_t err1 = res.log_error("TAG", "MoveOnly lvalue error");
  EXPECT_EQ(err1, ESP_FAIL);

  // Test const lvalue (returns esp_err_t)
  const EspResult<MoveOnly> const_res(ESP_FAIL, EspResult<MoveOnly>::error_tag_t{});
  esp_err_t err2 = const_res.log_error("TAG", "MoveOnly const lvalue error");
  EXPECT_EQ(err2, ESP_FAIL);

  // Test rvalue (returns EspResult<MoveOnly> because it is move-constructible)
  EspResult<MoveOnly> rvalue_res = EspResult<MoveOnly>::fail(ESP_FAIL).log_error("TAG", "MoveOnly rvalue error");
  EXPECT_FALSE(rvalue_res);
  EXPECT_EQ(rvalue_res.error(), ESP_FAIL);
}
