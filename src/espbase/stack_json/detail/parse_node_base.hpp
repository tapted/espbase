#pragma once

#include <string_view>

#include "espbase/stack_json/path.hpp"

namespace sjson {

class ParseNodeBase {
 protected:
  PathBase path_{};
  bool is_set_ = false;
  bool is_null_ = false;
  bool is_catch_all_ = false;

  explicit constexpr ParseNodeBase(bool catch_all = false) : is_catch_all_(catch_all) {}
  ~ParseNodeBase() = default;

 public:
  const PathBase& path() const { return path_; }
  // Whether this node is a catch-all (matches any path). Used for dynamic nodes.
  bool is_catch_all() const { return is_catch_all_; }

  ParseNodeBase* init_node() { return this; }

  // The parser will call this when it finds a matching path
  virtual void assign(const PathBase& actual_path, std::string_view raw_val, bool is_string,
                      bool is_null) = 0;

  bool is_set() const { return is_set_; }
  bool is_null() const { return is_null_; }

  void reset() { is_set_ = is_null_ = false; }
};

}  // namespace sjson
