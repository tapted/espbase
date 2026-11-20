#pragma once

// Convenience umbrella: includes both the generation and parsing APIs.
// The using-declarations below are intentional for Arduino/embedded projects
// that want unqualified sjson names. Do NOT include this header inside the
// library itself — use the specific headers instead.
#include "espbase/stack_json/generate.hpp"
#include "espbase/stack_json/parse.hpp"

using sjson::bind;
using sjson::bind_unknown;
using sjson::Builder;
using sjson::json_parser;
using sjson::node;
using sjson::node_if;
using sjson::path;
using sjson::span_array;
using sjson::stack_array;
using sjson::stack_json;
using sjson::StackBuffer;
using sjson::StackBuilder;
using sjson::stream_array;