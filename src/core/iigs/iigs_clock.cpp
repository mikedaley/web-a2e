/*
 * iigs_clock.cpp - The battery-backed clock, and the settings beside it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_clock.hpp"

namespace a2e::iigs {

namespace {
// The command bytes, as the firmware actually sends them — which is how these
// were arrived at. The documented descriptions of this chip are for the part
// before it was stretched to 256 bytes of RAM, and guessing from them produced
// a machine that stored each byte at the address of the byte before.
//
// A battery RAM access is three transactions, not one:
//
//   1. 0 0 1 1 1 a a a   with bit 7 set to read: the top three address bits
//   2. 0 a a a a a 0 0   the low five
//   3. the byte itself, out of the data register or into it
//
// Eight first bytes and thirty-two second bytes is 256 addresses, and the
// firmware's power-on test walks all of them, which is what makes the encoding
// readable straight off a trace.
constexpr uint8_t COMMAND_READ = 0x80;

constexpr uint8_t RAM_COMMAND_MASK = 0x78;
constexpr uint8_t RAM_COMMAND_MATCH = 0x38;

// A clock register access is one transaction: four registers holding the
// seconds since the 1st of January 1904, least significant first.
constexpr uint8_t CLOCK_MASK = 0x73;
constexpr uint8_t CLOCK_MATCH = 0x01;
} // namespace

void IIgsClock::reset() {
  data_ = 0;
  control_ = 0;
  pendingAddress_ = 0;
  sequenceStep_ = 0;
  // The battery and its contents survive a reset — that is the entire point of
  // them — so batteryRam_ and the clock are deliberately left alone.
}

void IIgsClock::writeControl(uint8_t value) {
  control_ = value;
  if ((value & CONTROL_TRANSACTION) == 0) return;

  performTransaction(data_);

  // The chip clears the transaction bit when it is done. A real one takes a
  // moment over it and the firmware waits; this one is finished by the time
  // the write returns, which the firmware cannot tell from being very fast.
  control_ &= static_cast<uint8_t>(~CONTROL_TRANSACTION);
}

void IIgsClock::performTransaction(uint8_t command) {
  switch (sequenceStep_) {
  case 1:
    // The second of three: the low five bits of the address.
    pendingAddress_ =
        static_cast<uint16_t>(pendingAddress_ | ((command >> 2) & 0x1F));
    sequenceStep_ = 2;
    return;

  case 2: {
    // The third: the byte. On a write the firmware has already put it in the
    // data register; on a read it is about to take it out of there.
    const uint8_t address = static_cast<uint8_t>(pendingAddress_);
    if (pendingRead_) {
      data_ = batteryRam_[address];
    } else {
      batteryRam_[address] = data_;
    }
    sequenceStep_ = 0;
    return;
  }

  default:
    break;
  }

  const bool reading = (command & COMMAND_READ) != 0;

  if ((command & RAM_COMMAND_MASK) == RAM_COMMAND_MATCH) {
    pendingAddress_ = static_cast<uint16_t>((command & 0x07) << 5);
    pendingRead_ = reading;
    sequenceStep_ = 1;
    return;
  }

  if ((command & CLOCK_MASK) == CLOCK_MATCH) {
    const int shift = ((command >> 2) & 0x03) * 8;
    if (reading) {
      data_ = static_cast<uint8_t>((seconds_ >> shift) & 0xFF);
    } else {
      seconds_ &= ~(0xFFu << shift);
      seconds_ |= static_cast<uint32_t>(data_) << shift;
    }
    return;
  }

  // Anything else is a register this chip does not have — the write-protect
  // latch and the test registers — and answering zero is what an absent
  // register does.
  if (reading) data_ = 0x00;
}

} // namespace a2e::iigs
