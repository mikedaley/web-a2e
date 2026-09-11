/*
 * iigs_sound.hpp - The Ensoniq's RAM and the window the CPU reaches it through
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
 * IIgsSound - the sound GLU: 64KB of RAM, a window, and 32 oscillators
 *
 * The Ensoniq 5503 has its own 64KB of RAM that the 65816 cannot address. It
 * reaches it a byte at a time through four registers, which is the whole of
 * the interface: an address pointer, a data port, and a control byte saying
 * whether the port is talking to the RAM or to the chip's own registers, and
 * whether the pointer moves on afterwards.
 *
 * **The oscillators are not here.** What is modelled is the RAM, the window
 * and the registers — enough for the firmware's power-on test to write its
 * patterns and read them back, which is where a IIgs stops if nothing answers
 * at $C03C. The synthesiser is its own piece of work and will be built on top
 * of this, because this is the part of it the rest of the machine can see.
 *
 * The read behaviour is the part worth knowing: a read of the data port
 * returns the byte the chip fetched *last* time and then fetches the next, so
 * after setting the address a program reads once to prime the window and takes
 * the answer from the second read. Getting that wrong looks like memory that
 * is off by one, which is exactly what the self-test is checking for.
 */
class IIgsSound {
public:
  IIgsSound() { reset(); }

  void reset();

  // ===== The window, at $C03C-$C03F =====

  uint8_t readControl() const { return control_; }
  void writeControl(uint8_t value) { control_ = value; }

  uint8_t readData();
  void writeData(uint8_t value);

  uint8_t readAddressLow() const { return static_cast<uint8_t>(address_); }
  uint8_t readAddressHigh() const { return static_cast<uint8_t>(address_ >> 8); }
  void writeAddressLow(uint8_t value);
  void writeAddressHigh(uint8_t value);

  // ===== What is behind it =====

  uint8_t soundRam(uint16_t address) const { return ram_[address]; }
  void setSoundRam(uint16_t address, uint8_t value) { ram_[address] = value; }

  /** A DOC register, as the chip sees it. Nothing reads these yet. */
  uint8_t docRegister(uint8_t index) const { return doc_[index]; }

  uint16_t address() const { return address_; }
  bool addressesRam() const { return (control_ & CONTROL_RAM) != 0; }
  bool autoIncrements() const { return (control_ & CONTROL_AUTO_INCREMENT) != 0; }
  uint8_t volume() const { return control_ & CONTROL_VOLUME_MASK; }

  static constexpr uint8_t CONTROL_BUSY = 0x80;
  static constexpr uint8_t CONTROL_AUTO_INCREMENT = 0x20;
  static constexpr uint8_t CONTROL_RAM = 0x40;
  static constexpr uint8_t CONTROL_VOLUME_MASK = 0x0F;

private:
  void advance();

  std::array<uint8_t, SOUND_RAM_SIZE> ram_{};
  std::array<uint8_t, 256> doc_{};
  uint16_t address_ = 0;
  uint8_t control_ = 0;
  uint8_t latch_ = 0; // What the next read will return
};

} // namespace a2e::iigs
