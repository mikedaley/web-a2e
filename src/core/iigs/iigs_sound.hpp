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
 * IIgsSound - the Ensoniq: 64KB of RAM, a window onto it, and 32 oscillators
 *
 * The Ensoniq 5503 has its own 64KB of RAM that the 65816 cannot address. It
 * reaches it a byte at a time through four registers, which is the whole of
 * the interface: an address pointer, a data port, and a control byte saying
 * whether the port is talking to the RAM or to the chip's own registers, and
 * whether the pointer moves on afterwards.
 *
 * **The oscillators are the synthesiser.** Each one walks a pointer through
 * the sound RAM at a rate its own frequency register sets, reads a byte,
 * scales it by its volume, and adds the result to one of sixteen output
 * channels. A byte of zero is silence and also a *stop*: the chip halts an
 * oscillator that reads one, which is how a sample knows where it ends.
 *
 * Thirty-two of them run at 26,320 samples a second between them — the chip
 * divides its clock among however many are enabled — so the machine's music is
 * whatever the RAM holds and the registers say about it.
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

  /** A DOC register, as the chip sees it. */
  uint8_t docRegister(uint8_t index) const { return doc_[index]; }

  // ===== The synthesiser =====

  /**
   * Run the oscillators forward and mix what they produce.
   *
   * @param buffer  interleaved stereo, filled with what the chip is playing
   * @param frames  how many stereo frames to produce
   * @param rate    the host's sample rate
   */
  void generateSamples(float *buffer, int frames, int rate);

  /** How many oscillators are enabled, which is what sets the chip's rate. */
  int activeOscillators() const;

  /** One oscillator's state, for tests and for looking. */
  struct Oscillator {
    uint16_t frequency = 0;   // How fast the pointer walks
    uint8_t volume = 0;       // 0-255, scaling what it reads
    uint8_t waveTablePointer = 0; // The high byte of where it reads from
    uint8_t control = 0;      // Halt, mode, channel and interrupt enable
    uint8_t tableSize = 0;    // How much of the RAM it walks before wrapping
    uint32_t accumulator = 0; // Where it has got to, in fractional steps
  };

  Oscillator oscillator(int index) const;
  bool oscillatorHalted(int index) const;

  uint16_t address() const { return address_; }
  bool addressesRam() const { return (control_ & CONTROL_RAM) != 0; }
  bool autoIncrements() const { return (control_ & CONTROL_AUTO_INCREMENT) != 0; }
  uint8_t volume() const { return control_ & CONTROL_VOLUME_MASK; }

  // A DOC register's address space: 32 of each kind, one after another.
  static constexpr uint8_t DOC_FREQUENCY_LOW = 0x00;
  static constexpr uint8_t DOC_FREQUENCY_HIGH = 0x20;
  static constexpr uint8_t DOC_VOLUME = 0x40;
  static constexpr uint8_t DOC_DATA = 0x60;
  static constexpr uint8_t DOC_WAVE_POINTER = 0x80;
  static constexpr uint8_t DOC_CONTROL = 0xA0;
  static constexpr uint8_t DOC_WAVE_SIZE = 0xC0;
  static constexpr uint8_t DOC_OSCILLATOR_ENABLE = 0xE1;

  // Control register bits.
  static constexpr uint8_t OSC_HALT = 0x01;
  static constexpr uint8_t OSC_MODE_MASK = 0x06;
  static constexpr uint8_t OSC_MODE_FREE_RUN = 0x00;
  static constexpr uint8_t OSC_MODE_ONE_SHOT = 0x02;
  static constexpr uint8_t OSC_MODE_SYNC = 0x04;
  static constexpr uint8_t OSC_MODE_SWAP = 0x06;
  static constexpr uint8_t OSC_INTERRUPT_ENABLE = 0x08;
  static constexpr uint8_t OSC_CHANNEL_MASK = 0xF0;

  static constexpr uint8_t CONTROL_BUSY = 0x80;
  static constexpr uint8_t CONTROL_AUTO_INCREMENT = 0x20;
  static constexpr uint8_t CONTROL_RAM = 0x40;
  static constexpr uint8_t CONTROL_VOLUME_MASK = 0x0F;

private:
  void advance();

  // Where each oscillator has got to, in the same fixed-point the chip uses:
  // the frequency register is added to an accumulator and the top bits of it
  // are the address. Keeping the fraction is what makes a pitch a pitch rather
  // than a staircase.
  std::array<uint32_t, DOC_OSCILLATOR_COUNT> accumulator_{};

  std::array<uint8_t, SOUND_RAM_SIZE> ram_{};
  std::array<uint8_t, 256> doc_{};
  uint16_t address_ = 0;
  uint8_t control_ = 0;
  uint8_t latch_ = 0; // What the next read will return
};

} // namespace a2e::iigs
