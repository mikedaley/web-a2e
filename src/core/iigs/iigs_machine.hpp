/*
 * iigs_machine.hpp - An Apple IIgs, assembled from its parts
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "iigs_memory.hpp"
#include "iigs_spec.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace a2e {
class CPU65816;
class Video;
} // namespace a2e

namespace a2e::iigs {

/**
 * IIgsMachine - the coordinator, as Emulator is for the 8-bit machines
 *
 * Emulator is the Apple II family's: an MMU, a Video, an Audio and a CPU6502,
 * chosen for it by a profile. A IIgs is not built from those parts, so it is
 * not built by that class — it has a 65816 on a 24-bit bus and a memory
 * controller with a //e inside it, and this is where they are wired together.
 *
 * What it borrows is the video. A IIgs's //e-mode picture is drawn by the Mega
 * II, and the Mega II is a //e, so `Video` reads the same memory through the
 * same MMU it always has. Super Hi-Res is a second display system and will be
 * a second class alongside it; nothing about it is here yet.
 *
 * **The machine has two clocks and the video counts in the slower one.** The
 * 65816 runs at 2.8MHz except when it is talking to the Mega II, and the video
 * is on the Mega II's side at 1.023MHz. So the CPU's cycles are converted as
 * they are spent, and what the video is handed is the slow-side count — which
 * is what makes a frame take a frame's worth of time whichever speed the
 * machine is running at.
 */
class IIgsMachine {
public:
  explicit IIgsMachine(size_t fastRamSize = FAST_RAM_SIZE_ROM01);
  ~IIgsMachine();

  IIgsMachine(const IIgsMachine &) = delete;
  IIgsMachine &operator=(const IIgsMachine &) = delete;

  /**
   * Load the ROM and start the machine.
   *
   * Without a ROM there is nothing to run: the reset vector reads as zero and
   * the CPU goes to $0000. `hasROM()` is how a caller finds out before trying.
   */
  void init(const uint8_t *rom, size_t romSize);
  bool hasROM() const { return memory_->hasROM(); }

  /** Power-on reset: the CPU comes up in emulation mode, as every 65816 does. */
  void reset();

  /** Run for this many cycles of the machine's *slow* side. */
  void runCycles(int slowCycles);

  /** Run one instruction, whatever it costs. Returns the cycles it took. */
  int step();

  // ===== The parts =====

  CPU65816 &cpu() { return *cpu_; }
  IIgsMemory &memory() { return *memory_; }
  Video &video() { return *video_; }

  /** Slow-side cycles since reset: the clock the video is counted in. */
  uint64_t slowCycles() const { return slowCycles_; }

  /** What is on the text screen, for tests and for looking. */
  std::string screenText() const;

private:
  // How many slow cycles an instruction costs. A 65816 access to the Mega II
  // side runs at the slow clock and everything else at the fast one, so the
  // honest answer needs to know which addresses an instruction touched. Until
  // the memory can say, this converts by the ratio, which is right on average
  // and wrong in the small — and is enough for firmware that is waiting on the
  // video rather than racing it.
  double slowCyclesFor(int cpuCycles) const;

  std::unique_ptr<IIgsMemory> memory_;
  std::unique_ptr<CPU65816> cpu_;
  std::unique_ptr<Video> video_;

  uint64_t slowCycles_ = 0;
  double slowCycleRemainder_ = 0.0;
};

} // namespace a2e::iigs
