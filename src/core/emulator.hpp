/*
 * emulator.hpp - Core emulator coordinator tying together CPU, memory, video, audio, and peripherals
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "audio/audio.hpp"
#include "cpu/6502/cpu6502.hpp"
#include "cards/disk2/disk2_card.hpp"
#include "cards/iwm/iwm.hpp"
#include "cards/expansion_card.hpp"
#include "input/joyport.hpp"
#include "input/keyboard.hpp"
#include "debug/machine_debug.hpp"
#include "machine/machine_profile.hpp"
#include "cards/mockingboard/mockingboard_card.hpp"
#include "cards/mouse/mouse_card.hpp"
#include "cards/smartport/smartport_card.hpp"
#include "cards/parallel/parallel_card.hpp"
#include "cards/softcard/softcard_z80.hpp"
#include "cards/serial/serial_port.hpp"
#include "input/mouse_iou.hpp"
#include "cards/ssc/ssc_card.hpp"
#include "disk-image/disk_converter.hpp"
#include "filesystem/fs_write_status.hpp"
#include "mmu/mmu.hpp"
#include "types.hpp"
#include "video/video.hpp"
#include <cstdint>
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>

namespace a2e {

class Emulator {
public:
  // BASIC stepping modes
  enum class BasicStepMode { None, Line, Statement };

  // Which machine to model. Everything downstream — the CPU fitted, the video
  // timing, how much RAM answers, which cards the slots take — follows from the
  // profile this selects.
  explicit Emulator(MachineId machine = MachineId::AppleIIe);
  ~Emulator();

  // The machine being modelled.
  const MachineProfile &getMachine() const { return *machine_; }

  // Whether this machine's system ROM was built into the binary. False means
  // the machine is described but cannot run — see init().
  bool hasSystemROM() const { return systemRomLoaded_; }

  // The same question about a machine that is not running. Lets a host offer a
  // choice of machines and say which of them it can actually start, without
  // building one to find out. The ROM arrays live in emulator.cpp, so asking
  // here keeps them out of every other translation unit.
  static bool isMachineRunnable(MachineId machine);

  /**
   * The system ROM built into this build for a machine, or nullptr.
   *
   * It lives on this class because emulator.cpp is the translation unit that
   * owns the generated ROM arrays — including them anywhere else would define
   * them twice. A machine that is not built from Emulator's parts still needs
   * its ROM from somewhere, and this is where somewhere is.
   */
  static const uint8_t *systemROMFor(MachineId machine, size_t &size);

  /** The character generator for a machine, on the same terms. */
  static const uint8_t *characterROMFor(MachineId machine, size_t &size);

  // Initialization
  void init();
  void reset();     // Cold reset - clears memory
  void warmReset(); // Warm reset - CPU only, preserves memory

  // Execution
  void runCycles(int cycles);
  int generateStereoAudioSamples(float *buffer, int sampleCount);

  // Audio-driven frame synchronization
  // Returns number of complete frames worth of samples generated since last
  // call
  int consumeFrameSamples();

  // Frame management
  bool isFrameReady() const { return frameReady_; }
  void clearFrameReady() { frameReady_ = false; }
  const uint8_t *getFramebuffer() const;
  size_t getFramebufferSize() const {
    return machine_->display.framebufferSize();
  }

  // Input - raw browser keycodes (preferred)
  int handleRawKeyDown(int browserKeycode, bool shift, bool ctrl, bool alt,
                       bool meta, bool capsLock, int keyLocation = 0);
  void handleRawKeyUp(int browserKeycode, bool shift, bool ctrl, bool alt,
                      bool meta, int keyLocation = 0);

  /** Release every held modifier — for when the host loses keyboard focus. */
  void releaseModifiers();

  // Input - direct Apple II keycodes
  void keyDown(int keycode);
  void keyUp(int keycode);

  // ---- Paste / typed-text buffer -------------------------------------
  //
  // Pasted text goes into a FIFO here rather than being metered out a
  // character at a time by the host. The machine drains it itself: a
  // character is handed to the keyboard latch, and the next one is loaded
  // the moment the program clears the strobe. That is what a type-ahead
  // buffer on a real machine does, and it means the paste runs at whatever
  // rate the software reads keys — no host timer, no speed boost, and no
  // round trip per character.

  /**
   * Queue UTF-8 text for typing. Characters that have no Apple II
   * equivalent are dropped, exactly as charToAppleKey() decides.
   * @return the number of key codes actually queued
   */
  size_t pasteText(const char *utf8);

  /** Queue one already-translated Apple II key code. */
  void pasteKey(int appleKey);

  /** How many key codes are still waiting to be typed. */
  size_t pastePending() const { return pasteBuffer_.size(); }

  /** Discard anything still waiting (Cancel, reset, state load). */
  void clearPasteBuffer();
  void setButton(int button, bool pressed);  // Set button state (0=Open Apple, 1=Closed Apple, 2=Button2)
  void setPaddleValue(int paddle, int value);  // Set paddle value (0-3, value 0-255)
  int getPaddleValue(int paddle) const;  // Get paddle value (0-3)

  // Game I/O connector: an Apple resistive joystick or a Sirius Joyport.
  // Host preference rather than machine state, so it survives reset and is not
  // written into a save state.
  /** How long after a reset the Joyport stays off PB0/PB1 (~50ms). */
  static constexpr uint64_t JOYPORT_RESET_GUARD_CYCLES = 50000;

  void setGamePortDevice(GamePortDevice device);
  GamePortDevice gamePortDevice() const { return gamePortDevice_; }
  /** Set one Joyport stick's switches (a mask of Joyport::SwitchBit). */
  void setJoyportStick(int stick, int switches);
  int getJoyportStick(int stick) const { return joyport_.stickState(stick); }

  // Mouse input
  void mouseMove(int dx, int dy);
  void mouseButton(bool pressed);
  bool isKeyboardReady() const { return (keyboardLatch_ & 0x80) == 0; }  // True if strobe cleared

  // Disk management
  bool insertDisk(int drive, const uint8_t *data, size_t size,
                  const char *filename);
  bool insertBlankDisk(int drive);
  void ejectDisk(int drive);
  const uint8_t *getDiskData(int drive, size_t *size) const;
  const uint8_t *exportDiskData(int drive, size_t *size);

  /**
   * Serialise a drive's image in a chosen save format, converting sector order
   * or encoding to a bit stream as needed. The returned pointer is owned by the
   * emulator and stays valid until the next call.
   *
   * @param drive  Drive number (0 or 1)
   * @param format Format to produce
   * @param size   Output: byte count, 0 if the conversion is not possible
   * @return Pointer to the converted image, or nullptr on failure
   */
  const uint8_t *exportDiskDataAs(int drive, DiskSaveFormat format, size_t *size);

  /**
   * A drive's sectors in DOS 3.3 order, whatever order the file holds them in.
   *
   * This is what a filesystem parser wants: the DOS 3.3 reader assumes DOS
   * order outright, and the ProDOS reader works from either. The returned
   * pointer is owned by the emulator and stays valid until the next call.
   *
   * @param size Output: byte count, 0 if the disk has no readable sectors
   * @return Pointer to the sectors, or nullptr if they cannot be read
   */
  const uint8_t *getDiskSectorsDOSOrder(int drive, size_t *size);

  /** Whether a drive's image can be saved in a format at all */
  bool canExportDiskAs(int drive, DiskSaveFormat format);

  /** The format a drive's image came from */
  DiskSaveFormat getDiskNativeFormat(int drive);

  const char *getDiskFilename(int drive) const;

  /**
   * Write a binary file into the filesystem of the disk in a drive, replacing
   * any file of the same name. DOS 3.3 and ProDOS volumes are both handled;
   * the filesystem is chosen by what is actually on the disk.
   *
   * This is a host-side write — the emulated machine plays no part in it — so
   * it works whether or not the machine is running, and takes effect the next
   * time the guest reads the affected tracks.
   *
   * @param drive       Drive number (0 or 1)
   * @param filename    Name to write; folded to the filesystem's conventions
   * @param loadAddress Load address recorded for the binary
   * @param data        File payload
   * @param len         Payload length in bytes
   */
  FsWriteStatus writeBinaryFileToDisk(int drive, const char *filename,
                                      uint16_t loadAddress, const uint8_t *data,
                                      size_t len);

  // Debugger interface
  void addBreakpoint(uint16_t address);
  void removeBreakpoint(uint16_t address);
  void enableBreakpoint(uint16_t address, bool enabled);
  bool isBreakpointHit() const { return debug_.isBreakpointHit(); }
  uint16_t getBreakpointAddress() const {
    return static_cast<uint16_t>(debug_.breakpointAddress());
  }

  // BASIC line and statement breakpoints
  void addBasicBreakpoint(uint16_t lineNumber, int statementIndex);
  void removeBasicBreakpoint(uint16_t lineNumber, int statementIndex);
  void clearBasicBreakpoints();
  void clearBasicBreakpointHit();  // Clear hit state for fresh run
  bool hasBasicBreakpoints() const { return !basicBreakpoints_.empty(); }
  bool isBasicBreakpointHit() const { return basicBreakpointHit_; }
  uint16_t getBasicBreakLine() const { return basicBreakLine_; }

  // BASIC condition-only rules: evaluated in C++ at every $D820
  void addBasicConditionRule(int id, const char* expression);
  void removeBasicConditionRule(int id);
  void clearBasicConditionRules();
  int getBasicConditionRuleHitId() const { return basicConditionRuleHitId_; }

  // BASIC stepping - execute current line/statement and stop at next
  void stepBasicLine();
  void stepBasicStatement();
  bool isBasicStepping() const { return basicStepMode_ != BasicStepMode::None; }
  bool isBasicProgramRunning() const { return basicProgramRunning_; }

  // BASIC runtime error state
  bool isBasicErrorHit() const { return basicErrorHit_; }
  uint16_t getBasicErrorLine() const { return basicErrorLine_; }
  uint16_t getBasicErrorTxtptr() const { return basicErrorTxtptr_; }
  uint8_t getBasicErrorCode() const { return basicErrorCode_; }
  void clearBasicError() { basicErrorHit_ = false; }
  uint16_t getBasicTxtptr() const;  // Get current TXTPTR for statement highlighting
  int getBasicStatementIndex();     // Get current statement index (0-based)

  // Statement geometry for an arbitrary line, for the debugger UI. These use
  // the same colon scan that drives statement breakpoint matching, so what the
  // UI highlights and where execution actually breaks cannot disagree.
  int getBasicStatementCountForLine(uint16_t lineNumber);
  int getBasicStatementIndexForLine(uint16_t lineNumber, uint16_t txtptr);

  // BASIC line heat map
  void setBasicHeatMapEnabled(bool enabled) { basicHeatMapEnabled_ = enabled; }
  bool isBasicHeatMapEnabled() const { return basicHeatMapEnabled_; }
  void clearBasicHeatMap() { basicHeatMap_.clear(); }
  const std::unordered_map<uint16_t, uint32_t>& getBasicHeatMap() const { return basicHeatMap_; }
  int getBasicHeatMapSize() const { return static_cast<int>(basicHeatMap_.size()); }
  // Copy heat map data into caller-provided buffers (lines[], counts[]), returns entry count
  int getBasicHeatMapData(uint16_t* lines, uint32_t* counts, int maxEntries) const;

  // Beam position (derived from cycle count)
  int getFrameCycle() const;
  int getBeamScanline() const;
  int getBeamHPos() const;
  int getBeamColumn() const;
  bool isInVBL() const;
  bool isInHBLANK() const;

  // Step Over / Step Out
  uint16_t stepOver();   // Returns temp breakpoint address, or 0 if single-stepped
  uint16_t stepOut();    // Returns temp breakpoint address, or 0 if invalid
  void clearTempBreakpoint();
  bool isTempBreakpointHit() const { return debug_.isTempBreakpointHit(); }

  // CPU state access
  uint16_t getPC() const { return cpu_->getPC(); }
  uint8_t getSP() const { return cpu_->getSP(); }
  uint8_t getA() const { return cpu_->getA(); }
  uint8_t getX() const { return cpu_->getX(); }
  uint8_t getY() const { return cpu_->getY(); }
  uint8_t getP() const { return cpu_->getP(); }
  uint64_t getTotalCycles() const { return cpu_->getTotalCycles(); }
  bool isIRQPending() const { return cpu_->isIRQPending(); }
  bool isNMIPending() const { return cpu_->isNMIPending(); }
  bool isNMIEdge() const { return cpu_->isNMIEdge(); }

  // CPU state setters (for debugger register editing)
  void setPC(uint16_t v) { cpu_->setPC(v); }
  void setSP(uint8_t v) { cpu_->setSP(v); }
  void setA(uint8_t v) { cpu_->setA(v); }
  void setX(uint8_t v) { cpu_->setX(v); }
  void setY(uint8_t v) { cpu_->setY(v); }
  void setP(uint8_t v) { cpu_->setP(v); }

  // Watchpoints
  // The watchpoint and trace types belong to MachineDebug, which both
  // machines own; these keep the names callers already use.
  using WatchpointType = MachineDebug::WatchpointType;
  static constexpr WatchpointType WP_READ = MachineDebug::WP_READ;
  static constexpr WatchpointType WP_WRITE = MachineDebug::WP_WRITE;
  static constexpr WatchpointType WP_READWRITE = MachineDebug::WP_READWRITE;
  void addWatchpoint(uint16_t startAddr, uint16_t endAddr, WatchpointType type);
  void removeWatchpoint(uint16_t startAddr);
  void clearWatchpoints();
  bool isWatchpointHit() const { return debug_.isWatchpointHit(); }
  uint16_t getWatchpointAddress() const {
    return static_cast<uint16_t>(debug_.watchpointAddress());
  }
  uint8_t getWatchpointValue() const { return debug_.watchpointValue(); }
  bool isWatchpointWrite() const { return debug_.isWatchpointWrite(); }

  // Beam breakpoints
  int32_t addBeamBreakpoint(int16_t scanline, int16_t hPos);  // returns ID, -1 if full
  void removeBeamBreakpoint(int32_t id);
  void enableBeamBreakpoint(int32_t id, bool enabled);
  void clearAllBeamBreakpoints();
  bool isBeamBreakpointHit() const { return debug_.isBeamBreakpointHit(); }
  int32_t getBeamBreakpointHitId() const { return debug_.beamBreakpointHitId(); }
  int16_t getBeamBreakScanline() const { return debug_.beamBreakScanline(); }
  int16_t getBeamBreakHPos() const { return debug_.beamBreakHPos(); }

  // Trace log
  using TraceEntry = MachineDebug::TraceEntry;
  void setTraceEnabled(bool enabled) { debug_.setTraceEnabled(enabled); }
  bool isTraceEnabled() const { return debug_.isTraceEnabled(); }
  void clearTrace() { debug_.clearTrace(); }
  size_t getTraceCount() const { return debug_.traceCount(); }
  size_t getTraceHead() const { return debug_.traceHead(); }
  const TraceEntry* getTraceBuffer() const { return debug_.traceBuffer(); }
  size_t getTraceCapacity() const { return debug_.traceCapacity(); }

  /**
   * Breakpoints, watchpoints, the trace ring and beam breakpoints.
   *
   * The same object a IIgs owns, so the host asks one set of questions
   * whichever machine is running. The methods above are the older, 16-bit
   * spellings of the same thing and forward here.
   */
  MachineDebug &debug() { return debug_; }
  const MachineDebug &debug() const { return debug_; }

  // Cycle profiling
  void setProfileEnabled(bool enabled) { profileEnabled_ = enabled; }
  bool isProfileEnabled() const { return profileEnabled_; }
  void clearProfile() { profileCycles_.fill(0); }
  const uint32_t* getProfileCycles() const { return profileCycles_.data(); }

  // Speed control
  void setSpeedMultiplier(int multiplier);
  int getSpeedMultiplier() const { return speedMultiplier_; }

  // Pause/resume
  bool isPaused() const { return paused_; }
  void setPaused(bool paused);

  // Single step
  void stepInstruction();

  // Memory access
  uint8_t readMemory(uint16_t address) const;
  uint8_t peekMemory(uint16_t address) const; // Non-side-effecting read for debugger
  void writeMemory(uint16_t address, uint8_t value);

  // Disassembly
  const char *disassembleAt(uint16_t address);

  // Soft switch state (64-bit packed state)
  uint64_t getSoftSwitchState() const;

  // Screen text extraction (for text selection / copy)
  static int screenCodeToAscii(uint8_t code);
  const char* readScreenText(int startRow, int startCol, int endRow, int endCol);

  // State serialization for save/restore
  // Returns pointer to state data and sets size. Caller does not own the pointer.
  const uint8_t *exportState(size_t *size);
  // Restores state from data. Returns true on success.
  bool importState(const uint8_t *data, size_t size);

  // Components access
  MMU &getMMU() { return *mmu_; }
  Video &getVideo() { return *video_; }
  Audio &getAudio() { return *audio_; }
  // The drive controller, whichever part this machine carries: a Disk II card
  // in a slot, or a //c's IWM soldered to the board. Everything the host asks
  // it — insert, eject, which track, is the motor running — is the same
  // question of both, so callers take the base rather than the part.
  DiskController &getDisk() { return *disk_; }
  DiskController *getDiskPtr() { return disk_; }
  MockingboardCard &getMockingboard() { return *mockingboard_; }
  MockingboardCard *getMockingboardPtr() { return mockingboard_; }
  MouseCard* getMouseCard() { return mouse_; }

  // A //c's mouse, which is not a card: null on machines whose mouse is one.
  MouseIOU* getMouseIOU() { return mouseIOU_.get(); }
  bool isMouseInstalled() const { return mouse_ != nullptr || mouseIOU_ != nullptr; }
  SmartPortCard* getSmartPortCard() { return smartport_; }
  SoftCardZ80* getSoftCard() { return softcard_; }
  SSCCard* getSSCCard() { return ssc_; }

  // A //c's built-in ports: 1 is the printer port, 2 the modem port. Null on a
  // machine whose serial is a card in a slot instead.
  SerialPort* getSerialPort(uint8_t port) {
    return (port == 1 || port == 2) ? serialPorts_[port - 1] : nullptr;
  }

  // Serial I/O. The same two calls serve a Super Serial Card and a //c's
  // built-in ports, because the host's question is about a serial line rather
  // than about what is providing it.
  void serialReceive(uint8_t byte);
  bool isSSCInstalled() const { return ssc_ != nullptr; }
  bool isSerialInstalled() const {
    return ssc_ != nullptr || serialPorts_[0] != nullptr ||
           serialPorts_[1] != nullptr;
  }
  void setSerialTxCallback(SSCCard::SerialTxCallback cb);

  // Parallel Interface Card (a generic Centronics port; a printer is one device
  // that may be attached downstream — the card itself knows nothing of printers)
  ParallelCard* getParallelCard() { return parallelCard_; }
  bool isParallelCardInstalled() const { return parallelCard_ != nullptr; }
  void setParallelTxCallback(ParallelCard::ParallelTxCallback cb);

  // No-Slot Clock
  void enableNoSlotClock(bool enable) { mmu_->enableNoSlotClock(enable); }
  bool isNoSlotClockEnabled() const { return mmu_->isNoSlotClockEnabled(); }

  // SmartPort hard drive management
  bool insertSmartPortImage(int device, const uint8_t* data, size_t size, const char* filename);
  void ejectSmartPortImage(int device);
  bool isSmartPortImageInserted(int device) const;
  const char* getSmartPortImageFilename(int device) const;
  bool isSmartPortImageModified(int device) const;
  const uint8_t* exportSmartPortImageData(int device, size_t* size) const;
  const uint8_t* getSmartPortBlockData(int device, size_t* size) const;
  bool isSmartPortCardInstalled() const { return smartport_ != nullptr; }

  // Slot management
  const char* getSlotCardName(uint8_t slot) const;
  bool setSlotCard(uint8_t slot, const char* cardId);
  bool isSlotEmpty(uint8_t slot) const;

