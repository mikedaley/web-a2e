/*
 * iigs_clock.hpp - The battery-backed clock, and the settings beside it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "iigs_spec.hpp"

#include <array>
#include <cstdint>

namespace a2e::iigs {

/**
 * IIgsClock - the clock chip at $C033/$C034, and its 256 bytes of settings
 *
 * The first Apple II that remembers anything. A battery keeps a clock running
 * and 256 bytes of RAM alive, and between them they hold what the Control
 * Panel sets: which slot to boot from, how fast to come up, what the screen
 * colours are, which port the printer is on. The firmware reads them before it
 * does anything else, and a machine whose battery RAM is nonsense spends its
 * startup putting it back to the defaults instead.
 *
 * Two registers, and one of them is a state machine. $C033 is a byte of data
 * going either way; $C034 is control: bit 7 starts a transaction, bit 6 says
 * which direction, and the low four bits are the border colour, which lives
 * here for no better reason than that there was a spare nibble.
 *
 * A transaction is a command byte and then a data byte. The command says read
 * or write, and which of the clock's or the RAM's bytes — and the address is
 * split across two commands for the RAM, because the protocol was designed for
 * a chip with eight bytes and then extended to one with two hundred and fifty
 * six.
 */
class IIgsClock {
public:
  IIgsClock() { reset(); }

  void reset();

  // ===== The two registers =====

  uint8_t readData() const { return data_; }
  void writeData(uint8_t value) { data_ = value; }

  uint8_t readControl() const { return control_; }
  void writeControl(uint8_t value);

  // ===== What is behind them =====

  uint8_t batteryRam(uint8_t address) const { return batteryRam_[address]; }
  void setBatteryRam(uint8_t address, uint8_t value) {
    batteryRam_[address] = value;
  }

  /** Seconds since 1st January 1904, which is how a IIgs counts time. */
  uint32_t seconds() const { return seconds_; }
  void setSeconds(uint32_t seconds) { seconds_ = seconds; }

  /** The border colour, in the bottom four bits of the control register. */
  uint8_t borderColour() const { return control_ & 0x0F; }

  static constexpr uint8_t CONTROL_TRANSACTION = 0x80;
  static constexpr uint8_t CONTROL_READ = 0x40;

private:
  void performTransaction(uint8_t command);

  std::array<uint8_t, BATTERY_RAM_SIZE> batteryRam_{};
  uint32_t seconds_ = 0;

  uint8_t data_ = 0;
  uint8_t control_ = 0;

  // The address being assembled across the three-part sequence, which way the
  // byte is about to go, and how far along we are.
  uint16_t pendingAddress_ = 0;
  bool pendingRead_ = false;
  int sequenceStep_ = 0;
};

} // namespace a2e::iigs
