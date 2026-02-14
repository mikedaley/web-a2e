/*
 * emulator.hpp - Core emulator coordinator tying together CPU, memory, video, audio, and peripherals
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "audio/audio.hpp"
#include "cpu/cpu6502.hpp"
#include "cards/disk2_card.hpp"
#include "cards/expansion_card.hpp"
#include "input/keyboard.hpp"
#include "cards/mockingboard_card.hpp"
#include "cards/mouse_card.hpp"
#include "cards/smartport/smartport_card.hpp"
#include "cards/softcard_z80.hpp"
#include "cards/ssc/ssc_card.hpp"
#include "mmu/mmu.hpp"
#include "types.hpp"
#include "video/video.hpp"
#include <cstdint>
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

  Emulator();
  ~Emulator();

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
  size_t getFramebufferSize() const { return FRAMEBUFFER_SIZE; }

  // Input - raw browser keycodes (preferred)
  int handleRawKeyDown(int browserKeycode, bool shift, bool ctrl, bool alt,
                       bool meta, bool capsLock);
  void handleRawKeyUp(int browserKeycode, bool shift, bool ctrl, bool alt,
                      bool meta);

  // Input - direct Apple II keycodes (for paste functionality)
  void keyDown(int keycode);
  void keyUp(int keycode);
  void setButton(int button, bool pressed);  // Set button state (0=Open Apple, 1=Closed Apple, 2=Button2)
  void setPaddleValue(int paddle, int value);  // Set paddle value (0-3, value 0-255)
  int getPaddleValue(int paddle) const;  // Get paddle value (0-3)

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
  const char *getDiskFilename(int drive) const;

  // Debugger interface
  void addBreakpoint(uint16_t address);
  void removeBreakpoint(uint16_t address);
  void enableBreakpoint(uint16_t address, bool enabled);
  bool isBreakpointHit() const { return breakpointHit_; }
  uint16_t getBreakpointAddress() const { return breakpointAddress_; }

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
  bool isTempBreakpointHit() const { return tempBreakpointHit_; }

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
  enum WatchpointType : uint8_t { WP_READ = 1, WP_WRITE = 2, WP_READWRITE = 3 };
  void addWatchpoint(uint16_t startAddr, uint16_t endAddr, WatchpointType type);
  void removeWatchpoint(uint16_t startAddr);
  void clearWatchpoints();
  bool isWatchpointHit() const { return watchpointHit_; }
  uint16_t getWatchpointAddress() const { return watchpointAddress_; }
  uint8_t getWatchpointValue() const { return watchpointValue_; }
  bool isWatchpointWrite() const { return watchpointIsWrite_; }

  // Beam breakpoints
  int32_t addBeamBreakpoint(int16_t scanline, int16_t hPos);  // returns ID, -1 if full
  void removeBeamBreakpoint(int32_t id);
  void enableBeamBreakpoint(int32_t id, bool enabled);
  void clearAllBeamBreakpoints();
  bool isBeamBreakpointHit() const { return beamBreakHit_; }
  int32_t getBeamBreakpointHitId() const { return beamBreakHitId_; }
  int16_t getBeamBreakScanline() const { return beamBreakHitScanline_; }
  int16_t getBeamBreakHPos() const { return beamBreakHitHPos_; }

  // Trace log
  struct TraceEntry {
    uint16_t pc;
    uint8_t opcode, a, x, y, sp, p;
    uint8_t operand1, operand2, instrLen;
    uint8_t padding;
    uint32_t cycle;
  };
  void setTraceEnabled(bool enabled) { traceEnabled_ = enabled; }
  bool isTraceEnabled() const { return traceEnabled_; }
  void clearTrace() { traceHead_ = 0; traceCount_ = 0; }
  size_t getTraceCount() const { return traceCount_; }
  size_t getTraceHead() const { return traceHead_; }
  const TraceEntry* getTraceBuffer() const { return traceBuffer_.data(); }
  size_t getTraceCapacity() const { return traceBuffer_.size(); }

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
  Disk2Card &getDisk() { return *disk_; }
  Disk2Card *getDiskPtr() { return disk_; }
  MockingboardCard &getMockingboard() { return *mockingboard_; }
  MockingboardCard *getMockingboardPtr() { return mockingboard_; }
  MouseCard* getMouseCard() { return mouse_; }
  SmartPortCard* getSmartPortCard() { return smartport_; }
  SoftCardZ80* getSoftCard() { return softcard_; }
  SSCCard* getSSCCard() { return ssc_; }

  // Serial I/O for Super Serial Card
  void serialReceive(uint8_t byte);
  bool isSSCInstalled() const { return ssc_ != nullptr; }
  void setSerialTxCallback(SSCCard::SerialTxCallback cb);

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

  // Speaker callback
  void toggleSpeaker();

  // Components
  std::unique_ptr<MMU> mmu_;
  std::unique_ptr<CPU6502> cpu_;
  std::unique_ptr<Video> video_;
  std::unique_ptr<Audio> audio_;
  std::unique_ptr<Keyboard> keyboard_;

  // Non-owning pointers to cards (owned by MMU slot system)
  Disk2Card* disk_ = nullptr;
  MockingboardCard* mockingboard_ = nullptr;
  MouseCard* mouse_ = nullptr;
  SmartPortCard* smartport_ = nullptr;
  SoftCardZ80* softcard_ = nullptr;
  SSCCard* ssc_ = nullptr;

  // Storage for cards when removed from slots
  std::unique_ptr<ExpansionCard> diskStorage_;
  std::unique_ptr<ExpansionCard> mbStorage_;

  // Keyboard state
  uint8_t keyboardLatch_ = 0;
  bool keyDown_ = false;

  // Button state (Open Apple, Closed Apple, Button 2)
  bool buttonState_[3] = {false, false, false};
  uint8_t getButtonState(int button);

  // Speed control
  int speedMultiplier_ = 1;

  // Frame timing
  uint64_t lastFrameCycle_ = 0;
  bool frameReady_ = false;

  // Audio-driven frame sync
  static constexpr int SAMPLES_PER_FRAME = 800; // 48000 Hz / 60 Hz
  int samplesGenerated_ = 0;

  // Debugger state
  std::set<uint16_t> breakpoints_;
  std::set<uint16_t> disabledBreakpoints_;
  bool breakpointHit_ = false;
  uint16_t breakpointAddress_ = 0;
  bool paused_ = false;
  bool skipBreakpointOnce_ = false;

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

  // Temp breakpoint for step over / step out
  uint16_t tempBreakpoint_ = 0;
  bool tempBreakpointActive_ = false;
  bool tempBreakpointHit_ = false;

  // Watchpoints
  struct Watchpoint {
    uint16_t startAddr;
    uint16_t endAddr;
    WatchpointType type;
    bool enabled;
  };
  std::vector<Watchpoint> watchpoints_;
  bool watchpointsActive_ = false;
  bool watchpointHit_ = false;
  uint16_t watchpointAddress_ = 0;
  uint8_t watchpointValue_ = 0;
  bool watchpointIsWrite_ = false;

  // Beam breakpoints
  struct BeamBreakpoint {
    int16_t scanline;       // -1 = any
    int16_t hPos;           // -1 = any (raw 0-64)
    bool enabled;
    int32_t id;
    uint64_t lastFireFrame;    // per-breakpoint re-fire prevention
    int16_t lastFireScanline;  // for wildcard-scanline breakpoints (fire once per scanline)
  };
  std::vector<BeamBreakpoint> beamBreakpoints_;
  int32_t beamBreakNextId_ = 1;
  static constexpr size_t MAX_BEAM_BREAKPOINTS = 16;
  bool beamBreakHit_ = false;
  int32_t beamBreakHitId_ = -1;
  int16_t beamBreakHitScanline_ = -1;  // Scanline where break occurred (for display)
  int16_t beamBreakHitHPos_ = -1;      // hPos where break occurred (for display)

  // Watchpoint callback for MMU
  void onWatchpointRead(uint16_t address, uint8_t value);
  void onWatchpointWrite(uint16_t address, uint8_t value);

  // Trace log
  std::vector<TraceEntry> traceBuffer_;
  size_t traceHead_ = 0;
  size_t traceCount_ = 0;
  bool traceEnabled_ = false;

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
};

} // namespace a2e
