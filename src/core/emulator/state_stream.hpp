/*
 * state_stream.hpp - Reading and writing a save state's bytes
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace a2e {

/**
 * StateWriter - appends little-endian values to a save state
 *
 * Every machine writes its state through one of these so that the layout
 * rules are in one place: integers are little-endian, a string or a blob is
 * its length and then its bytes, and a bool is one byte. A device that can
 * serialize itself takes a writer rather than a buffer and a size, because
 * then it cannot get its own length wrong.
 */
class StateWriter {
public:
  explicit StateWriter(std::vector<uint8_t> &out) : out_(out) {}

  void u8(uint8_t v) { out_.push_back(v); }
  void boolean(bool v) { out_.push_back(v ? 1 : 0); }
  void u16(uint16_t v) {
    out_.push_back(v & 0xFF);
    out_.push_back((v >> 8) & 0xFF);
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; i++) out_.push_back((v >> (i * 8)) & 0xFF);
  }
  void u64(uint64_t v) {
    for (int i = 0; i < 8; i++) out_.push_back((v >> (i * 8)) & 0xFF);
  }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void f64(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    u64(bits);
  }
  void f32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    u32(bits);
  }

  /** Raw bytes with no length in front of them: the caller knows how many. */
  void bytes(const uint8_t *data, size_t size) {
    if (size) out_.insert(out_.end(), data, data + size);
  }

  /** A length and then the bytes. */
  void blob(const uint8_t *data, size_t size) {
    u32(static_cast<uint32_t>(size));
    bytes(data, size);
  }
  void blob(const std::vector<uint8_t> &data) { blob(data.data(), data.size()); }
  void string(const std::string &s) {
    blob(reinterpret_cast<const uint8_t *>(s.data()), s.size());
  }

  size_t size() const { return out_.size(); }

private:
  std::vector<uint8_t> &out_;
};

/**
 * StateReader - the other side of StateWriter
 *
 * Reads never run past the end: a read that would sets `failed()` and returns
 * zero, and the caller checks once at the end rather than after every field.
 * A truncated state therefore restores as far as it can and then reports the
 * failure, and a garbage one fails at the first length that does not fit.
 */
class StateReader {
public:
  StateReader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

  bool ok() const { return !failed_; }
  bool failed() const { return failed_; }
  bool atEnd() const { return offset_ >= size_; }
  size_t offset() const { return offset_; }
  size_t remaining() const { return size_ - offset_; }

  uint8_t u8() {
    if (!need(1)) return 0;
    return data_[offset_++];
  }
  bool boolean() { return u8() != 0; }
  uint16_t u16() {
    if (!need(2)) return 0;
    uint16_t v = data_[offset_] | (data_[offset_ + 1] << 8);
    offset_ += 2;
    return v;
  }
  uint32_t u32() {
    if (!need(4)) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(data_[offset_ + i]) << (i * 8);
    offset_ += 4;
    return v;
  }
  uint64_t u64() {
    if (!need(8)) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(data_[offset_ + i]) << (i * 8);
    offset_ += 8;
    return v;
  }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  double f64() {
    uint64_t bits = u64();
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
  }
  float f32() {
    uint32_t bits = u32();
    float v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
  }

  /** Raw bytes the caller sized; null if they are not there. */
  const uint8_t *bytes(size_t size) {
    if (!need(size)) return nullptr;
    const uint8_t *p = data_ + offset_;
    offset_ += size;
    return p;
  }

  /** A length and then the bytes; `size` comes back as the length. */
  const uint8_t *blob(size_t &size) {
    size = u32();
    if (failed_) return nullptr;
    const uint8_t *p = bytes(size);
    if (!p) size = 0;
    return p;
  }
  std::string string() {
    size_t size = 0;
    const uint8_t *p = blob(size);
    return p ? std::string(reinterpret_cast<const char *>(p), size) : std::string();
  }

  /** Skip bytes the reader does not want. */
  void skip(size_t size) { (void)bytes(size); }

private:
  bool need(size_t n) {
    if (failed_ || n > size_ - offset_) {
      failed_ = true;
      return false;
    }
    return true;
  }

  const uint8_t *data_;
  size_t size_;
  size_t offset_ = 0;
  bool failed_ = false;
};

} // namespace a2e
