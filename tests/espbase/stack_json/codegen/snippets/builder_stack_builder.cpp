#include "espbase/stack_json/builder.hpp"

// Builder merging multiple documents using StackBuilder
__attribute__((noinline))
std::size_t snippet_builder_stack_builder(sjson::Buffer& buffer, const char* name, int id) {
  auto doc1 = sjson::stack_json(sjson::node("name", name));
  auto doc2 = sjson::stack_json(sjson::node("id", id));

  sjson::StackBuilder<8> builder;
  builder.add(doc1).add(doc2);
  return builder.emit(buffer);
}
