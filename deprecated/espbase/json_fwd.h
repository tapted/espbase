#pragma once

#include <memory>

#include "cJSON.h"

struct cJSONDeleter;
struct cJSONStringDeleter;

// We need the `cJSON` type, which is an anonymous struct, so we can't forward declare it.
using unique_cjson = std::unique_ptr<cJSON, cJSONDeleter>;
using unique_cjson_str = std::unique_ptr<char, cJSONStringDeleter>;

class JsonObjectBuilder;