/*
 * iigs_sound.hpp - The Ensoniq 5503: its RAM, the window onto it, and the
 * thirty-two oscillators
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "iigs_spec.hpp"
#include "../emulator/state_stream.hpp"

#include <array>
#include <cstdint>
#include <vector>

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
 * oscillator that reads one, in every mode, which is how a sample knows where
 * it ends. Reaching the end of its table is the other way a sound ends — a
 * free-running oscillator wraps, a one-shot halts, a swapped one halts and
 * starts its partner, a synced one restarts the oscillator below it.
 *
 * **It runs on the machine's clock, not the host's.** The chip scans its
 * enabled oscillators in turn, one per eight ticks of the 7.16MHz clock with
 * two spare slots per scan, so thirty-two of them produce a sample every 544
 * ticks — 26,320 a second — and eight of them one every 160. `advance()` is
 * fed the slow clock as the machine runs and produces one frame per scan into
 * a ring; `generateSamples()` resamples that ring to whatever rate the host
 * wants. Running the chip only when the host asked for a buffer put every
 * oscillator interrupt tens of milliseconds late, and a program refilling a
 * buffer from that interrupt never keeps up.
 *
 * **It interrupts.** An oscillator whose control byte has the interrupt bit
 * raises one when it halts, and the sound tools run on exactly that: a sample
 * is played by a swapped pair, each half refilled from the interrupt the other
 * half's end raises. Register $E0 says which oscillator, active low, and
 * reading it clears that one and re-raises the line if another is waiting.
 *
 * The data port is the part worth knowing: a read returns the byte the chip
 * fetched *last* time and then fetches the one at the current address, so a
 * program sets the address, reads once to prime the window, and takes its
 * answer from the read after that. Getting that wrong looks like memory that
 * is off by one, which is exactly what the self-test is checking for.
 */
class IIgsSound {
public:
  IIgsSound() { reset(); }

  void reset();

  // ===== The window, at $C03C-$C03F =====

  /**
   * $C03C: bit 7 busy (never, here), 6 RAM/registers, 5 auto-increment, 3-0
   * volume.
   *
   * The volume reads back as it was written. This used to force the nibble to
   * 15, which is not what the register does and is not harmless: the Control
   * Panel's volume setting and the toolbox's SetSoundVolume both change the
   * volume by reading the register, altering the nibble and writing it back,
   * so every one of them was reading a machine that claimed to be at full
   * volume whatever it was actually set to.
   */
  uint8_t readControl() const { return control_; }
  void writeControl(uint8_t value);

  uint8_t readData();
  void writeData(uint8_t value);

  uint8_t readAddressLow() const { return static_cast<uint8_t>(address_); }
  uint8_t readAddressHigh() const { return static_cast<uint8_t>(address_ >> 8); }
  void writeAddressLow(uint8_t value);
  void writeAddressHigh(uint8_t value);

  // ===== What is behind it =====

  uint8_t soundRam(uint16_t address) const { return ram_[address]; }
  void setSoundRam(uint16_t address, uint8_t value) { ram_[address] = value; }

  /** A DOC register, as the chip would answer for it. */
  uint8_t docRegister(uint8_t index) const;

  // ===== The synthesiser =====

  /**
   * The machine's clock has moved: run the chip for that long, producing a
   * frame for every scan of the oscillators it completes.
   */
  void advance(uint32_t slowCycles);

  /**
   * Resample what the chip has produced to the host's rate into an
   * interleaved stereo buffer. The step is the chip's rate over the host's,
   * nudged by up to half a percent to keep the backlog near a few
   * milliseconds — the host asks for exactly the time the machine ran, so the
   * two only ever drift by rounding.
   *
   * @param buffer  interleaved stereo, filled with what the chip is playing
   * @param frames  how many stereo frames to produce
   * @param rate    the host's sample rate
   */
  void generateSamples(float *buffer, int frames, int rate);

  /** How many oscillators are enabled, which is what sets the chip's rate. */
  int activeOscillators() const { return oscillatorsEnabled_; }

  /** Frames a second at the current number of oscillators. */
  double sampleRate() const;

  /** Whether an oscillator is holding the interrupt line down. */
  bool interruptPending() const;

  /** One oscillator's state, for tests and for looking. */
  struct Oscillator {
    uint16_t frequency = 0;   // How fast the pointer walks
    uint8_t volume = 0;       // 0-255, scaling what it reads
    uint8_t waveTablePointer = 0; // The high byte of where it reads from
    uint8_t control = 0;      // Halt, mode, channel and interrupt enable
    uint8_t tableSize = 0;    // Bank, size and resolution, as written to $C0
    uint32_t accumulator = 0; // Where it has got to, in fractional steps
    uint8_t data = 0x80;      // The last byte it read
    bool interruptPending = false;
  };

  Oscillator oscillator(int index) const;
  bool oscillatorHalted(int index) const;