private:
  // Memory callbacks for CPU
  uint8_t cpuRead(uint16_t address);
  void cpuWrite(uint16_t address, uint8_t value);

  // Keyboard handling
  uint8_t getKeyboardData();
  void clearKeyboardStrobe();

  /** Move the next buffered key into the latch, if the latch is free. */
  void loadNextPasteKey();

  /**
   * Recompute AKD ($C010 bit 7) from the two things that can hold a key down:
   * a physically held host key, and a pasted character still sitting unread in
   * the latch. Deriving it is what stops it sticking on — every path that
   * asserts it now has a matching release.
   */
  void updateAnyKeyDown();

  // Speaker callback
  void toggleSpeaker();

  // Components
  // Not owned: profiles are static constexpr objects with program lifetime.
  // Declared first so the subsystems below can be constructed from it.
  const MachineProfile *machine_ = &defaultMachineProfile();

  // Set by init(): whether this machine's ROM is present in the build.
  bool systemRomLoaded_ = false;

  std::unique_ptr<MMU> mmu_;
  std::unique_ptr<CPU6502> cpu_;
  std::unique_ptr<Video> video_;
  std::unique_ptr<Audio> audio_;
  std::unique_ptr<Keyboard> keyboard_;

  // Non-owning pointers to cards (owned by MMU slot system)
  DiskController* disk_ = nullptr;
  MockingboardCard* mockingboard_ = nullptr;
  MouseCard* mouse_ = nullptr;
  // Owned, unlike the cards: a //c's mouse is part of the machine, not
  // something fitted to it, so there is no slot to hold it.
  std::unique_ptr<MouseIOU> mouseIOU_;
  SmartPortCard* smartport_ = nullptr;
  SoftCardZ80* softcard_ = nullptr;
  SSCCard* ssc_ = nullptr;
  SerialPort* serialPorts_[2] = {nullptr, nullptr};
  ParallelCard* parallelCard_ = nullptr;
  ParallelCard::ParallelTxCallback parallelTxCallback_;
  SSCCard::SerialTxCallback serialTxCallback_;

  // Storage for cards when removed from slots
  std::unique_ptr<ExpansionCard> diskStorage_;
  std::unique_ptr<ExpansionCard> mbStorage_;

  // Keyboard state. keyDown_ is AKD, derived by updateAnyKeyDown() rather than
  // set directly, and kept as a member because save states carry it.
  uint8_t keyboardLatch_ = 0;
  bool keyDown_ = false;

