#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/detail/document_impl.hpp"

namespace sjson {

/**
 * @brief Non-templated base class for collecting multiple Documents and emitting them as a single JSON object.
 *
 * @details Typical use is accepting `Builder&` across function boundaries or in
 * virtual methods (escaping the `Document<Nodes...>` template type) so multiple
 * layers can contribute nodes into one merged JSON object without heap allocation.
 * Instantiate via `StackBuilder<MaxSize>`.
 */
class Builder {
 protected:
  /// @brief Span over the caller-owned storage array of `NodeBase*`.
  std::span<NodeBase*> storage_;
  /// @brief Number of node pointers currently collected.
  std::size_t count_ = 0;

  /// @brief Protected constructor; instantiate via a subclass such as `StackBuilder`.
  explicit Builder(std::span<NodeBase*> storage) : storage_(storage) {}

 public:
  // Delete copy/move to prevent accidental slicing or dangling spans
  Builder(const Builder&) = delete;
  Builder& operator=(const Builder&) = delete;

  /**
   * @brief Appends all node pointers from @p doc into this builder.
   *
   * @details Nodes beyond the builder's capacity are safely ignored.
   * @tparam Nodes Template parameter pack of @p doc.
   * @param doc Source document whose nodes must outlive `emit()`.
   * @return Reference to `*this` for call chaining.
   */
  template <typename... Nodes>
  Builder& add(Document<Nodes...>& doc) {
    doc.append_pointers_to(storage_, count_);
    return *this;
  }

  /**
   * @brief Evaluates all collected nodes and emits them as a single JSON object into @p buffer.
   *
   * @param buffer Destination output buffer.
   * @return Number of bytes written to @p buffer.
   */
  size_t emit(Buffer& buffer) const {
    // subspan(0, count_) creates a view of only the valid, populated pointers
    return emit_json_nodes(buffer, storage_.subspan(0, count_));
  }
};

/**
 * @brief Stack-allocated `Builder` with space for up to @p MaxSize node pointers.
 *
 * @tparam MaxSize Maximum total number of nodes (not documents) that can be added.
 *
 * @code{.cpp}
 * void Entity::inject_into(sjson::Buffer& buffer, sjson::Builder& builder) const {
 *     auto base_doc = sjson::stack_json(sjson::node("unique_id", this->id));
 *     builder.add(base_doc);
 *     builder.emit(buffer);
 * }
 *
 * sjson::StackBuffer<256> buffer;
 * sjson::StackBuilder<32> builder;  // Space for up to 32 node pointers on the stack
 * entity.inject_into(buffer, builder);
 * @endcode
 */
template <std::size_t MaxSize>
class StackBuilder : public Builder {
  std::array<NodeBase*, MaxSize> array_;

 public:
  /// @brief Constructs an empty `StackBuilder` backed by its internal stack array.
  StackBuilder() : Builder(array_) {}
};

}  // namespace sjson