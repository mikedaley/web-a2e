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
 * Two registers, and one of them is a serial line. $C033 is a byte of data
 * going either way; $C034 is control: bit 7 starts a transfer, bit 6 says
 * which way the byte goes — clear, and the chip takes what is in the data
 * register; set, and it puts a byte there — bit 5 holds the chip selected
 * across the transfers of one transaction, and the low four bits are the
 * border colour, which lives here for no better reason than that there was a
 * spare nibble.
 *
 * A transaction is a command byte and then a data byte, each its own transfer.
 * The command says read or write, and which of the clock's or the RAM's bytes;
 * for the 256 bytes of RAM the address is split across two command bytes,
 * because the protocol was designed for a chip with twenty bytes and then
 * extended to one with two hundred and fifty six.
 *
 * The data byte of a read is the chip's to supply, and it supplies it on the
 * read-direction transfer, not when it sees the command. The firmware's driver
 * — the same routine in the ROM and in the IIgs Diagnostic — stores whatever
 * it has in hand to $C033 before every transfer, the read of the data byte
 * included, so a chip that answered early had its answer overwritten. The
 * Diagnostic's Clock RAM Test read every clock byte back as the junk it had
 * just stored, retried 256 times, and gave up into the monitor.
 */
class IIgsClock {
public:
  IIgsClock();

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

  /**
   * Seconds since 1st January 1904, local time, which is how a IIgs counts.
   * Set from the host's clock when the chip is made, and counted on from
   * there by tick(), once per second of the machine's own time — the same
   * second that raises the VGC's one-second interrupt, because on the real
   * machine that interrupt is this chip's tick. Counting emulated time rather
   * than reading the host's is what lets a program set the clock and watch it
   * move: the Diagnostic writes $FFFFFFFF and waits for the roll-over, and a
   * machine running faster than real time would otherwise wait in vain.
   */
  uint32_t seconds() const { return seconds_; }
  void setSeconds(uint32_t seconds) { seconds_ = seconds; }
  void tick() { seconds_++; }

  /** The border colour, in the bottom four bits of the control register. */
  uint8_t borderColour() const { return control_ & 0x0F; }

  static constexpr uint8_t CONTROL_TRANSACTION = 0x80;
  /** Set for a byte coming out of the chip, clear for one going into it. */
  static constexpr uint8_t CONTROL_READ = 0x40;
  /** Held set across the transfers of a transaction; dropped to end one. */
  static constexpr uint8_t CONTROL_SELECT = 0x20;

  /** Seconds since 1904 as the host's clock counts them now. */
  static uint32_t hostSecondsSince1904();

private:
  enum class Target : uint8_t { None, BatteryRam, Seconds };

  void takeCommand(uint8_t command);
  void takeAddress(uint8_t command);
  void transferData(bool toChip);

  std::array<uint8_t, BATTERY_RAM_SIZE> batteryRam_{};
  uint32_t seconds_ = 0;

  uint8_t data_ = 0;
  uint8_t control_ = 0;

  // Where the transaction has got to: waiting for a command, for the second
  // half of a RAM address, or for the data byte — and what that byte is for.
  enum class Step : uint8_t { Command, Address, Data };
  Step step_ = Step::Command;
  Target target_ = Target::None;
  bool reading_ = false;
  uint16_t address_ = 0; // RAM address, or which byte of the seconds
};

} // namespace a2e::iigs