public:
  // Pacing for the paste buffer, in CPU cycles so it scales with the machine
  // rather than with the host.
  //
  // A pasted key must NOT appear the instant the strobe is cleared. Clearing
  // the strobe twice is a keyboard *flush*, and it is everywhere in Apple II
  // software — `POKE -16368,0` in BASIC, `STA $C010` in assembly, the ROM and
  // DOS doing it before settling down to wait for input. A person typing
  // leaves nothing pending for those flushes to eat; a buffer that refilled
  // the latch immediately handed them a fresh character to throw away, which
  // is seen as a paste arriving with characters missing, and only in the
  // programs that flush. The gap is what makes a flush harmless: it has to
  // outlast the work a program does between taking a character and flushing.
  //
  // A poll of $C000 finding the keyboard empty looks like a better signal —
  // the program asking for input — but it is not one: Applesoft polls $C000
  // between every statement to check for Ctrl-C, so the key is released long
  // before the program reaches its flush. That was measured, not assumed.
  //
  // KEY is ~65 characters a second, several times faster than anyone types
  // and far longer than the handful of statements a program runs between a
  // GET and its flush. LINE is much longer because a carriage return leaves
  // the machine a line to digest — Applesoft tokenises it, DOS and
  // BASIC.SYSTEM run their command parsers — with flushes at the end of that
  // work. Both are emulated time, so the paste speed boost shortens the
  // wall-clock wait in step with everything else.
  static constexpr uint64_t PASTE_KEY_GAP_CYCLES = 15000;   // ~15ms
  static constexpr uint64_t PASTE_LINE_GAP_CYCLES = 150000; // ~150ms

