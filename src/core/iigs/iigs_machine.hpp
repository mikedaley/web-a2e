/*
 * iigs_machine.hpp - An Apple IIgs, assembled from its parts
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "iigs_memory.hpp"
#include "iigs_spec.hpp"
#include "iigs_video.hpp"

#include <string>

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace a2e {
class CPU65816;
class DiskController;
class Keyboard;
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
   *
   * The character generator is separate and is not optional in practice. A
   * IIgs keeps its font inside the video chip rather than anywhere the CPU can
   * read, so there is nothing in the system ROM to find: the machine is given
   * the //e's set, which is the same font. Without one, every glyph is blank
   * and text comes out as solid bars.
   */
  void init(const uint8_t *rom, size_t romSize,
            const uint8_t *characterRom = nullptr, size_t characterSize = 0);
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

  /** The Mega II's video generator: a //e's, drawing a //e's picture. */
  Video &video() { return *video_; }

  /** The machine's screen, which is that picture or Super Hi-Res. */
  IIgsVideo &screen() { return *screen_; }

  /** Slow-side cycles since reset: the clock the video and the drive run on. */
  uint64_t slowCycles() const { return memory_->slowCycles(); }

  // ===== The drive =====
  //
  // A IIgs's 5.25" port is an IWM, which is the chip a //c has and the same
  // class: what is between it and the disk — two drives, the stepper, the
  // motor, the sequencer — is DiskController, shared with the card a //e takes.
  // It answers at slot 6's addresses, and the boot code that drives it is the
  // machine's own firmware rather than a ROM on a card.

  DiskController &disk() { return *disk_; }
  bool insertDisk(int drive, const uint8_t *data, size_t size,
                  const std::string &filename);
  void ejectDisk(int drive);
  bool hasDisk(int drive) const;

  // ===== Somebody typing =====
  //
  // The same calls Emulator takes, because the host should not have to know
  // which machine it is talking to. What differs is where they land: a //e's
  // keyboard is wired to the machine, and a IIgs's goes through the ADB
  // controller, which fills in the //e's own registers on the Mega II's
  // behalf.

  /** A browser key event, translated and handed to the controller. */
  int handleRawKeyDown(int browserKeycode, bool shift, bool ctrl, bool alt,
                       bool meta, bool capsLock, int keyLocation);
  void handleRawKeyUp(int browserKeycode, bool shift, bool ctrl, bool alt,
                      bool meta, int keyLocation);

  /** An already-translated Apple II key code. */
  void keyDown(int keycode);

  /** Mouse movement and its button, which reach the machine through the ADB. */
  void mouseMove(int dx, int dy);
  void mouseButton(bool pressed);

  /**
   * What is on the text screen, for tests and for looking.
   *
   * The whole 40-column screen, or the rectangle asked for. It comes out of
   * the Mega II's main RAM, in the interleaved layout every Apple II has used,
   * because it is the same chip generating it.
   */
  std::string screenText() const;
  std::string screenText(int startRow, int startColumn, int endRow,
                         int endColumn) const;

  // ===== What the host drives it through =====
  //
  // The emulation is paced by audio: the worker asks for a buffer of samples,
  // and producing them is what runs the machine forward. So these are the
  // calls that matter, and they are deliberately the same shape as Emulator's
  // — the host should not have to know which machine it has.

  /**
   * Run long enough to produce this many samples, and fill them in.
   *
   * The samples are silence: a IIgs's sound is the Ensoniq, and the
   * synthesiser behind that chip's RAM is not written yet. Returning the
   * buffer full of zeroes rather than nothing is what keeps the worker's
   * pacing loop turning at the right rate.
   */
  int generateStereoAudioSamples(float *buffer, int sampleCount);

  /** How many whole frames' worth of samples have been produced since asked. */
  int consumeFrameSamples();

  bool isFrameReady() const;
  void clearFrameReady();

  /**
   * The picture, at the size the machine's profile promises: 640 by 400.
   *
   * Which of the machine's two video systems drew it is $C029's business, and
   * IIgsVideo's.
   */
  const uint8_t *framebuffer();
  size_t framebufferSize() const;

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
  std::unique_ptr<IIgsVideo> screen_;
  std::unique_ptr<Keyboard> keyboard_;
  DiskController *disk_ = nullptr; // Owned by the Mega II's slot

  int samplesGenerated_ = 0;
  uint64_t lastFrameCycle_ = 0;
  bool frameReady_ = false;
  std::vector<uint8_t> frame_;
};

} // namespace a2e::iigs
