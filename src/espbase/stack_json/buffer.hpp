#pragma once

#include <algorithm>
#include <span>
#include <string_view>

namespace sjson {

/**
 * @brief Abstract output buffer interface.
 *
 * @details All sjson emitters write through this interface. Concrete implementations
 * range from fixed-size stack buffers (`StackBuffer`) and heap/PSRAM-backed dynamic
 * buffers (`DynamicBuffer`) to decorator buffers such as `PrettyBuffer`. Emitters
 * call `write()`, `get_write_span()` / `commit()`, and `head()` / `length()` through
 * this common base so that generation code is fully decoupled from storage strategy.
 *
 * A buffer tracks a single boolean failure flag. Any write that cannot be completed
 * (e.g. a fixed-size buffer has no remaining capacity) calls `fail()`, sets the flag,
 * and returns 0. All subsequent writes are silently ignored until `clear_fail()` is
 * called. This lets emitters proceed without checking every write individually and
 * inspect the result at the end.
 */
class Buffer {
 public:
  constexpr Buffer() = default;
  virtual ~Buffer() = default;

  /**
   * @brief Writes @p str with JSON string escaping applied in-place.
   *
   * @details Special characters (`"`, `\`, and control characters) are
   * expanded to their `\uXXXX` / shorthand escape sequences directly into
   * the buffer's unused tail region. The outer quote characters are **not**
   * emitted — the caller is responsible for them.
   *
   * @param str The raw (unescaped) string to write.
   * @return Number of bytes written (after escaping), or 0 on failure.
   */
  size_t write_escaped(std::string_view str);

  /**
   * @brief Returns a view over the most recent @p written bytes.
   *
   * @details Useful for inspecting what was just emitted without an extra copy.
   * @param written Number of bytes that were just written.
   * @return A `std::string_view` pointing into the buffer.
   */
  std::string_view last_write(size_t written) {
    return std::string_view(head() - written, written);
  }

  /**
   * @brief Writes @p str to the buffer.
   *
   * @param str The bytes to write.
   * @return Number of bytes written, or 0 on failure (which also sets the
   *         failure flag via `fail()`).
   */
  virtual size_t write(std::string_view str) = 0;

  /**
   * @brief Returns a writable span into the buffer's available tail region.
   *
   * @details Used by printf-style formatters that must write directly into
   * contiguous memory (e.g. `vsnprintf`). After writing, call `commit()` with
   * the number of bytes actually used. The default implementation returns an
   * empty span; concrete buffers override this to expose their free region.
   *
   * @return A span over unused, writable bytes. May be empty if the buffer is full.
   */
  virtual std::span<char> get_write_span();

  /**
   * @brief Advances the write cursor by @p count bytes after a direct write.
   *
   * @details Must be called after each `get_write_span()` / direct-write pair
   * with the number of bytes actually consumed. The default implementation is a
   * no-op; concrete buffers override it to bump their internal head pointer.
   *
   * @param count Number of bytes to commit.
   */
  virtual void commit(std::size_t count);

  /**
   * @brief Returns a pointer to the next byte to be written.
   *
   * @return Raw pointer to the write position inside the buffer's storage.
   */
  virtual char* head() = 0;

  /**
   * @brief Returns the number of bytes written so far.
   *
   * @return Byte count of content currently held in the buffer.
   */
  virtual size_t length() const = 0;

  /**
   * @brief Returns `true` if the buffer is in a failed state.
   *
   * @details Once set, the flag persists until `clear_fail()` is called. All
   * virtual write operations on a bad buffer are expected to be no-ops.
   *
   * @return `true` if a previous write failed, `false` otherwise.
   */
  bool bad() const { return bad_; }

  /**
   * @brief Resets the failure flag.
   *
   * @details Allows re-using a buffer after recovering from a write failure
   * (e.g. after calling `reset()` on a `StackBuffer`).
   */
  void clear_fail() { bad_ = false; }

  /**
   * @brief Marks the buffer as failed and returns 0.
   *
   * @details Concrete `write()` overrides call this when they cannot satisfy a
   * write request (e.g. fixed capacity exceeded). After calling `fail()`, `bad()`
   * returns `true` and all further writes should be skipped.
   *
   * @return Always returns 0.
   */
  size_t fail() {
    bad_ = true;
    return 0;
  }

 private:
  bool bad_ = false;
};

/**
 * @brief Fixed-size stack-allocated output buffer.
 *
 * @details Stores up to `MaxSize` characters in an internal `char` array on
 * the stack. No heap allocation occurs. If a write would exceed capacity,
 * the failure flag is set and the write is discarded. After emitting, use
 * `view()` for a `std::string_view` or `c_str()` for a null-terminated C string.
 *
 * @tparam MaxSize Maximum number of characters the buffer can hold (not counting
 *                 the implicit null terminator reserved by `c_str()`).
 *
 * @code
 * sjson::StackBuffer<256> buf;
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("sensor", "temperature"),
 *     sjson::node("value", 42)
 * );
 * doc.emit(buf);
 *
 * if (!buf.bad()) {
 *     Serial.println(buf.c_str());
 *     // Prints: {"sensor":"temperature","value":42}
 * }
 * @endcode
 */
template <std::size_t MaxSize>
class StackBuffer : public Buffer {
  char buffer_[MaxSize + 1] = {};  // +1 so we can null-terminate for c_str()
  std::size_t head_ = 0;

 public:
  constexpr StackBuffer() = default;

  /**
   * @brief Clears the buffer and resets the failure flag.
   *
   * @details Resets the write cursor to the beginning without zeroing the
   * underlying storage, making it safe and cheap to re-use the buffer for a
   * new emission.
   */
  void reset() {
    clear_fail();
    head_ = 0;
  }

  /**
   * @brief Writes @p str to the buffer if capacity permits.
   *
   * @param str The bytes to write.
   * @return Number of bytes written, or 0 (and `bad()` set) if the buffer is full.
   */
  size_t write(std::string_view str) override {
    if (head_ + str.size() > MaxSize) return fail();
    std::copy(str.begin(), str.end(), buffer_ + head_);
    head_ += str.size();
    return str.size();
  }

  /**
   * @brief Returns a read-only view over the written content.
   *
   * @return A `std::string_view` spanning exactly the bytes written so far.
   */
  std::string_view view() const { return {buffer_, head_}; }

  /**
   * @brief Null-terminates and returns the buffer as a C string.
   *
   * @details The null byte is written to the byte immediately after the last
   * written character. This does not change `length()`.
   *
   * @return Pointer to the null-terminated content.
   */
  const char* c_str() {
    buffer_[head_] = '\0';
    return buffer_;
  }

  /// @brief Returns a writable span over the remaining capacity (zero if full).
  std::span<char> get_write_span() override {
    if (head_ >= MaxSize) return {};
    return std::span<char>(buffer_ + head_, MaxSize - head_);
  }

  /// @brief Advances the write cursor by @p count bytes after a direct write.
  void commit(std::size_t count) override { head_ += count; }

  /// @brief Returns a pointer to the next byte to be written.
  char* head() override { return buffer_ + head_; }

  /// @brief Returns the number of bytes written.
  size_t length() const override { return head_; }
};

}  // namespace sjson
