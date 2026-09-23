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

#include "../debug/machine_debug.hpp"
#include "../input/joyport.hpp"
#include "../disk-image/disk_converter.hpp"

#include <string>

#include <array>
#include <functional>
#include <cstdint>
#include <memory>
#include <vector>

namespace a2e {
class Audio;
class CPU65816;
class DiskController;
class ExpansionCard;
class Keyboard;
class MockingboardCard;
class SmartPortCard;
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

  /**
   * Control-Reset: the RESET line, with memory left alone.
   *
   * The processor takes the vector from ROM and the registers go back to
   * their reset values, but the fast RAM, the Mega II's RAM and the disk in
   * the drive are exactly as they were — so the firmware finds its warm-start
   * bytes and restarts what was running, as a //e's does. `reset()` is the
   * power switch; this is the key.
   */
  void warmReset();

  /** Run for this many cycles of the machine's *slow* side. */
  void runCycles(int slowCycles);

  /** Run one instruction, whatever it costs. Returns the cycles it took. */
  int step();

  // ===== Debugging =====
  //
  // The same facilities a //e has, and deliberately the same object behind
  // them: a breakpoint, a watchpoint, a trace entry and a beam position are
  // the same questions on a 65816 as on a 6502, and the host asks them the
  // same way whichever machine is running.

  /** Breakpoints, watchpoints, the trace ring and beam breakpoints. */
  MachineDebug &debug() { return debug_; }
  const MachineDebug &debug() const { return debug_; }

  bool isPaused() const { return paused_; }

  /**
   * Stop or start the machine.
   *
   * Resuming from a breakpoint skips the one it is sitting on, or continuing
   * would stop again without running an instruction.
   */
  void setPaused(bool paused);

  /** One instruction, whether or not a breakpoint sits on it. */
  void stepInstruction();

  /**
   * Run to just after the call at the program counter, or one instruction if
   * it is not a call.
   *
   * Returns the address the temporary breakpoint was put on, or 0 if it
   * single-stepped instead. A JSL is four bytes where a JSR is three, which
   * is the whole of why this cannot be the //e's version.
   */
  uint32_t stepOver();

  /**
   * Run to the return address on the stack.
   *
   * Returns the address, or 0 if the stack does not hold a plausible one. The
   * 65816 has two kinds of return — RTS takes two bytes off the stack and RTL
   * three — so which one is looked for depends on how the subroutine was
   * entered, and that is not knowable from the stack alone. The instruction
   * at the program counter is consulted when it is a return, and RTS is
   * assumed otherwise, because a long call is the rarer of the two.
   */
  uint32_t stepOut();

  /** Where the beam is, derived from the machine's own clock. */
  BeamPosition beam() const;

  // ===== The serial ports =====
  //
  // A IIgs has two sockets on the back rather than a card in a slot, and the
  // SCC behind them is part of the machine. The host's serial calls mean the
  // same thing here as they do on a //c: a byte the machine sends goes out to
  // whatever is attached, and a byte from outside arrives at the modem port,
  // because a printer does not talk back.

  /** Every byte either port transmits, with the byte's port: 1 or 2. */
  using SerialTxCallback = std::function<void(int port, uint8_t byte)>;
  void setSerialTxCallback(SerialTxCallback cb);

  /** A byte on a fitted parallel card's Centronics port. */
  using ParallelTxCallback = std::function<void(uint8_t byte)>;
  void setParallelTxCallback(ParallelTxCallback cb);

  /** A byte from outside, into the modem port. */
  void serialReceive(uint8_t byte);

  /** Which SCC channel is which socket. Settled by the firmware, not guessed. */
  static constexpr int PRINTER_PORT = 1; // slot 1, SCC channel A
  static constexpr int MODEM_PORT = 2;   // slot 2, SCC channel B

  // ===== The parts =====

  CPU65816 &cpu() { return *cpu_; }
  IIgsMemory &memory() { return *memory_; }

  /** The Mega II's video generator: a //e's, drawing a //e's picture. */
  Video &video() { return *video_; }

  /**
   * The speaker, which a IIgs has as well as an Ensoniq.
   *
   * $C030 is a Mega II address and toggles the same one-bit speaker every
   * Apple II has; the synthesiser is a separate chip on a separate pair of
   * addresses, and the two are mixed. A machine given only the Ensoniq is
   * silent through every beep, every click and every game written before 1986
   * — which is most of what it runs.
   */
  Audio &audio() { return *audio_; }

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

