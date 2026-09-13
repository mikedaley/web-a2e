/*
 * iigs_clock.cpp - The battery-backed clock, and the settings beside it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_clock.hpp"

#include <ctime>

namespace a2e::iigs {

namespace {
// The command bytes, as the firmware actually sends them — which is how these
// were arrived at. The documented descriptions of this chip are for the part
// before it was stretched to 256 bytes of RAM, and guessing from them produced
// a machine that stored each byte at the address of the byte before.
//
// A battery RAM access is three transfers, not one:
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

// A clock register access is one command: four registers holding the seconds
// since the 1st of January 1904, least significant first.
constexpr uint8_t CLOCK_MASK = 0x73;
constexpr uint8_t CLOCK_MATCH = 0x01;

// The twenty bytes the chip had before it was extended, reached the old way:
// z1aaaa01 for the first sixteen and z0010aa1 for the four after them. They
// are the same bytes as the first twenty of the 256.
constexpr uint8_t OLD_RAM_LOW_MASK = 0x43;
constexpr uint8_t OLD_RAM_LOW_MATCH = 0x41;
constexpr uint8_t OLD_RAM_HIGH_MASK = 0x79;
constexpr uint8_t OLD_RAM_HIGH_MATCH = 0x11;

// 1st January 1904 to 1st January 1970, in seconds.
constexpr int64_t SECONDS_1904_TO_1970 = 2082844800LL;
} // namespace

IIgsClock::IIgsClock() : seconds_(hostSecondsSince1904()) { reset(); }

uint32_t IIgsClock::hostSecondsSince1904() {
  // A IIgs keeps local time, not UTC: what its clock shows is what the person
  // in front of it would write down.
  const std::time_t utc = std::time(nullptr);
  const std::tm *local = std::localtime(&utc);
  const int64_t zone = local ? static_cast<int64_t>(local->tm_gmtoff) : 0;
  return static_cast<uint32_t>(static_cast<int64_t>(utc) + zone +
                               SECONDS_1904_TO_1970);
}

void IIgsClock::reset() {
  data_ = 0;
  control_ = 0;
  step_ = Step::Command;
  target_ = Target::None;
  reading_ = false;
  address_ = 0;
  // The battery and its contents survive a reset — that is the entire point of
  // them — so batteryRam_ and the clock are deliberately left alone.
}

void IIgsClock::writeControl(uint8_t value) {
  control_ = value;

  // Dropping the select line ends whatever transaction was in progress. The
  // firmware's driver does it after every one, and a chip that stayed where
  // it was would take the next command byte as the last one's data.
  if ((value & CONTROL_SELECT) == 0) {
    step_ = Step::Command;
    target_ = Target::None;
  }

  if ((value & CONTROL_TRANSACTION) == 0) return;

  const bool toChip = (value & CONTROL_READ) == 0;
  switch (step_) {
  case Step::Command:
    // A read transfer with no command outstanding is the host listening to a
    // chip that has nothing to say; the data register keeps what it had.
    if (toChip) takeCommand(data_);
    break;
  case Step::Address:
    if (toChip) takeAddress(data_);
    break;
  case Step::Data:
    transferData(toChip);
    break;
  }

  // The chip clears the transaction bit when it is done. A real one takes a
  // moment over it and the firmware waits; this one is finished by the time
  // the write returns, which the firmware cannot tell from being very fast.
  control_ &= static_cast<uint8_t>(~CONTROL_TRANSACTION);
}

void IIgsClock::takeCommand(uint8_t command) {
  reading_ = (command & COMMAND_READ) != 0;

  if ((command & RAM_COMMAND_MASK) == RAM_COMMAND_MATCH) {
    address_ = static_cast<uint16_t>((command & 0x07) << 5);
    target_ = Target::BatteryRam;
    step_ = Step::Address;
    return;
  }

  if ((command & CLOCK_MASK) == CLOCK_MATCH) {
    address_ = static_cast<uint16_t>((command >> 2) & 0x03);
    target_ = Target::Seconds;
    step_ = Step::Data;
    return;
  }

  if ((command & OLD_RAM_LOW_MASK) == OLD_RAM_LOW_MATCH) {
    address_ = static_cast<uint16_t>((command >> 2) & 0x0F);
    target_ = Target::BatteryRam;
    step_ = Step::Data;
    return;
  }

  if ((command & OLD_RAM_HIGH_MASK) == OLD_RAM_HIGH_MATCH) {
    address_ = static_cast<uint16_t>(0x10 + ((command >> 1) & 0x03));
    target_ = Target::BatteryRam;
    step_ = Step::Data;
    return;
  }

  // Anything else is a register this chip does not have — the write-protect
  // latch and the test register — which still takes its data byte and does
  // nothing with it, and answers zero if asked.
  target_ = Target::None;
  step_ = Step::Data;
}

void IIgsClock::takeAddress(uint8_t command) {
  address_ = static_cast<uint16_t>(address_ | ((command >> 2) & 0x1F));
  step_ = Step::Data;
}

void IIgsClock::transferData(bool toChip) {
  const uint8_t address = static_cast<uint8_t>(address_);
  if (toChip) {
    switch (target_) {
    case Target::BatteryRam:
      // Through setBatteryRam so the host hears about it: this is the path
      // the Control Panel's settings actually take.
      setBatteryRam(address, data_);
      break;
    case Target::Seconds: {
      const int shift = address * 8;
      seconds_ &= ~(0xFFu << shift);
      seconds_ |= static_cast<uint32_t>(data_) << shift;
      break;
    }
    case Target::None:
      break;
    }
  } else {
    switch (target_) {
    case Target::BatteryRam:
      data_ = batteryRam_[address];
      break;
    case Target::Seconds:
      data_ = static_cast<uint8_t>((seconds_ >> (address * 8)) & 0xFF);
      break;
    case Target::None:
      data_ = 0x00;
      break;
    }
  }
  step_ = Step::Command;
  target_ = Target::None;
}

} // namespace a2e::iigs