private:

  // Type-ahead buffer for pasted / programmatically typed text. Host state,
  // not machine state: it is deliberately absent from save states, like the
  // speed multiplier, so loading a state cannot resurrect a half-finished
  // paste. pasteHoldsKey_ records that the key currently in the latch came
  // from this buffer, so draining it can release AKD without touching the
  // state of a physically held key.
  std::deque<uint8_t> pasteBuffer_;
  bool pasteHoldsKey_ = false;
  // Earliest cycle at which the next buffered key may enter the latch.
  uint64_t pasteReadyCycle_ = 0;

  // Button state (Open Apple, Closed Apple, Button 2)
  bool buttonState_[3] = {false, false, false};
  uint8_t getButtonState(int button);

  // Game I/O connector. The Joyport drives the same three pushbutton inputs
  // the Apple keys do, and drives them inverted, so it replaces them rather
  // than adding to them — which is why this is a device selection.
  GamePortDevice gamePortDevice_ = GamePortDevice::AppleJoystick;
  Joyport joyport_;

  // Cycle after which the Joyport may drive PB0/PB1 again following a reset.
  //
  // The //e's reset routine reads $C061 and $C062 to see whether Open or
  // Closed Apple is held: Open Apple asks for a cold boot, Closed Apple runs
  // the self test. A Joyport idles both lines *high*, which is exactly what a
  // held key looks like, so a //e with one plugged in ran the self test on
  // every reset and could never reach a prompt. That is faithful — the game
  // connector's pins 2 and 3 really are the Apple keys on a //e, which is why
  // the Joyport belongs to the II and II+ era — but it makes the device
  // useless on the machine most people run here.
  //
  // So the Joyport lets go of PB0 and PB1 for a brief window after reset, long
  // enough to cover the ROM's key check and far too short for a game to have
  // asked about the stick yet. PB2 is untouched: no Apple key is wired to it.
  uint64_t joyportResetGuardCycle_ = 0;

  // Speed control
  int speedMultiplier_ = 1;
  // Push the current speed to the speaker and Mockingboard, both of which
  // measure their sample rate in CPU cycles.
  void applySpeedToAudio();

  // Frame timing
  uint64_t lastFrameCycle_ = 0;
  bool frameReady_ = false;

  // Audio-driven frame sync
  static constexpr int SAMPLES_PER_FRAME = 800; // 48000 Hz / 60 Hz
  int samplesGenerated_ = 0;

  // Breakpoints, watchpoints, the trace ring, beam breakpoints: the parts of
  // debugging that are not about this processor, shared with the IIgs.
  MachineDebug debug_;
  bool paused_ = false;

  // BASIC breakpoints - supports whole-line and statement-level
  struct BasicBreakpoint {
    uint16_t lineNumber;
    int8_t statementIndex;  // -1 = whole line, 0+ = specific statement
    bool operator<(const BasicBreakpoint& o) const {
      if (lineNumber != o.lineNumber) return lineNumber < o.lineNumber;
      return statementIndex < o.statementIndex;
    }
  };
  std::set<BasicBreakpoint> basicBreakpoints_;
  bool basicBreakpointHit_ = false;
  struct BasicConditionRule {
    int id;
    std::string expression;
    bool enabled;
  };
  std::vector<BasicConditionRule> basicConditionRules_;
  int basicConditionRuleHitId_ = -1;
  uint16_t basicBreakLine_ = 0;
  uint16_t skipBasicBreakpointLine_ = 0xFFFF;  // Line to skip (0xFFFF = none)
  int8_t skipBasicBreakpointStmt_ = -1;        // Statement to skip (-1 = whole line)

  // BASIC program execution tracking - set by monitoring ROM entry points
  // $D912 (RUN) sets true, $D43C (RESTART/] prompt) sets false
  bool basicProgramRunning_ = false;

  // BASIC runtime error tracking - captured at $D412 (ERROR handler entry)
  bool basicErrorHit_ = false;
  uint16_t basicErrorLine_ = 0;      // CURLIN at error time
  uint16_t basicErrorTxtptr_ = 0;    // TXTPTR at error time
  uint8_t basicErrorCode_ = 0;       // X register (error message table offset)

  // BASIC stepping
  BasicStepMode basicStepMode_ = BasicStepMode::None;
  uint16_t basicStepFromLine_ = 0xFFFF;
  uint16_t basicStepFromTxtptr_ = 0;
  int basicStepFromStmtIndex_ = 0;
  uint16_t basicStepLineStart_ = 0;  // Address where current line's tokens start
  uint16_t basicStepNextColon_ = 0;  // Address of next colon after starting position (0 if none)
  bool basicStepSkipFirst_ = false;  // Skip first EXECUTE_STATEMENT hit when already there

  // BASIC line heat map - counts execution hits per line at $D820
  bool basicHeatMapEnabled_ = false;
  std::unordered_map<uint16_t, uint32_t> basicHeatMap_;

  // Helper to count colons (statement separators) between lineStart and txtptr
  int countColonsBetween(uint16_t lineStart, uint16_t txtptr);
  // Helper to find the start address of tokenized text for a BASIC line
  uint16_t findCurrentLineStart(uint16_t lineNumber);
  // Helper to find the next colon address after a given position within a line
  uint16_t findNextColonAfter(uint16_t lineStart, uint16_t afterPos);

  // Whether the MMU is routing every access through the watchpoint check,
  // which is a cost worth paying only while there is a watchpoint to check.
  bool watchpointsActive_ = false;

  // Watchpoint callbacks for the MMU
  void onWatchpointRead(uint16_t address, uint8_t value);
  void onWatchpointWrite(uint16_t address, uint8_t value);

  void recordTrace();

  // Cycle profiling
  bool profileEnabled_ = false;
  std::array<uint32_t, 65536> profileCycles_{};

  // Disassembly buffer
  mutable std::string disasmBuffer_;

  // Screen text extraction buffer
  mutable std::string screenTextBuffer_;

  // State serialization buffer
  mutable std::vector<uint8_t> stateBuffer_;

  // Holds the most recent format-converted disk image handed out by
  // exportDiskDataAs, which returns a pointer into it
  std::vector<uint8_t> diskExportBuffer_;

  // Separate buffer for getDiskSectorsDOSOrder, so a file browser holding a
  // pointer into it is not disturbed by a save happening alongside
  std::vector<uint8_t> diskSectorBuffer_;
};

} // namespace a2e
