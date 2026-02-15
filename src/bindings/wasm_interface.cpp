/*
 * wasm_interface.cpp - WebAssembly binding layer exposing the emulator API to JavaScript
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "../core/emulator.hpp"
#include "../core/disassembler/disassembler.hpp"
#include "../core/assembler/assembler.hpp"
#include "../core/debug/condition_evaluator.hpp"
#include "../core/filesystem/dos33.hpp"
#include "../core/filesystem/prodos.hpp"
#include "../core/filesystem/pascal.hpp"
#include "../core/basic/basic_detokenizer.hpp"
#include "../core/basic/basic_tokenizer.hpp"
#include "../core/input/keyboard.hpp"
#include <cstdlib>
#include <cstring>
#include <emscripten.h>

// Global emulator instance
static a2e::Emulator *g_emulator = nullptr;

// Helper macros to reduce repetitive null checks
#define REQUIRE_EMULATOR() do { if (!g_emulator) return; } while(0)
#define REQUIRE_EMULATOR_OR(default_val) do { if (!g_emulator) return (default_val); } while(0)
#define REQUIRE_MOCKINGBOARD() do { if (!g_emulator || !g_emulator->getMockingboardPtr()) return; } while(0)
#define REQUIRE_MOCKINGBOARD_OR(default_val) do { if (!g_emulator || !g_emulator->getMockingboardPtr()) return (default_val); } while(0)
#define REQUIRE_DISK() do { if (!g_emulator || !g_emulator->getDiskPtr()) return; } while(0)
#define REQUIRE_DISK_OR(default_val) do { if (!g_emulator || !g_emulator->getDiskPtr()) return (default_val); } while(0)

extern "C" {

EMSCRIPTEN_KEEPALIVE
void init() {
  if (!g_emulator) {
    g_emulator = new a2e::Emulator();
    g_emulator->init();
  }
}

EMSCRIPTEN_KEEPALIVE
void reset() {
  REQUIRE_EMULATOR();
  g_emulator->reset();
}

EMSCRIPTEN_KEEPALIVE
void warmReset() {
  REQUIRE_EMULATOR();
  g_emulator->warmReset();
}

EMSCRIPTEN_KEEPALIVE
void runCycles(int cycles) {
  REQUIRE_EMULATOR();
  g_emulator->runCycles(cycles);
}

EMSCRIPTEN_KEEPALIVE
int generateStereoAudioSamples(float *buffer, int sampleCount) {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->generateStereoAudioSamples(buffer, sampleCount);
}

EMSCRIPTEN_KEEPALIVE
void setAudioVolume(float volume) {
  REQUIRE_EMULATOR();
  g_emulator->getAudio().setVolume(volume);
}

EMSCRIPTEN_KEEPALIVE
void setAudioMuted(bool muted) {
  REQUIRE_EMULATOR();
  g_emulator->getAudio().setMuted(muted);
}

EMSCRIPTEN_KEEPALIVE
int consumeFrameSamples() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->consumeFrameSamples();
}

EMSCRIPTEN_KEEPALIVE
uint8_t *getFramebuffer() {
  REQUIRE_EMULATOR_OR(nullptr);
  return const_cast<uint8_t *>(g_emulator->getFramebuffer());
}

EMSCRIPTEN_KEEPALIVE
int getFramebufferSize() { return a2e::FRAMEBUFFER_SIZE; }

EMSCRIPTEN_KEEPALIVE
void forceRenderFrame() {
  REQUIRE_EMULATOR();
  g_emulator->getVideo().forceRenderFrame();
}

EMSCRIPTEN_KEEPALIVE
bool isFrameReady() {
  REQUIRE_EMULATOR_OR(false);
  bool ready = g_emulator->isFrameReady();
  if (ready) {
    g_emulator->clearFrameReady();
  }
  return ready;
}

EMSCRIPTEN_KEEPALIVE
void keyDown(int keycode) {
  REQUIRE_EMULATOR();
  g_emulator->keyDown(keycode);
}

EMSCRIPTEN_KEEPALIVE
void keyUp(int keycode) {
  REQUIRE_EMULATOR();
  g_emulator->keyUp(keycode);
}

EMSCRIPTEN_KEEPALIVE
int handleRawKeyDown(int browserKeycode, bool shift, bool ctrl, bool alt,
                     bool meta, bool capsLock) {
  REQUIRE_EMULATOR_OR(-1);
  return g_emulator->handleRawKeyDown(browserKeycode, shift, ctrl, alt, meta,
                                      capsLock);
}

EMSCRIPTEN_KEEPALIVE
void handleRawKeyUp(int browserKeycode, bool shift, bool ctrl, bool alt,
                    bool meta) {
  REQUIRE_EMULATOR();
  g_emulator->handleRawKeyUp(browserKeycode, shift, ctrl, alt, meta);
}

EMSCRIPTEN_KEEPALIVE
int charToAppleKey(int charCode) {
  return a2e::charToAppleKey(charCode);
}

EMSCRIPTEN_KEEPALIVE
void setButton(int button, bool pressed) {
  REQUIRE_EMULATOR();
  g_emulator->setButton(button, pressed);
}

EMSCRIPTEN_KEEPALIVE
void setPaddleValue(int paddle, int value) {
  REQUIRE_EMULATOR();
  g_emulator->setPaddleValue(paddle, value);
}

EMSCRIPTEN_KEEPALIVE
int getPaddleValue(int paddle) {
  REQUIRE_EMULATOR_OR(128);
  return g_emulator->getPaddleValue(paddle);
}

EMSCRIPTEN_KEEPALIVE
bool isKeyboardReady() {
  REQUIRE_EMULATOR_OR(true);
  return g_emulator->isKeyboardReady();
}

EMSCRIPTEN_KEEPALIVE
void setSpeedMultiplier(int multiplier) {
  REQUIRE_EMULATOR();
  g_emulator->setSpeedMultiplier(multiplier);
}

EMSCRIPTEN_KEEPALIVE
int getSpeedMultiplier() {
  REQUIRE_EMULATOR_OR(1);
  return g_emulator->getSpeedMultiplier();
}

EMSCRIPTEN_KEEPALIVE
bool insertDisk(int drive, uint8_t *data, int size, const char *filename) {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->insertDisk(drive, data, size, filename);
}

EMSCRIPTEN_KEEPALIVE
bool insertBlankDisk(int drive) {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->insertBlankDisk(drive);
}

EMSCRIPTEN_KEEPALIVE
void ejectDisk(int drive) {
  REQUIRE_EMULATOR();
  g_emulator->ejectDisk(drive);
}

EMSCRIPTEN_KEEPALIVE
uint8_t *getDiskData(int drive, size_t *size) {
  if (!g_emulator) { *size = 0; return nullptr; }
  return const_cast<uint8_t *>(g_emulator->exportDiskData(drive, size));
}

EMSCRIPTEN_KEEPALIVE
const uint8_t *getDiskSectorData(int drive, size_t *size) {
  if (!g_emulator) { *size = 0; return nullptr; }
  return g_emulator->getDiskData(drive, size);
}

// ============================================================================
// Beam Position
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int getFrameCycle() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getFrameCycle();
}

EMSCRIPTEN_KEEPALIVE
int getBeamScanline() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBeamScanline();
}

EMSCRIPTEN_KEEPALIVE
int getBeamHPos() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBeamHPos();
}

EMSCRIPTEN_KEEPALIVE
int getBeamColumn() {
  REQUIRE_EMULATOR_OR(-1);
  return g_emulator->getBeamColumn();
}

EMSCRIPTEN_KEEPALIVE
bool isInVBL() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isInVBL();
}

EMSCRIPTEN_KEEPALIVE
bool isInHBLANK() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isInHBLANK();
}

// ============================================================================
// Step Over / Step Out
// ============================================================================

EMSCRIPTEN_KEEPALIVE
uint16_t stepOver() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->stepOver();
}

EMSCRIPTEN_KEEPALIVE
uint16_t stepOut() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->stepOut();
}

EMSCRIPTEN_KEEPALIVE
void clearTempBreakpoint() {
  REQUIRE_EMULATOR();
  g_emulator->clearTempBreakpoint();
}

EMSCRIPTEN_KEEPALIVE
bool isTempBreakpointHit() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isTempBreakpointHit();
}

EMSCRIPTEN_KEEPALIVE
void addBreakpoint(uint16_t address) {
  REQUIRE_EMULATOR();
  g_emulator->addBreakpoint(address);
}

EMSCRIPTEN_KEEPALIVE
void removeBreakpoint(uint16_t address) {
  REQUIRE_EMULATOR();
  g_emulator->removeBreakpoint(address);
}

EMSCRIPTEN_KEEPALIVE
void enableBreakpoint(uint16_t address, bool enabled) {
  REQUIRE_EMULATOR();
  g_emulator->enableBreakpoint(address, enabled);
}

EMSCRIPTEN_KEEPALIVE
bool isBreakpointHit() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isBreakpointHit();
}

EMSCRIPTEN_KEEPALIVE
uint16_t getBreakpointAddress() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBreakpointAddress();
}

// ============================================================================
// BASIC Breakpoints
// ============================================================================

EMSCRIPTEN_KEEPALIVE
void addBasicBreakpoint(uint16_t lineNumber, int statementIndex) {
  REQUIRE_EMULATOR();
  g_emulator->addBasicBreakpoint(lineNumber, statementIndex);
}

EMSCRIPTEN_KEEPALIVE
void removeBasicBreakpoint(uint16_t lineNumber, int statementIndex) {
  REQUIRE_EMULATOR();
  g_emulator->removeBasicBreakpoint(lineNumber, statementIndex);
}

EMSCRIPTEN_KEEPALIVE
void clearBasicBreakpoints() {
  REQUIRE_EMULATOR();
  g_emulator->clearBasicBreakpoints();
}

EMSCRIPTEN_KEEPALIVE
void clearBasicBreakpointHit() {
  REQUIRE_EMULATOR();
  g_emulator->clearBasicBreakpointHit();
}

EMSCRIPTEN_KEEPALIVE
void addBasicConditionRule(int id, const char* expression) {
  REQUIRE_EMULATOR();
  g_emulator->addBasicConditionRule(id, expression);
}

EMSCRIPTEN_KEEPALIVE
void removeBasicConditionRule(int id) {
  REQUIRE_EMULATOR();
  g_emulator->removeBasicConditionRule(id);
}

EMSCRIPTEN_KEEPALIVE
void clearBasicConditionRules() {
  REQUIRE_EMULATOR();
  g_emulator->clearBasicConditionRules();
}

EMSCRIPTEN_KEEPALIVE
int getBasicConditionRuleHitId() {
  REQUIRE_EMULATOR_OR(-1);
  return g_emulator->getBasicConditionRuleHitId();
}

EMSCRIPTEN_KEEPALIVE
bool hasBasicBreakpoints() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->hasBasicBreakpoints();
}

EMSCRIPTEN_KEEPALIVE
bool isBasicBreakpointHit() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isBasicBreakpointHit();
}

EMSCRIPTEN_KEEPALIVE
uint16_t getBasicBreakLine() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBasicBreakLine();
}

EMSCRIPTEN_KEEPALIVE
bool isBasicProgramRunning() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isBasicProgramRunning();
}

EMSCRIPTEN_KEEPALIVE
bool isBasicErrorHit() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isBasicErrorHit();
}

EMSCRIPTEN_KEEPALIVE
uint16_t getBasicErrorLine() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBasicErrorLine();
}

EMSCRIPTEN_KEEPALIVE
uint16_t getBasicErrorTxtptr() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBasicErrorTxtptr();
}

EMSCRIPTEN_KEEPALIVE
uint8_t getBasicErrorCode() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBasicErrorCode();
}

EMSCRIPTEN_KEEPALIVE
void clearBasicError() {
  REQUIRE_EMULATOR();
  g_emulator->clearBasicError();
}

EMSCRIPTEN_KEEPALIVE
void stepBasicLine() {
  REQUIRE_EMULATOR();
  g_emulator->stepBasicLine();
}

EMSCRIPTEN_KEEPALIVE
void stepBasicStatement() {
  REQUIRE_EMULATOR();
  g_emulator->stepBasicStatement();
}

EMSCRIPTEN_KEEPALIVE
uint16_t getBasicTxtptr() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBasicTxtptr();
}

EMSCRIPTEN_KEEPALIVE
int getBasicStatementIndex() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBasicStatementIndex();
}

// Debug function to get BASIC memory state with detailed line info
// Uses readRAM to bypass ALTZP - BASIC always uses main RAM for zero page
EMSCRIPTEN_KEEPALIVE
void getBasicDebugInfo(uint16_t* txttab, uint16_t* vartab, uint16_t* curlin, uint16_t* txtptr) {
  if (!g_emulator) return;
  auto& mmu = g_emulator->getMMU();
  *txttab = mmu.readRAM(0x67, false) | (mmu.readRAM(0x68, false) << 8);
  *vartab = mmu.readRAM(0x69, false) | (mmu.readRAM(0x6A, false) << 8);
  *curlin = mmu.readRAM(0x75, false) | (mmu.readRAM(0x76, false) << 8);
  *txtptr = mmu.readRAM(0xB8, false) | (mmu.readRAM(0xB9, false) << 8);
}

// BASIC line heat map
EMSCRIPTEN_KEEPALIVE
void setBasicHeatMapEnabled(bool enabled) {
  REQUIRE_EMULATOR();
  g_emulator->setBasicHeatMapEnabled(enabled);
}

EMSCRIPTEN_KEEPALIVE
void clearBasicHeatMap() {
  REQUIRE_EMULATOR();
  g_emulator->clearBasicHeatMap();
}

EMSCRIPTEN_KEEPALIVE
int getBasicHeatMapSize() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBasicHeatMapSize();
}

EMSCRIPTEN_KEEPALIVE
int getBasicHeatMapData(uint16_t* lines, uint32_t* counts, int maxEntries) {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getBasicHeatMapData(lines, counts, maxEntries);
}

// Debug function to dump bytes around TXTPTR to see what's there
EMSCRIPTEN_KEEPALIVE
void getBasicLineBytes(uint8_t* buffer, int* lineStart, int* colonCount) {
  if (!g_emulator) return;
  auto& mmu = g_emulator->getMMU();

  uint16_t txttab = mmu.readRAM(0x67, false) | (mmu.readRAM(0x68, false) << 8);
  uint16_t curlin = mmu.readRAM(0x75, false) | (mmu.readRAM(0x76, false) << 8);
  uint16_t txtptr = mmu.readRAM(0xB8, false) | (mmu.readRAM(0xB9, false) << 8);

  // Find current line
  uint16_t addr = txttab;
  uint16_t foundLineStart = 0;

  while (addr < 0xC000) {
    uint16_t nextPtr = mmu.readRAM(addr, false) | (mmu.readRAM(addr + 1, false) << 8);
    if (nextPtr == 0) break;

    uint16_t lineNum = mmu.readRAM(addr + 2, false) | (mmu.readRAM(addr + 3, false) << 8);
    if (lineNum == curlin) {
      foundLineStart = addr + 4;
      break;
    }
    addr = nextPtr;
  }

  *lineStart = foundLineStart;

  // Count colons from line start to TXTPTR
  int count = 0;
  if (foundLineStart > 0 && txtptr > foundLineStart) {
    for (uint16_t a = foundLineStart; a < txtptr && a < foundLineStart + 64; a++) {
      uint8_t byte = mmu.readRAM(a, false);
      if (byte == 0) break;
      if (byte == 0x3A) count++;  // Colon
    }
  }
  *colonCount = count;

  // Copy 32 bytes starting from line start (or txtptr if lineStart is 0)
  uint16_t dumpStart = foundLineStart > 0 ? foundLineStart : txtptr;
  for (int i = 0; i < 32; i++) {
    buffer[i] = mmu.readRAM(dumpStart + i, false);
  }
}

EMSCRIPTEN_KEEPALIVE
uint16_t getPC() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getPC();
}

EMSCRIPTEN_KEEPALIVE
uint8_t getSP() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getSP();
}

EMSCRIPTEN_KEEPALIVE
uint8_t getA() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getA();
}

EMSCRIPTEN_KEEPALIVE
uint8_t getX() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getX();
}

EMSCRIPTEN_KEEPALIVE
uint8_t getY() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getY();
}

EMSCRIPTEN_KEEPALIVE
uint8_t getP() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getP();
}

EMSCRIPTEN_KEEPALIVE
uint64_t getTotalCycles() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getTotalCycles();
}

EMSCRIPTEN_KEEPALIVE
bool isIRQPending() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isIRQPending();
}

EMSCRIPTEN_KEEPALIVE
bool isNMIPending() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isNMIPending();
}

EMSCRIPTEN_KEEPALIVE
bool isNMIEdge() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isNMIEdge();
}

// CPU register setters (for debugger editing)
EMSCRIPTEN_KEEPALIVE
void setRegA(uint8_t value) {
  REQUIRE_EMULATOR();
  g_emulator->setA(value);
}

EMSCRIPTEN_KEEPALIVE
void setRegX(uint8_t value) {
  REQUIRE_EMULATOR();
  g_emulator->setX(value);
}

EMSCRIPTEN_KEEPALIVE
void setRegY(uint8_t value) {
  REQUIRE_EMULATOR();
  g_emulator->setY(value);
}

EMSCRIPTEN_KEEPALIVE
void setRegSP(uint8_t value) {
  REQUIRE_EMULATOR();
  g_emulator->setSP(value);
}

EMSCRIPTEN_KEEPALIVE
void setRegPC(uint16_t value) {
  REQUIRE_EMULATOR();
  g_emulator->setPC(value);
}

EMSCRIPTEN_KEEPALIVE
void setRegP(uint8_t value) {
  REQUIRE_EMULATOR();
  g_emulator->setP(value);
}

EMSCRIPTEN_KEEPALIVE
bool isPaused() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isPaused();
}

EMSCRIPTEN_KEEPALIVE
void setPaused(bool paused) {
  REQUIRE_EMULATOR();
  g_emulator->setPaused(paused);
}

EMSCRIPTEN_KEEPALIVE
void stepInstruction() {
  REQUIRE_EMULATOR();
  g_emulator->stepInstruction();
}

EMSCRIPTEN_KEEPALIVE
uint8_t readMemory(uint16_t address) {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->readMemory(address);
}

EMSCRIPTEN_KEEPALIVE
uint8_t peekMemory(uint16_t address) {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->peekMemory(address);
}

EMSCRIPTEN_KEEPALIVE
uint8_t readMainRAM(uint16_t address) {
  // Read directly from main RAM, bypassing ALTZP and other switches
  // Useful for reading BASIC zero page variables which are always in main RAM
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getMMU().readRAM(address, false);
}

EMSCRIPTEN_KEEPALIVE
void writeMemory(uint16_t address, uint8_t value) {
  REQUIRE_EMULATOR();
  g_emulator->writeMemory(address, value);
}

EMSCRIPTEN_KEEPALIVE
const char *disassembleAt(uint16_t address) {
  REQUIRE_EMULATOR_OR("");
  return g_emulator->disassembleAt(address);
}

EMSCRIPTEN_KEEPALIVE
uint32_t getSoftSwitchState() {
  REQUIRE_EMULATOR_OR(0);
  return static_cast<uint32_t>(g_emulator->getSoftSwitchState() & 0xFFFFFFFF);
}

EMSCRIPTEN_KEEPALIVE
uint32_t getSoftSwitchStateHigh() {
  REQUIRE_EMULATOR_OR(0);
  return static_cast<uint32_t>(g_emulator->getSoftSwitchState() >> 32);
}

// Screen text extraction
EMSCRIPTEN_KEEPALIVE
int screenCodeToAscii(uint8_t code) {
  return a2e::Emulator::screenCodeToAscii(code);
}

EMSCRIPTEN_KEEPALIVE
const char* readScreenText(int startRow, int startCol, int endRow, int endCol) {
  REQUIRE_EMULATOR_OR("");
  return g_emulator->readScreenText(startRow, startCol, endRow, endCol);
}

// Disk controller state for debugging
EMSCRIPTEN_KEEPALIVE
int getDiskTrack(int drive) {
  REQUIRE_DISK_OR(0);
  auto &disk = g_emulator->getDisk();
  if (disk.hasDisk(drive)) {
    const auto *image = disk.getDiskImage(drive);
    if (image) {
      return image->getTrack();
    }
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int getDiskPhase(int drive) {
  REQUIRE_DISK_OR(0);
  (void)drive; // Phase states are controller-wide
  return g_emulator->getDisk().getPhaseStates();
}

EMSCRIPTEN_KEEPALIVE
bool getDiskMotorOn(int drive) {
  REQUIRE_DISK_OR(false);
  (void)drive; // Motor state is controller-wide
  return g_emulator->getDisk().isMotorOn();
}

EMSCRIPTEN_KEEPALIVE
void stopDiskMotor() {
  REQUIRE_DISK();
  g_emulator->getDisk().stopMotor();
}

EMSCRIPTEN_KEEPALIVE
bool getDiskWriteMode(int drive) {
  REQUIRE_DISK_OR(false);
  (void)drive; // Write mode (Q7) is controller-wide
  return g_emulator->getDisk().getQ7();
}

EMSCRIPTEN_KEEPALIVE
int getDiskHeadPosition(int drive) {
  REQUIRE_DISK_OR(0);
  auto &disk = g_emulator->getDisk();
  if (disk.hasDisk(drive)) {
    const auto *image = disk.getDiskImage(drive);
    if (image) {
      return image->getQuarterTrack();
    }
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int getSelectedDrive() {
  REQUIRE_DISK_OR(0);
  return g_emulator->getDisk().getSelectedDrive();
}

EMSCRIPTEN_KEEPALIVE
bool isDiskInserted(int drive) {
  REQUIRE_DISK_OR(false);
  return g_emulator->getDisk().hasDisk(drive);
}

EMSCRIPTEN_KEEPALIVE
uint8_t getLastDiskByte() {
  REQUIRE_DISK_OR(0);
  return g_emulator->getDisk().getDataLatch();
}

EMSCRIPTEN_KEEPALIVE
uint8_t getTrackNibble(int drive, int track, int position) {
  REQUIRE_DISK_OR(0);
  if (g_emulator->getDisk().hasDisk(drive)) {
    const auto *image = g_emulator->getDisk().getDiskImage(drive);
    if (image) {
      return image->getNibbleAt(track, position);
    }
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int getTrackNibbleCount(int drive, int track) {
  REQUIRE_DISK_OR(0);
  if (g_emulator->getDisk().hasDisk(drive)) {
    const auto *image = g_emulator->getDisk().getDiskImage(drive);
    if (image) {
      return image->getTrackNibbleCount(track);
    }
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
size_t getCurrentNibblePosition(int drive) {
  REQUIRE_DISK_OR(0);
  if (g_emulator->getDisk().hasDisk(drive)) {
    const auto *image = g_emulator->getDisk().getDiskImage(drive);
    if (image) {
      return image->getCurrentNibblePosition();
    }
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
bool isDiskModified(int drive) {
  REQUIRE_DISK_OR(false);
  if (g_emulator->getDisk().hasDisk(drive)) {
    const auto *image = g_emulator->getDisk().getDiskImage(drive);
    if (image) {
      return image->isModified();
    }
  }
  return false;
}

EMSCRIPTEN_KEEPALIVE
const char *getDiskFilename(int drive) {
  REQUIRE_EMULATOR_OR(nullptr);
  return g_emulator->getDiskFilename(drive);
}

// Memory tracking for debugger heat map
EMSCRIPTEN_KEEPALIVE
void enableMemoryTracking(bool enable) {
  REQUIRE_EMULATOR();
  g_emulator->getMMU().enableTracking(enable);
}

EMSCRIPTEN_KEEPALIVE
void clearMemoryTracking() {
  REQUIRE_EMULATOR();
  g_emulator->getMMU().clearTracking();
}

EMSCRIPTEN_KEEPALIVE
void decayMemoryTracking(uint8_t amount) {
  REQUIRE_EMULATOR();
  g_emulator->getMMU().decayTracking(amount);
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* getMemoryReadCounts() {
  REQUIRE_EMULATOR_OR(nullptr);
  return g_emulator->getMMU().getReadCounts();
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* getMemoryWriteCounts() {
  REQUIRE_EMULATOR_OR(nullptr);
  return g_emulator->getMMU().getWriteCounts();
}

// Direct memory array access for heat map visualization
EMSCRIPTEN_KEEPALIVE
const uint8_t* getMainRAM() {
  REQUIRE_EMULATOR_OR(nullptr);
  return g_emulator->getMMU().getMainRAM();
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* getAuxRAM() {
  REQUIRE_EMULATOR_OR(nullptr);
  return g_emulator->getMMU().getAuxRAM();
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* getSystemROM() {
  REQUIRE_EMULATOR_OR(nullptr);
  return g_emulator->getMMU().getSystemROM();
}

// Read auxiliary memory directly (for 80-column text selection)
EMSCRIPTEN_KEEPALIVE
uint8_t peekAuxMemory(uint16_t address) {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getMMU().peekAux(address);
}

// UK/US character set switch (like the physical switch on UK Apple IIe)
EMSCRIPTEN_KEEPALIVE
void setUKCharacterSet(bool uk) {
  REQUIRE_EMULATOR();
  g_emulator->getVideo().setUKCharacterSet(uk);
}

EMSCRIPTEN_KEEPALIVE
bool isUKCharacterSet() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->getVideo().isUKCharacterSet();
}

// Monochrome display mode (bypasses NTSC artifact coloring)
EMSCRIPTEN_KEEPALIVE
void setMonochrome(bool mono) {
  REQUIRE_EMULATOR();
  g_emulator->getVideo().setMonochrome(mono);
}

EMSCRIPTEN_KEEPALIVE
bool isMonochrome() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->getVideo().isMonochrome();
}

// ============================================================================
// State Serialization
// ============================================================================

EMSCRIPTEN_KEEPALIVE
uint8_t *exportState(size_t *size) {
  if (!g_emulator) { *size = 0; return nullptr; }
  return const_cast<uint8_t *>(g_emulator->exportState(size));
}

EMSCRIPTEN_KEEPALIVE
bool importState(const uint8_t *data, size_t size) {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->importState(data, size);
}

// ============================================================================
// Standalone Disassembler (for file browser, external tools)
// ============================================================================

// Static buffer for disassembly result
static a2e::DisasmResult g_disasmResult;

EMSCRIPTEN_KEEPALIVE
uint32_t disassembleRawData(const uint8_t *data, size_t size,
                            uint16_t baseAddress) {
  g_disasmResult = a2e::disassembleBlock(data, size, baseAddress);
  return static_cast<uint32_t>(g_disasmResult.instructions.size());
}

EMSCRIPTEN_KEEPALIVE
const a2e::DisasmInstruction *getDisasmInstructions() {
  if (g_disasmResult.instructions.empty()) {
    return nullptr;
  }
  return g_disasmResult.instructions.data();
}

EMSCRIPTEN_KEEPALIVE
int getDisasmInstructionLength(uint8_t opcode) {
  return a2e::getInstructionLength(opcode);
}

EMSCRIPTEN_KEEPALIVE
uint32_t disassembleWithFlowAnalysis(const uint8_t *data, size_t size,
                                      uint16_t baseAddress) {
  g_disasmResult = a2e::disassembleWithFlowAnalysis(data, size, baseAddress);
  return static_cast<uint32_t>(g_disasmResult.instructions.size());
}

EMSCRIPTEN_KEEPALIVE
uint32_t disassembleWithFlowAnalysisMultiEntry(const uint8_t *data, size_t size,
                                                uint16_t baseAddress,
                                                const uint16_t *entryPoints,
                                                size_t entryCount) {
  std::vector<uint16_t> entries(entryPoints, entryPoints + entryCount);
  g_disasmResult = a2e::disassembleWithFlowAnalysis(data, size, baseAddress, entries);
  return static_cast<uint32_t>(g_disasmResult.instructions.size());
}

// ============================================================================
// Mockingboard Debug State
// ============================================================================

EMSCRIPTEN_KEEPALIVE
bool isMockingboardEnabled() {
  REQUIRE_MOCKINGBOARD_OR(false);
  return g_emulator->getMockingboard().isEnabled();
}

EMSCRIPTEN_KEEPALIVE
uint8_t getMockingboardPSGRegister(int psg, int reg) {
  REQUIRE_MOCKINGBOARD_OR(0);
  if (reg < 0 || reg >= 16) return 0;
  if (psg == 0) {
    return g_emulator->getMockingboard().getPSG1().getRegister(reg);
  } else if (psg == 1) {
    return g_emulator->getMockingboard().getPSG2().getRegister(reg);
  }
  return 0;
}

// Get all 16 PSG registers as a packed structure for efficiency
// Returns pointer to static buffer with 16 bytes
static uint8_t g_psgRegisters[16];

EMSCRIPTEN_KEEPALIVE
const uint8_t* getMockingboardPSGRegisters(int psg) {
  REQUIRE_MOCKINGBOARD_OR(nullptr);
  const auto& psgChip = (psg == 0)
    ? g_emulator->getMockingboard().getPSG1()
    : g_emulator->getMockingboard().getPSG2();
  for (int i = 0; i < 16; i++) {
    g_psgRegisters[i] = psgChip.getRegister(i);
  }
  return g_psgRegisters;
}

EMSCRIPTEN_KEEPALIVE
bool getMockingboardVIAIRQ(int via) {
  REQUIRE_MOCKINGBOARD_OR(false);
  if (via == 0) {
    return g_emulator->getMockingboard().getVIA1().isIRQActive();
  } else if (via == 1) {
    return g_emulator->getMockingboard().getVIA2().isIRQActive();
  }
  return false;
}

// Get VIA port registers for debugging
// reg: 0=ORA, 1=ORB, 2=DDRA, 3=DDRB
EMSCRIPTEN_KEEPALIVE
uint8_t getMockingboardVIAPort(int via, int reg) {
  REQUIRE_MOCKINGBOARD_OR(0);
  const auto& viaChip = (via == 0)
      ? g_emulator->getMockingboard().getVIA1()
      : g_emulator->getMockingboard().getVIA2();
  switch (reg) {
    case 0: return viaChip.getORA();
    case 1: return viaChip.getORB();
    case 2: return viaChip.getDDRA();
    case 3: return viaChip.getDDRB();
  }
  return 0;
}

// Get PSG write debug info
// info: 0=writeCount, 1=lastWriteReg, 2=lastWriteVal, 3=currentRegister
EMSCRIPTEN_KEEPALIVE
uint32_t getMockingboardPSGWriteInfo(int psg, int info) {
  REQUIRE_MOCKINGBOARD_OR(0);
  const auto& psgChip = (psg == 0)
      ? g_emulator->getMockingboard().getPSG1()
      : g_emulator->getMockingboard().getPSG2();
  switch (info) {
    case 0: return psgChip.getWriteCount();
    case 1: return psgChip.getLastWriteReg();
    case 2: return psgChip.getLastWriteVal();
    case 3: return psgChip.getCurrentRegister();
  }
  return 0;
}

// Get VIA timer debug info
// info: 0=T1Counter, 1=T1Latch, 2=T1Running, 3=T1Fired, 4=ACR, 5=IFR, 6=IER
EMSCRIPTEN_KEEPALIVE
uint32_t getMockingboardVIATimerInfo(int via, int info) {
  REQUIRE_MOCKINGBOARD_OR(0);
  const auto& viaChip = (via == 0)
      ? g_emulator->getMockingboard().getVIA1()
      : g_emulator->getMockingboard().getVIA2();
  switch (info) {
    case 0: return viaChip.getT1Counter();
    case 1: return viaChip.getT1Latch();
    case 2: return viaChip.isT1Running() ? 1 : 0;
    case 3: return viaChip.hasT1Fired() ? 1 : 0;
    case 4: return viaChip.getACR();
    case 5: return viaChip.getIFR();
    case 6: return viaChip.getIER();
  }
  return 0;
}

// Enable/disable console debug logging for Mockingboard PSG writes
EMSCRIPTEN_KEEPALIVE
void setMockingboardDebugLogging(bool enabled) {
  REQUIRE_MOCKINGBOARD();
  g_emulator->getMockingboard().setDebugLogging(enabled);
}

// Mute/unmute a specific channel on a PSG
// psg: 0 or 1 (PSG1 or PSG2)
// channel: 0, 1, or 2 (A, B, C)
// muted: true to mute, false to unmute
EMSCRIPTEN_KEEPALIVE
void setMockingboardChannelMute(int psg, int channel, bool muted) {
  REQUIRE_MOCKINGBOARD();
  auto& psgChip = (psg == 0)
      ? g_emulator->getMockingboard().getPSG1()
      : g_emulator->getMockingboard().getPSG2();
  psgChip.setChannelMute(channel, muted);
}

// Check if a channel is muted
EMSCRIPTEN_KEEPALIVE
bool getMockingboardChannelMute(int psg, int channel) {
  REQUIRE_MOCKINGBOARD_OR(false);
  const auto& psgChip = (psg == 0)
      ? g_emulator->getMockingboard().getPSG1()
      : g_emulator->getMockingboard().getPSG2();
  return psgChip.isChannelMuted(channel);
}

// Generate waveform samples from a PSG channel for visualization
// psg: 0 or 1 (PSG1 or PSG2)
// channel: 0, 1, or 2 (A, B, C) - use -1 for combined output
// buffer: float array to fill with samples
// count: number of samples to generate
// Returns actual number of samples generated
EMSCRIPTEN_KEEPALIVE
int getMockingboardWaveform(int psg, int channel, float* buffer, int count) {
  REQUIRE_MOCKINGBOARD_OR(0);
  if (!buffer || count <= 0 || count > 1024) return 0;

  const int SAMPLE_RATE = 48000;
  auto& psgChip = (psg == 0)
      ? g_emulator->getMockingboard().getPSG1()
      : g_emulator->getMockingboard().getPSG2();

  // Create a copy of the PSG to generate visualization samples
  // without affecting the actual audio state
  a2e::AY8910 psgCopy = psgChip;

  if (channel >= 0 && channel < 3) {
    psgCopy.generateChannelSamples(buffer, count, SAMPLE_RATE, channel);
  } else {
    psgCopy.generateSamples(buffer, count, SAMPLE_RATE);
  }

  return count;
}

// ============================================================================
// Mouse Input
// ============================================================================

EMSCRIPTEN_KEEPALIVE
void mouseMove(int dx, int dy) {
  REQUIRE_EMULATOR();
  g_emulator->mouseMove(dx, dy);
}

EMSCRIPTEN_KEEPALIVE
void mouseButton(bool pressed) {
  REQUIRE_EMULATOR();
  g_emulator->mouseButton(pressed);
}

// ============================================================================
// Mouse Card Debug
// ============================================================================

// Returns whether a mouse card is currently installed
EMSCRIPTEN_KEEPALIVE
bool isMouseCardInstalled() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->getMouseCard() != nullptr;
}

// Get mouse card state field
// field: 0=slotNum, 1=mouseX, 2=mouseY, 3=button, 4=moved, 5=buttonChanged,
//        6=clampMinX, 7=clampMaxX, 8=clampMinY, 9=clampMaxY,
//        10=irqActive, 11=vblPending, 12=movePending, 13=buttonPending,
//        14=wasInVBL, 15=mode, 16=lastCommand, 17=responseState
EMSCRIPTEN_KEEPALIVE
int32_t getMouseCardState(int field) {
  REQUIRE_EMULATOR_OR(0);
  auto* mouse = g_emulator->getMouseCard();
  if (!mouse) return 0;
  switch (field) {
    case 0: return mouse->getSlotNumber();
    case 1: return mouse->getMouseX();
    case 2: return mouse->getMouseY();
    case 3: return mouse->getMouseButton() ? 1 : 0;
    case 4: return mouse->getMoved() ? 1 : 0;
    case 5: return mouse->getButtonChanged() ? 1 : 0;
    case 6: return mouse->getClampMinX();
    case 7: return mouse->getClampMaxX();
    case 8: return mouse->getClampMinY();
    case 9: return mouse->getClampMaxY();
    case 10: return mouse->isIRQActive() ? 1 : 0;
    case 11: return mouse->getVBLInterruptPending() ? 1 : 0;
    case 12: return mouse->getMoveInterruptPending() ? 1 : 0;
    case 13: return mouse->getButtonInterruptPending() ? 1 : 0;
    case 14: return mouse->getWasInVBL() ? 1 : 0;
    case 15: return mouse->getMode();
    case 16: return mouse->getLastCommand();
    case 17: return mouse->getResponseState();
  }
  return 0;
}

// Get mouse card PIA register
// reg: 0=DDRA, 1=DDRB, 2=ORA, 3=ORB, 4=IRA, 5=IRB, 6=CRA, 7=CRB
EMSCRIPTEN_KEEPALIVE
uint32_t getMouseCardPIARegister(int reg) {
  REQUIRE_EMULATOR_OR(0);
  auto* mouse = g_emulator->getMouseCard();
  if (!mouse) return 0;
  switch (reg) {
    case 0: return mouse->getDDRA();
    case 1: return mouse->getDDRB();
    case 2: return mouse->getORA();
    case 3: return mouse->getORB();
    case 4: return mouse->getIRA();
    case 5: return mouse->getIRB();
    case 6: return mouse->getCRA();
    case 7: return mouse->getCRB();
  }
  return 0;
}

// ============================================================================
// SmartPort Hard Drive
// ============================================================================

EMSCRIPTEN_KEEPALIVE
bool insertSmartPortImage(int device, uint8_t* data, int size, const char* filename) {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->insertSmartPortImage(device, data, size, filename);
}

EMSCRIPTEN_KEEPALIVE
void ejectSmartPortImage(int device) {
  REQUIRE_EMULATOR();
  g_emulator->ejectSmartPortImage(device);
}

EMSCRIPTEN_KEEPALIVE
bool isSmartPortImageInserted(int device) {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isSmartPortImageInserted(device);
}

EMSCRIPTEN_KEEPALIVE
const char* getSmartPortImageFilename(int device) {
  REQUIRE_EMULATOR_OR(nullptr);
  return g_emulator->getSmartPortImageFilename(device);
}

EMSCRIPTEN_KEEPALIVE
bool isSmartPortImageModified(int device) {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isSmartPortImageModified(device);
}

EMSCRIPTEN_KEEPALIVE
uint8_t* getSmartPortImageData(int device, size_t* size) {
  if (!g_emulator) { *size = 0; return nullptr; }
  return const_cast<uint8_t*>(g_emulator->exportSmartPortImageData(device, size));
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* getSmartPortBlockData(int device, size_t* size) {
  if (!g_emulator) { *size = 0; return nullptr; }
  return g_emulator->getSmartPortBlockData(device, size);
}

EMSCRIPTEN_KEEPALIVE
bool isSmartPortCardInstalled() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isSmartPortCardInstalled();
}

EMSCRIPTEN_KEEPALIVE
bool getSmartPortActivity(int device) {
  REQUIRE_EMULATOR_OR(false);
  auto* card = g_emulator->getSmartPortCard();
  if (!card) return false;
  return card->hasActivity();
}

EMSCRIPTEN_KEEPALIVE
bool getSmartPortActivityWrite(int device) {
  REQUIRE_EMULATOR_OR(false);
  auto* card = g_emulator->getSmartPortCard();
  if (!card) return false;
  return card->isActivityWrite();
}

EMSCRIPTEN_KEEPALIVE
void clearSmartPortActivity() {
  REQUIRE_EMULATOR();
  auto* card = g_emulator->getSmartPortCard();
  if (card) card->clearActivity();
}

// ============================================================================
// Super Serial Card
// ============================================================================

EMSCRIPTEN_KEEPALIVE
void serialReceive(uint8_t byte) {
  REQUIRE_EMULATOR();
  g_emulator->serialReceive(byte);
}

EMSCRIPTEN_KEEPALIVE
bool isSSCInstalled() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isSSCInstalled();
}

EMSCRIPTEN_KEEPALIVE
void setSerialTxCallback() {
  REQUIRE_EMULATOR();
  g_emulator->setSerialTxCallback([](uint8_t byte) {
    EM_ASM({
      if (window.emulator && window.emulator.modem) {
        window.emulator.modem.processTxByte($0);
      } else if (window.emulator && window.emulator.serialManager) {
        window.emulator.serialManager.sendByte($0);
      }
    }, byte);
  });
}

// ============================================================================
// Expansion Slot Management
// ============================================================================

EMSCRIPTEN_KEEPALIVE
const char* getSlotCard(int slot) {
  if (g_emulator) {
    return g_emulator->getSlotCardName(static_cast<uint8_t>(slot));
  }
  return "invalid";
}

EMSCRIPTEN_KEEPALIVE
bool setSlotCard(int slot, const char* cardId) {
  if (g_emulator) {
    return g_emulator->setSlotCard(static_cast<uint8_t>(slot), cardId);
  }
  return false;
}

EMSCRIPTEN_KEEPALIVE
bool isSlotEmpty(int slot) {
  if (g_emulator) {
    return g_emulator->isSlotEmpty(static_cast<uint8_t>(slot));
  }
  return true;
}

// ============================================================================
// Watchpoints
// ============================================================================

EMSCRIPTEN_KEEPALIVE
void addWatchpoint(uint16_t startAddr, uint16_t endAddr, uint8_t type) {
  REQUIRE_EMULATOR();
  g_emulator->addWatchpoint(startAddr, endAddr,
    static_cast<a2e::Emulator::WatchpointType>(type));
}

EMSCRIPTEN_KEEPALIVE
void removeWatchpoint(uint16_t startAddr) {
  REQUIRE_EMULATOR();
  g_emulator->removeWatchpoint(startAddr);
}

EMSCRIPTEN_KEEPALIVE
void clearWatchpoints() {
  REQUIRE_EMULATOR();
  g_emulator->clearWatchpoints();
}

EMSCRIPTEN_KEEPALIVE
bool isWatchpointHit() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isWatchpointHit();
}

EMSCRIPTEN_KEEPALIVE
uint16_t getWatchpointAddress() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getWatchpointAddress();
}

EMSCRIPTEN_KEEPALIVE
uint8_t getWatchpointValue() {
  REQUIRE_EMULATOR_OR(0);
  return g_emulator->getWatchpointValue();
}

EMSCRIPTEN_KEEPALIVE
bool isWatchpointWrite() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isWatchpointWrite();
}

// ============================================================================
// Instruction Trace
// ============================================================================

EMSCRIPTEN_KEEPALIVE
void setTraceEnabled(bool enabled) {
  REQUIRE_EMULATOR();
  g_emulator->setTraceEnabled(enabled);
}

EMSCRIPTEN_KEEPALIVE
void clearTrace() {
  REQUIRE_EMULATOR();
  g_emulator->clearTrace();
}

EMSCRIPTEN_KEEPALIVE
uint32_t getTraceCount() {
  REQUIRE_EMULATOR_OR(0);
  return static_cast<uint32_t>(g_emulator->getTraceCount());
}

EMSCRIPTEN_KEEPALIVE
uint32_t getTraceHead() {
  REQUIRE_EMULATOR_OR(0);
  return static_cast<uint32_t>(g_emulator->getTraceHead());
}

EMSCRIPTEN_KEEPALIVE
const void* getTraceBuffer() {
  REQUIRE_EMULATOR_OR(nullptr);
  return g_emulator->getTraceBuffer();
}

EMSCRIPTEN_KEEPALIVE
uint32_t getTraceCapacity() {
  REQUIRE_EMULATOR_OR(0);
  return static_cast<uint32_t>(g_emulator->getTraceCapacity());
}

// ============================================================================
// Cycle Profiling
// ============================================================================

EMSCRIPTEN_KEEPALIVE
void setProfileEnabled(bool enabled) {
  REQUIRE_EMULATOR();
  g_emulator->setProfileEnabled(enabled);
}

EMSCRIPTEN_KEEPALIVE
void clearProfile() {
  REQUIRE_EMULATOR();
  g_emulator->clearProfile();
}

EMSCRIPTEN_KEEPALIVE
const uint32_t* getProfileCycles() {
  REQUIRE_EMULATOR_OR(nullptr);
  return g_emulator->getProfileCycles();
}

// ============================================================================
// Beam Breakpoints
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int32_t addBeamBreakpoint(int16_t scanline, int16_t hPos) {
  REQUIRE_EMULATOR_OR(-1);
  return g_emulator->addBeamBreakpoint(scanline, hPos);
}

EMSCRIPTEN_KEEPALIVE
void removeBeamBreakpoint(int32_t id) {
  REQUIRE_EMULATOR();
  g_emulator->removeBeamBreakpoint(id);
}

EMSCRIPTEN_KEEPALIVE
void enableBeamBreakpoint(int32_t id, bool enabled) {
  REQUIRE_EMULATOR();
  g_emulator->enableBeamBreakpoint(id, enabled);
}

EMSCRIPTEN_KEEPALIVE
void clearAllBeamBreakpoints() {
  REQUIRE_EMULATOR();
  g_emulator->clearAllBeamBreakpoints();
}

EMSCRIPTEN_KEEPALIVE
bool isBeamBreakpointHit() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isBeamBreakpointHit();
}

EMSCRIPTEN_KEEPALIVE
int32_t getBeamBreakpointHitId() {
  REQUIRE_EMULATOR_OR(-1);
  return g_emulator->getBeamBreakpointHitId();
}

EMSCRIPTEN_KEEPALIVE
int16_t getBeamBreakScanline() {
  REQUIRE_EMULATOR_OR(-1);
  return g_emulator->getBeamBreakScanline();
}

EMSCRIPTEN_KEEPALIVE
int16_t getBeamBreakHPos() {
  REQUIRE_EMULATOR_OR(-1);
  return g_emulator->getBeamBreakHPos();
}

// ============================================================================
// Condition Evaluator
// ============================================================================

EMSCRIPTEN_KEEPALIVE
bool evaluateCondition(const char* expr) {
  REQUIRE_EMULATOR_OR(false);
  return a2e::ConditionEvaluator::evaluate(expr, *g_emulator);
}

EMSCRIPTEN_KEEPALIVE
int32_t evaluateExpression(const char* expr) {
  REQUIRE_EMULATOR_OR(0);
  return a2e::ConditionEvaluator::evaluateNumeric(expr, *g_emulator);
}

EMSCRIPTEN_KEEPALIVE
const char* getConditionError() {
  return a2e::ConditionEvaluator::getLastError();
}

// ============================================================================
// Opcode Mnemonic Lookup
// ============================================================================

EMSCRIPTEN_KEEPALIVE
const char* getOpcodeMnemonic(uint8_t opcode) {
  return a2e::getMnemonic(opcode);
}

EMSCRIPTEN_KEEPALIVE
uint8_t getOpcodeAddressingMode(uint8_t opcode) {
  return static_cast<uint8_t>(a2e::getAddressingMode(opcode));
}

// ============================================================================
// Call Stack Analysis
// ============================================================================

struct CallStackEntry {
  uint16_t returnAddr;
  uint16_t jsrTarget;
};

static CallStackEntry g_callStack[64];
static int g_callStackCount = 0;

EMSCRIPTEN_KEEPALIVE
int getCallStack() {
  REQUIRE_EMULATOR_OR(0);
  g_callStackCount = 0;

  uint8_t sp = g_emulator->getSP();
  int i = sp + 1;

  while (i < 0xFF && g_callStackCount < 64) {
    uint8_t low = g_emulator->peekMemory(0x100 + i);
    uint8_t high = g_emulator->peekMemory(0x100 + i + 1);
    uint16_t retAddr = ((high << 8) | low) + 1;

    // Validate: check if instruction before retAddr was a JSR
    if (retAddr >= 3 && retAddr <= 0xFFFF) {
      uint8_t possibleJSR = g_emulator->peekMemory(retAddr - 3);
      if (possibleJSR == 0x20) {
        // JSR target
        uint8_t jsrLo = g_emulator->peekMemory(retAddr - 2);
        uint8_t jsrHi = g_emulator->peekMemory(retAddr - 1);
        g_callStack[g_callStackCount].returnAddr = retAddr;
        g_callStack[g_callStackCount].jsrTarget = (jsrHi << 8) | jsrLo;
        g_callStackCount++;
        i += 2;
        continue;
      }
    }
    i++;
  }

  return g_callStackCount;
}

EMSCRIPTEN_KEEPALIVE
const void* getCallStackBuffer() {
  return g_callStack;
}

EMSCRIPTEN_KEEPALIVE
bool isLikelyReturnAddress(uint16_t addr) {
  REQUIRE_EMULATOR_OR(false);
  // Check if it points to code-like regions
  return (addr >= 0x0800 && addr < 0xC000) ||  // Main RAM (program code)
         (addr >= 0xD000 && addr <= 0xFFFF);    // ROM
}

// ============================================================================
// DOS 3.3 Filesystem
// ============================================================================

static a2e::DOS33CatalogEntry g_dos33Catalog[128];
static int g_dos33CatalogCount = 0;
static uint8_t g_dos33FileBuffer[256 * 256]; // 64KB max file

EMSCRIPTEN_KEEPALIVE
bool isDOS33Format(const uint8_t* data, int size) {
  return a2e::DOS33::isDOS33(data, static_cast<size_t>(size));
}

EMSCRIPTEN_KEEPALIVE
int getDOS33Catalog(const uint8_t* data, int size) {
  g_dos33CatalogCount = a2e::DOS33::readCatalog(data, static_cast<size_t>(size),
                                                  g_dos33Catalog, 128);
  return g_dos33CatalogCount;
}

EMSCRIPTEN_KEEPALIVE
const void* getDOS33CatalogBuffer() {
  return g_dos33Catalog;
}

EMSCRIPTEN_KEEPALIVE
int getDOS33CatalogEntrySize() {
  return static_cast<int>(sizeof(a2e::DOS33CatalogEntry));
}

EMSCRIPTEN_KEEPALIVE
const char* getDOS33EntryFilename(int index) {
  if (index < 0 || index >= g_dos33CatalogCount) return "";
  return g_dos33Catalog[index].filename;
}

EMSCRIPTEN_KEEPALIVE
uint8_t getDOS33EntryFileType(int index) {
  if (index < 0 || index >= g_dos33CatalogCount) return 0;
  return g_dos33Catalog[index].fileType;
}

EMSCRIPTEN_KEEPALIVE
const char* getDOS33EntryFileTypeName(int index) {
  if (index < 0 || index >= g_dos33CatalogCount) return "?";
  return g_dos33Catalog[index].fileTypeName;
}

EMSCRIPTEN_KEEPALIVE
bool getDOS33EntryIsLocked(int index) {
  if (index < 0 || index >= g_dos33CatalogCount) return false;
  return g_dos33Catalog[index].isLocked;
}

EMSCRIPTEN_KEEPALIVE
int getDOS33EntrySectorCount(int index) {
  if (index < 0 || index >= g_dos33CatalogCount) return 0;
  return g_dos33Catalog[index].sectorCount;
}

EMSCRIPTEN_KEEPALIVE
int readDOS33File(const uint8_t* data, int size, int index) {
  if (index < 0 || index >= g_dos33CatalogCount) return 0;
  const auto& entry = g_dos33Catalog[index];
  return a2e::DOS33::readFile(data, static_cast<size_t>(size),
                               entry.firstTrack, entry.firstSector,
                               g_dos33FileBuffer, sizeof(g_dos33FileBuffer));
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* getDOS33FileBuffer() {
  return g_dos33FileBuffer;
}

// ============================================================================
// ProDOS Filesystem
// ============================================================================

static a2e::ProDOSCatalogEntry g_prodosCatalog[2048];
static int g_prodosCatalogCount = 0;
static a2e::ProDOSVolumeInfo g_prodosVolumeInfo;
static uint8_t g_prodosFileBuffer[128 * 1024]; // 128KB max file

EMSCRIPTEN_KEEPALIVE
bool isProDOSFormat(const uint8_t* data, int size) {
  return a2e::ProDOS::isProDOS(data, static_cast<size_t>(size));
}

EMSCRIPTEN_KEEPALIVE
bool getProDOSVolumeInfo(const uint8_t* data, int size) {
  return a2e::ProDOS::parseVolumeInfo(data, static_cast<size_t>(size), &g_prodosVolumeInfo);
}

EMSCRIPTEN_KEEPALIVE
const char* getProDOSVolumeName() {
  return g_prodosVolumeInfo.volumeName;
}

EMSCRIPTEN_KEEPALIVE
int getProDOSTotalBlocks() {
  return g_prodosVolumeInfo.totalBlocks;
}

EMSCRIPTEN_KEEPALIVE
int getProDOSCatalog(const uint8_t* data, int size) {
  g_prodosCatalogCount = a2e::ProDOS::readCatalog(data, static_cast<size_t>(size),
                                                    g_prodosCatalog, 2048);
  return g_prodosCatalogCount;
}

EMSCRIPTEN_KEEPALIVE
int getProDOSDirectory(const uint8_t* data, int size, int startBlock,
                       const char* pathPrefix) {
  g_prodosCatalogCount = a2e::ProDOS::readDirectory(
      data, static_cast<size_t>(size), startBlock,
      pathPrefix ? pathPrefix : "", g_prodosCatalog, 2048);
  return g_prodosCatalogCount;
}

EMSCRIPTEN_KEEPALIVE
const char* getProDOSEntryFilename(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return "";
  return g_prodosCatalog[index].filename;
}

EMSCRIPTEN_KEEPALIVE
const char* getProDOSEntryPath(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return "";
  return g_prodosCatalog[index].path;
}

EMSCRIPTEN_KEEPALIVE
uint8_t getProDOSEntryFileType(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return 0;
  return g_prodosCatalog[index].fileType;
}

EMSCRIPTEN_KEEPALIVE
const char* getProDOSEntryFileTypeName(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return "???";
  return g_prodosCatalog[index].fileTypeName;
}

EMSCRIPTEN_KEEPALIVE
uint8_t getProDOSEntryStorageType(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return 0;
  return g_prodosCatalog[index].storageType;
}

EMSCRIPTEN_KEEPALIVE
uint32_t getProDOSEntryEOF(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return 0;
  return g_prodosCatalog[index].eof;
}

EMSCRIPTEN_KEEPALIVE
uint16_t getProDOSEntryAuxType(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return 0;
  return g_prodosCatalog[index].auxType;
}

EMSCRIPTEN_KEEPALIVE
bool getProDOSEntryIsLocked(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return false;
  return g_prodosCatalog[index].isLocked;
}

EMSCRIPTEN_KEEPALIVE
uint16_t getProDOSEntryBlocksUsed(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return 0;
  return g_prodosCatalog[index].blocksUsed;
}

EMSCRIPTEN_KEEPALIVE
bool getProDOSEntryIsDirectory(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return false;
  return g_prodosCatalog[index].isDirectory;
}

EMSCRIPTEN_KEEPALIVE
uint16_t getProDOSEntryKeyPointer(int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return 0;
  return g_prodosCatalog[index].keyPointer;
}

EMSCRIPTEN_KEEPALIVE
int readProDOSFile(const uint8_t* data, int size, int index) {
  if (index < 0 || index >= g_prodosCatalogCount) return 0;
  return a2e::ProDOS::readFile(data, static_cast<size_t>(size),
                                &g_prodosCatalog[index],
                                g_prodosFileBuffer, sizeof(g_prodosFileBuffer));
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* getProDOSFileBuffer() {
  return g_prodosFileBuffer;
}

EMSCRIPTEN_KEEPALIVE
int mapProDOSFileType(uint8_t prodosType) {
  return a2e::ProDOS::mapFileTypeForViewer(prodosType);
}

// ============================================================================
// Pascal Filesystem
// ============================================================================

static a2e::PascalCatalogEntry g_pascalCatalog[77];
static int g_pascalCatalogCount = 0;
static a2e::PascalVolumeInfo g_pascalVolumeInfo;
static uint8_t g_pascalFileBuffer[128 * 1024]; // 128KB max file

EMSCRIPTEN_KEEPALIVE
bool isPascalFormat(const uint8_t* data, int size) {
  return a2e::Pascal::isPascal(data, static_cast<size_t>(size));
}

EMSCRIPTEN_KEEPALIVE
bool getPascalVolumeInfo(const uint8_t* data, int size) {
  return a2e::Pascal::parseVolumeInfo(data, static_cast<size_t>(size), &g_pascalVolumeInfo);
}

EMSCRIPTEN_KEEPALIVE
const char* getPascalVolumeName() {
  return g_pascalVolumeInfo.volumeName;
}

EMSCRIPTEN_KEEPALIVE
int getPascalTotalBlocks() {
  return g_pascalVolumeInfo.totalBlocks;
}

EMSCRIPTEN_KEEPALIVE
int getPascalCatalog(const uint8_t* data, int size) {
  g_pascalCatalogCount = a2e::Pascal::readCatalog(data, static_cast<size_t>(size),
                                                    g_pascalCatalog, 77);
  return g_pascalCatalogCount;
}

EMSCRIPTEN_KEEPALIVE
const char* getPascalEntryFilename(int index) {
  if (index < 0 || index >= g_pascalCatalogCount) return "";
  return g_pascalCatalog[index].filename;
}

EMSCRIPTEN_KEEPALIVE
uint8_t getPascalEntryFileType(int index) {
  if (index < 0 || index >= g_pascalCatalogCount) return 0;
  return g_pascalCatalog[index].fileType;
}

EMSCRIPTEN_KEEPALIVE
const char* getPascalEntryFileTypeName(int index) {
  if (index < 0 || index >= g_pascalCatalogCount) return "???";
  return g_pascalCatalog[index].fileTypeName;
}

EMSCRIPTEN_KEEPALIVE
uint32_t getPascalEntryFileSize(int index) {
  if (index < 0 || index >= g_pascalCatalogCount) return 0;
  return g_pascalCatalog[index].fileSize;
}

EMSCRIPTEN_KEEPALIVE
uint16_t getPascalEntryBlocksUsed(int index) {
  if (index < 0 || index >= g_pascalCatalogCount) return 0;
  return g_pascalCatalog[index].nextBlock - g_pascalCatalog[index].startBlock;
}

EMSCRIPTEN_KEEPALIVE
int readPascalFile(const uint8_t* data, int size, int index) {
  if (index < 0 || index >= g_pascalCatalogCount) return 0;
  return a2e::Pascal::readFile(data, static_cast<size_t>(size),
                                &g_pascalCatalog[index],
                                g_pascalFileBuffer, sizeof(g_pascalFileBuffer));
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* getPascalFileBuffer() {
  return g_pascalFileBuffer;
}

EMSCRIPTEN_KEEPALIVE
int mapPascalFileType(uint8_t pascalType) {
  return a2e::Pascal::mapFileTypeForViewer(pascalType);
}

// ============================================================================
// BASIC Detokenization
// ============================================================================

EMSCRIPTEN_KEEPALIVE
const char* detokenizeApplesoft(const uint8_t* data, int size, bool hasLengthHeader) {
  return a2e::BasicDetokenizer::detokenizeApplesoft(data, size, hasLengthHeader);
}

EMSCRIPTEN_KEEPALIVE
const char* detokenizeIntegerBasic(const uint8_t* data, int size, bool hasLengthHeader) {
  return a2e::BasicDetokenizer::detokenizeIntegerBasic(data, size, hasLengthHeader);
}

// ============================================================================
// Assembler
// ============================================================================

static a2e::Assembler g_assembler;
static a2e::AsmResult g_asmResult;

EMSCRIPTEN_KEEPALIVE
bool assembleSource(const char* source) {
  g_asmResult = g_assembler.assemble(source);
  return g_asmResult.success;
}

EMSCRIPTEN_KEEPALIVE
int getAsmOutputSize() {
  return static_cast<int>(g_asmResult.output.size());
}

EMSCRIPTEN_KEEPALIVE
const uint8_t* getAsmOutputBuffer() {
  if (g_asmResult.output.empty()) return nullptr;
  return g_asmResult.output.data();
}

EMSCRIPTEN_KEEPALIVE
uint16_t getAsmOrigin() {
  return g_asmResult.origin;
}

EMSCRIPTEN_KEEPALIVE
int getAsmErrorCount() {
  return static_cast<int>(g_asmResult.errors.size());
}

EMSCRIPTEN_KEEPALIVE
int getAsmErrorLine(int index) {
  if (index < 0 || index >= static_cast<int>(g_asmResult.errors.size())) return 0;
  return g_asmResult.errors[index].lineNumber;
}

EMSCRIPTEN_KEEPALIVE
const char* getAsmErrorMessage(int index) {
  if (index < 0 || index >= static_cast<int>(g_asmResult.errors.size())) return "";
  return g_asmResult.errors[index].message;
}

EMSCRIPTEN_KEEPALIVE
int getAsmSymbolCount() {
  return static_cast<int>(g_asmResult.symbols.size());
}

EMSCRIPTEN_KEEPALIVE
const char* getAsmSymbolName(int index) {
  if (index < 0 || index >= static_cast<int>(g_asmResult.symbols.size())) return "";
  return g_asmResult.symbols[index].name;
}

EMSCRIPTEN_KEEPALIVE
int32_t getAsmSymbolValue(int index) {
  if (index < 0 || index >= static_cast<int>(g_asmResult.symbols.size())) return 0;
  return g_asmResult.symbols[index].value;
}

EMSCRIPTEN_KEEPALIVE
void loadAsmIntoMemory() {
  if (!g_emulator || g_asmResult.output.empty()) return;
  uint16_t addr = g_asmResult.origin;
  for (size_t i = 0; i < g_asmResult.output.size(); i++) {
    g_emulator->writeMemory(static_cast<uint16_t>(addr + i),
                            g_asmResult.output[i]);
  }
}

// ============================================================================
// BASIC Tokenizer
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int loadBasicProgram(const char* source) {
  REQUIRE_EMULATOR_OR(-1);
  auto read = [](uint16_t addr) -> uint8_t { return g_emulator->readMemory(addr); };
  auto write = [](uint16_t addr, uint8_t val) { g_emulator->writeMemory(addr, val); };
  return a2e::loadBasicProgram(source, read, write);
}

// ============================================================================
// No-Slot Clock (DS1215)
// ============================================================================

EMSCRIPTEN_KEEPALIVE
void enableNoSlotClock(bool enable) {
  REQUIRE_EMULATOR();
  g_emulator->enableNoSlotClock(enable);
}

EMSCRIPTEN_KEEPALIVE
bool isNoSlotClockEnabled() {
  REQUIRE_EMULATOR_OR(false);
  return g_emulator->isNoSlotClockEnabled();
}

} // extern "C"