  /**
   * The SmartPort, which is slot 5 and is part of the machine.
   *
   * A real IIgs has SmartPort firmware of its own in that slot, and it is the
   * genuine article: it polls the IWM looking for a Sony 3.5" drive and for
   * whatever is daisy-chained off the port behind it. Neither of those is
   * modelled — the 3.5" recording scheme and the SmartPort bus are each their
   * own piece of work — so what sits in slot 5 here is the block-device
   * SmartPort the other machines use, answering the same ProDOS and SmartPort
   * calls from images the host hands over.
   *
   * It is not a card somebody fitted. It has no entry in the Expansion Slots
   * window and needs no Control Panel setting: with an image loaded it answers
   * at $C500, and without one it has no ROM at all and the machine's own slot
   * 5 firmware shows through exactly as it did before.
   */
  static constexpr uint8_t SMARTPORT_SLOT = 5;

  SmartPortCard &smartPort() { return *smartPort_; }

  /**
   * Fit or remove a card in one of the seven sockets.
   *
   * A IIgs has seven real slots, and each one also has a built-in device
   * assigned to it; only one of the two answers at a time, and $C02D is the
   * switch. So fitting a card does not take the machine's own part away — it
   * puts something in the socket beside it, and the Slot register decides
   * which the processor reaches. `setSlotCard` names a card by the same id the
   * //e uses, or "empty".
   */
  bool setSlotCard(uint8_t slot, const std::string &cardId);
  std::string getSlotCardName(uint8_t slot) const;

  /**
   * The Control Panel's per-slot setting, which is $C02D.
   *
   * On a real machine the user reaches this through the firmware's Control
   * Panel; that is not reachable here (its hotkey needs an interrupt-driven
   * Desk Manager that never starts), so the emulator offers the same switch
   * directly. `internal` false hands the slot to whatever is in the socket.
   *
   * Slot 3 is not in the register — bit 3 is reserved and its ROM follows the
   * //e's own SLOTC3ROM — so asking about it answers "internal" and setting it
   * does nothing.
   */
  bool isSlotInternal(uint8_t slot) const;
  void setSlotInternal(uint8_t slot, bool internal);
  bool insertBlockImage(int device, const uint8_t *data, size_t size,
                        const std::string &filename);
  void ejectBlockImage(int device);
  bool insertDisk(int drive, const uint8_t *data, size_t size,
                  const std::string &filename);
  void ejectDisk(int drive);

  // Reading a drive's image back out: what the host saves to a file, and what
  // the file explorer parses. Same conversions as the other machines do —
  // `DiskConverter` is the one implementation — with this machine's own
  // buffers, because a IIgs and an Emulator are never alive at once but do not
  // share anything either.
  const uint8_t *exportDiskDataAs(int drive, DiskSaveFormat format,
                                  size_t *size);
  const uint8_t *getDiskSectorsDOSOrder(int drive, size_t *size);
  bool canExportDiskAs(int drive, DiskSaveFormat format);
  DiskSaveFormat getDiskNativeFormat(int drive);
  const char *getDiskFilename(int drive) const;
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

  /**
   * The game port, which a IIgs has on the back like every Apple II.
   *
   * The paddle timers are the Mega II's, so the values go there; the buttons
   * are shared with the Apple keys on $C061/$C062, exactly as they are on a
   * //e, so either one pressed reads as pressed.
   */
  void setPaddleValue(int paddle, int value);
  int getPaddleValue(int paddle) const;
  void setButton(int button, bool pressed);
  /** What $C061-$C063 read: the buttons, or the Joyport's switches. */
  uint8_t buttonLine(int button) const;

  /**
   * What is plugged into that port: the Apple joystick, or a Sirius Joyport.
   *
   * A IIgs has the same 9-pin connector and the same three pushbutton inputs,
   * so the Joyport's multiplexing works here exactly as it does on a //e —
   * the annunciators choosing which stick and which axis pair the three lines
   * are reporting. It is a host preference like the speed multiplier: reset
   * releases the switches but keeps the device, and it is not written into a
   * save state.
   */
  void setGamePortDevice(GamePortDevice device);
  GamePortDevice gamePortDevice() const { return gamePortDevice_; }
  /**
   * How long after a reset the Joyport stays off PB0/PB1.
   *
   * The same trouble a //e has, an order of magnitude later. Both lines idle
   * high on a Joyport, which is indistinguishable from a held Open and Closed
   * Apple, and the startup firmware reads exactly those to choose between a
   * normal start, the Control Panel and the self test — so a machine with a
   * Joyport fitted went into the self test and drew nothing at all. A //e
   * looks within a few milliseconds of reset; this machine looks twice, at
   * about 229,000 and 396,000 cycles of the Mega II's clock — a fifth and
   * four tenths of a second, after its power-on diagnostics — and then never
   * again. A second's worth of window covers both with room to spare, and is
   * still far shorter than the time it takes anything to boot off a disk and
   * ask about a joystick.
   */
  static constexpr uint64_t JOYPORT_RESET_GUARD_CYCLES = 1000000;

