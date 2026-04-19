/*
 * noslot_clock.cpp - DS1215 No-Slot Clock emulation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "noslot_clock.hpp"
#include <ctime>
#include <chrono>

namespace a2e {

NoSlotClock::NoSlotClock() = default;

uint8_t NoSlotClock::interceptRead(uint16_t address, uint8_t romValue) {
  if (!enabled_) return romValue;

  uint8_t addressBit = address & 1;

  switch (state_) {
    case State::IDLE:
      // First bit of pattern must match to start recognition
      if (addressBit == (RECOGNITION_PATTERN & 1)) {
        state_ = State::PATTERN_MATCHING;
        bitIndex_ = 1;  // Bit 0 already matched
      }
      return romValue;

    case State::PATTERN_MATCHING: {
      uint8_t expectedBit = (RECOGNITION_PATTERN >> bitIndex_) & 1;
      if (addressBit == expectedBit) {
        bitIndex_++;
        if (bitIndex_ >= 64) {
          // Full pattern matched - switch to clock read mode
          state_ = State::CLOCK_READ;
          bitIndex_ = 0;
          loadCurrentTime();
        }
      } else {
        // Mismatch - reset
        state_ = State::IDLE;
        bitIndex_ = 0;
      }
      return romValue;
    }

    case State::CLOCK_READ: {
      // Replace bit 0 of romValue with clock data bit
      uint8_t clockBit = (clockData_ >> bitIndex_) & 1;
      uint8_t result = (romValue & 0xFE) | clockBit;
      bitIndex_++;
      if (bitIndex_ >= 64) {
        state_ = State::IDLE;
        bitIndex_ = 0;
      }
      return result;
    }
  }

  return romValue;
}

void NoSlotClock::interceptWrite(uint16_t /*address*/) {
  if (!enabled_) return;
  // Any write to the NSC region resets pattern matching
  state_ = State::IDLE;
  bitIndex_ = 0;
}

void NoSlotClock::reset() {
  state_ = State::IDLE;
  bitIndex_ = 0;
  clockData_ = 0;
}

void NoSlotClock::loadCurrentTime() {
  auto now = std::chrono::system_clock::now();
  time_t nowTime = std::chrono::system_clock::to_time_t(now);
  struct tm* t = localtime(&nowTime);

  // Calculate hundredths of a second from sub-second component
  auto sinceEpoch = now.time_since_epoch();
  auto wholeSeconds = std::chrono::duration_cast<std::chrono::seconds>(sinceEpoch);
  auto hundredths = std::chrono::duration_cast<std::chrono::milliseconds>(
      sinceEpoch - wholeSeconds).count() / 10;

  // Pack BCD time into 64 bits, LSB first:
  // Byte 0: hundredths (0-99)
  // Byte 1: seconds
  // Byte 2: minutes
  // Byte 3: hours
  // Byte 4: day of week (1=Sunday)
  // Byte 5: day of month
  // Byte 6: month
  // Byte 7: year (00-99)

  auto toBCD = [](int val) -> uint8_t {
    return static_cast<uint8_t>(((val / 10) << 4) | (val % 10));
  };

  uint8_t bytes[8];
  bytes[0] = toBCD(static_cast<int>(hundredths)); // hundredths (0-99)
  bytes[1] = toBCD(t->tm_sec);              // seconds
  bytes[2] = toBCD(t->tm_min);              // minutes
  bytes[3] = toBCD(t->tm_hour);             // hours
  bytes[4] = toBCD(t->tm_wday + 1);         // day of week (1-7)
  bytes[5] = toBCD(t->tm_mday);             // day of month
  bytes[6] = toBCD(t->tm_mon + 1);          // month (1-12)
  bytes[7] = toBCD(t->tm_year % 100);       // year (00-99)

  clockData_ = 0;
  for (int i = 0; i < 8; i++) {
    clockData_ |= static_cast<uint64_t>(bytes[i]) << (i * 8);
  }
}

void NoSlotClock::serialize(std::vector<uint8_t>& buf) const {
  buf.push_back(enabled_ ? 1 : 0);
  buf.push_back(static_cast<uint8_t>(state_));
  buf.push_back(static_cast<uint8_t>(bitIndex_));
}

bool NoSlotClock::deserialize(const uint8_t* data, size_t size, size_t& offset) {
  if (offset + 3 > size) return false;
  enabled_ = data[offset++] != 0;
  state_ = static_cast<State>(data[offset++]);
  bitIndex_ = data[offset++];
  return true;
}

} // namespace a2e