  uint16_t address() const { return address_; }
  bool addressesRam() const { return (control_ & CONTROL_RAM) != 0; }
  bool autoIncrements() const { return (control_ & CONTROL_AUTO_INCREMENT) != 0; }
  uint8_t volume() const { return control_ & CONTROL_VOLUME_MASK; }

  /**
   * The chip in a save state: its RAM, every oscillator and the registers.
   * The output ring is not — it is the host's backlog, and a restored chip
   * starts one afresh.
   */
  void serialize(StateWriter &w) const;
  void deserialize(StateReader &r);

  // A DOC register's address space: 32 of each kind, one after another.
  static constexpr uint8_t DOC_FREQUENCY_LOW = 0x00;
  static constexpr uint8_t DOC_FREQUENCY_HIGH = 0x20;
  static constexpr uint8_t DOC_VOLUME = 0x40;
  static constexpr uint8_t DOC_DATA = 0x60;
  static constexpr uint8_t DOC_WAVE_POINTER = 0x80;
  static constexpr uint8_t DOC_CONTROL = 0xA0;
  static constexpr uint8_t DOC_WAVE_SIZE = 0xC0;
  static constexpr uint8_t DOC_INTERRUPT = 0xE0; // the oscillator interrupt register
  static constexpr uint8_t DOC_OSCILLATOR_ENABLE = 0xE1;
  static constexpr uint8_t DOC_ADC = 0xE2;

  // Control register bits.
  static constexpr uint8_t OSC_HALT = 0x01;
  static constexpr uint8_t OSC_MODE_MASK = 0x06;
  static constexpr uint8_t OSC_MODE_FREE_RUN = 0x00;
  static constexpr uint8_t OSC_MODE_ONE_SHOT = 0x02;
  static constexpr uint8_t OSC_MODE_SYNC = 0x04;
  static constexpr uint8_t OSC_MODE_SWAP = 0x06;
  static constexpr uint8_t OSC_INTERRUPT_ENABLE = 0x08;
  static constexpr uint8_t OSC_CHANNEL_MASK = 0xF0;

  // Wave size register bits.
  static constexpr uint8_t SIZE_BANK = 0x40;
  static constexpr uint8_t SIZE_TABLE_MASK = 0x38;
  static constexpr uint8_t SIZE_RESOLUTION_MASK = 0x07;

  static constexpr uint8_t CONTROL_BUSY = 0x80;
  static constexpr uint8_t CONTROL_RAM = 0x40;
  static constexpr uint8_t CONTROL_AUTO_INCREMENT = 0x20;
  static constexpr uint8_t CONTROL_VOLUME_MASK = 0x0F;

  /** The 7.16MHz clock the chip divides, in ticks per second. */
  static constexpr double DOC_CLOCK_HZ = 7159090.0;

private:
  struct Voice {
    uint16_t frequency = 0;
    uint8_t volume = 0;
    uint32_t wavePointer = 0; // Bits 8-15 from $80, bit 16 from $C0's bank bit
    uint8_t control = 0;
    uint8_t sizeCode = 0;     // 0-7: 256 bytes to 32K
    uint8_t resolution = 0;   // 0-7
    uint32_t accumulator = 0;
    uint8_t data = 0x80;
    bool interruptPending = false;
  };

  uint8_t readDocRegister(uint8_t reg);
  void writeDocRegister(uint8_t reg, uint8_t value);
  void advancePointer();

  /** One scan of the enabled oscillators: one frame into the ring. */
  void scan();

  /**
   * An oscillator has reached the end of its table (fromEnd) or read a zero
   * byte (!fromEnd): loop, halt, hand over to a partner, and interrupt, as
   * its mode says.
   */
  void haltOscillator(int index, bool fromEnd, uint8_t newControl);

  uint32_t tableLength(const Voice &v) const { return 256u << v.sizeCode; }
  int resolutionShift(const Voice &v) const {
    return 9 + v.resolution - v.sizeCode;
  }

  std::array<Voice, DOC_OSCILLATOR_COUNT> voices_{};
  int oscillatorsEnabled_ = 1;
  uint8_t enableRegister_ = 0;
  uint8_t interruptRegister_ = 0xFF;

  std::array<uint8_t, SOUND_RAM_SIZE> ram_{};
  uint16_t address_ = 0;
  uint8_t control_ = 0;
  uint8_t latch_ = 0; // What the next read of the data port returns

  // The chip's clock, carried between advances, in ticks of DOC_CLOCK_HZ.
  double ticks_ = 0.0;

  // What the oscillators have produced and the host has not yet taken: stereo
  // frames at the chip's own rate. Sized for the fastest the chip can run for
  // longer than any host buffer.
  static constexpr size_t RING_FRAMES = 32768;
  std::vector<float> ring_;
  uint64_t produced_ = 0;    // Frames ever written
  double consumed_ = 0.0;    // Frames ever read, with the fraction
  float lastLeft_ = 0.0f;    // What the last host frame was, for a dry ring
  float lastRight_ = 0.0f;
};

} // namespace a2e::iigs