  /** Set one Joyport stick's switches (a mask of Joyport::SwitchBit). */
  void setJoyportStick(int stick, int switches);
  int getJoyportStick(int stick) const { return joyport_.stickState(stick); }



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

  // ===== Save states =====
  //
  // The same two calls Emulator has, with the same header in front of the
  // bytes, so the host saves and restores a IIgs exactly as it does a //e and
  // a state from either machine is refused by the other by its id.

  const uint8_t *exportState(size_t *size);
  bool importState(const uint8_t *data, size_t size);

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
  std::unique_ptr<Audio> audio_;
  std::unique_ptr<Video> video_;
  std::unique_ptr<IIgsVideo> screen_;
  std::unique_ptr<Keyboard> keyboard_;
  // The game port's three pushbuttons, ORed with the Apple keys on read.
  std::array<bool, 3> buttonState_ = {false, false, false};
  // ...unless a Joyport is fitted, which drives those same three lines itself
  // and reads them active low, so it answers *instead of* the buttons.
  GamePortDevice gamePortDevice_ = GamePortDevice::AppleJoystick;
  Joyport joyport_;
  // Cycle after which the Joyport may drive PB0/PB1 again following a reset.
  uint64_t joyportResetGuardCycle_ = 0;
  DiskController *disk_ = nullptr;   // Owned by the Mega II's slot
  SmartPortCard *smartPort_ = nullptr; // ...and so is this

  // The cards the user fitted, by slot, so they can be stepped and asked
  // about their interrupt line without walking the whole socket array on the
  // hottest loop in the machine. `IIgsMemory` owns them.
  std::vector<ExpansionCard *> fittedCards_;
  void refreshFittedCards();

  // A Mockingboard in a socket, if there is one: its samples are added after
  // the $C03C amplifier rather than through it, so a card's music does not
  // fade with the ROM's bell.
  MockingboardCard *mockingboard_ = nullptr;
  std::vector<float> cardMix_;

  // Kept so a card fitted later reaches the host's printer the way one fitted
  // earlier does. The machine's own two ports go straight to the SCC.
  SerialTxCallback serialTxCallback_;
  ParallelTxCallback parallelTxCallback_;

  std::vector<uint8_t> diskExportBuffer_;
  std::vector<uint8_t> diskSectorBuffer_;

  // Scratch for the Ensoniq's half of the mix, kept rather than reallocated
  // every buffer.
  std::vector<float> ensoniqMix_;

  MachineDebug debug_;
  bool paused_ = false;

  void recordTrace();

  /** Tell the ADB which modifier keys are down. See $C025. */
  void reportModifiers(bool shift, bool ctrl, bool capsLock, int browserKeycode);
  // Caps Lock as the last key-down reported it; a key-up cannot say.
  bool capsLockOn_ = false;

  int samplesGenerated_ = 0;
  uint64_t lastFrameCycle_ = 0;
  bool frameReady_ = false;
  // How many of this frame's lines the beam has finished, for the VGC's
  // scan-line interrupt (see raiseScanLineInterrupts).
  int linesFinished_ = 0;
  // Where the Ensoniq's clock had got to, so each step feeds it the cycles
  // the step took.
  uint64_t soundCycle_ = 0;

  // The amplifier's volume as the speaker hears it. The nibble's changes are
  // kept with the time they happened, and the gain follows them per sample
  // through a slew, because the control is analogue and sits after the
  // coupling capacitor: restoring the volume after the ROM's bell has ramped
  // it to nothing must not bring the speaker's decaying tail back as a thump.
  struct VolumeChange { uint64_t cycle; uint8_t nibble; };
  std::vector<VolumeChange> volumeChanges_;
  float speakerGain_ = 0.0f;
  uint8_t speakerNibble_ = 0;
  std::vector<uint8_t> frame_;
  std::vector<uint8_t> stateBuffer_;

  void raiseScanLineInterrupts();
};

} // namespace a2e::iigs
