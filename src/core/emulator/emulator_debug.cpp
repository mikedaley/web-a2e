/*
 * emulator_debug.cpp - Debug facilities (breakpoints, watchpoints, trace, beam)
 *
 * Split from emulator.cpp to reduce file size. Implements Emulator member
 * methods for debugging: breakpoints, BASIC breakpoints, watchpoints,
 * beam breakpoints, beam position queries, step over/out, and trace logging.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "../emulator.hpp"
#include <algorithm>

namespace a2e {

// ============================================================================
// Beam Position
// ============================================================================

int Emulator::getFrameCycle() const {
  return static_cast<int>(cpu_->getTotalCycles() %
                          machine_->timing.cyclesPerFrame());
}

int Emulator::getBeamScanline() const {
  return getFrameCycle() / machine_->timing.cyclesPerScanline;
}

int Emulator::getBeamHPos() const {
  return getFrameCycle() % machine_->timing.cyclesPerScanline;
}

int Emulator::getBeamColumn() const {
  // A scanline starts in blanking; the visible columns follow it, so a beam
  // still inside the blanking interval is on no column at all.
  const int hblank = machine_->timing.hblankCycles;
  int hPos = getBeamHPos();
  return hPos >= hblank ? hPos - hblank : -1;
}

bool Emulator::isInVBL() const {
  return getBeamScanline() >= machine_->timing.visibleScanlines;
}

bool Emulator::isInHBLANK() const {
  return getBeamHPos() < machine_->timing.hblankCycles;
}

// ============================================================================
// Step Over / Step Out
// ============================================================================

uint16_t Emulator::stepOver() {
  clearTempBreakpoint();
  uint16_t pc = cpu_->getPC();
  uint8_t opcode = mmu_->peek(pc);

  if (opcode == 0x20) {
    // JSR - set temp breakpoint at instruction after JSR (PC + 3)
    uint16_t returnAddr = (pc + 3) & 0xFFFF;
    tempBreakpoint_ = returnAddr;
    tempBreakpointActive_ = true;
    setPaused(false);
    return returnAddr;
  } else if (opcode == 0x00) {
    // BRK - treat like JSR but with PC+2 as return address
    uint16_t returnAddr = (pc + 2) & 0xFFFF;
    tempBreakpoint_ = returnAddr;
    tempBreakpointActive_ = true;
    setPaused(false);
    return returnAddr;
  } else {
    // Not a JSR/BRK, just single step
    stepInstruction();
    return 0;
  }
}

uint16_t Emulator::stepOut() {
  clearTempBreakpoint();
  uint8_t sp = cpu_->getSP();
  uint8_t pcl = mmu_->peek(0x0100 + ((sp + 1) & 0xFF));
  uint8_t pch = mmu_->peek(0x0100 + ((sp + 2) & 0xFF));
  // RTS adds 1 to the address
  uint16_t returnAddr = ((pch << 8) | pcl) + 1;

  if (returnAddr > 0 && returnAddr <= 0xFFFF) {
    returnAddr &= 0xFFFF;
    tempBreakpoint_ = returnAddr;
    tempBreakpointActive_ = true;
    setPaused(false);
    return returnAddr;
  } else {
    // Invalid return address, just step
    stepInstruction();
    return 0;
  }
}

void Emulator::clearTempBreakpoint() {
  if (tempBreakpointActive_) {
    tempBreakpointActive_ = false;
    tempBreakpoint_ = 0;
    tempBreakpointHit_ = false;
  }
}

// ============================================================================
// Breakpoints
// ============================================================================

void Emulator::addBreakpoint(uint16_t address) { breakpoints_.insert(address); }

void Emulator::removeBreakpoint(uint16_t address) {
  breakpoints_.erase(address);
  disabledBreakpoints_.erase(address);
}

void Emulator::enableBreakpoint(uint16_t address, bool enabled) {
  if (enabled) {
    disabledBreakpoints_.erase(address);
  } else {
    if (breakpoints_.count(address)) {
      disabledBreakpoints_.insert(address);
    }
  }
}

// ============================================================================
// BASIC Breakpoints
// ============================================================================

void Emulator::addBasicBreakpoint(uint16_t lineNumber, int statementIndex) {
  basicBreakpoints_.insert({lineNumber, static_cast<int8_t>(statementIndex)});
}

void Emulator::removeBasicBreakpoint(uint16_t lineNumber, int statementIndex) {
  basicBreakpoints_.erase({lineNumber, static_cast<int8_t>(statementIndex)});
}

void Emulator::clearBasicBreakpoints() {
  basicBreakpoints_.clear();
  basicBreakpointHit_ = false;
}

void Emulator::clearBasicBreakpointHit() {
  basicBreakpointHit_ = false;
  basicConditionRuleHitId_ = -1;
  // Don't clear skipBasicBreakpointLine_ here - let it be cleared naturally
  // when CURLIN changes. This allows Run to work from a breakpoint by:
  // 1. setPaused(false) sets skip line
  // 2. clearBasicBreakpointHit() clears step mode but keeps skip
  // 3. Program continues, types RUN, skip cleared when line changes
  basicStepMode_ = BasicStepMode::None;
}

void Emulator::addBasicConditionRule(int id, const char* expression) {
  // Remove existing rule with same id
  removeBasicConditionRule(id);
  basicConditionRules_.push_back({id, std::string(expression), true});
}

void Emulator::removeBasicConditionRule(int id) {
  basicConditionRules_.erase(
    std::remove_if(basicConditionRules_.begin(), basicConditionRules_.end(),
      [id](const BasicConditionRule& r) { return r.id == id; }),
    basicConditionRules_.end());
}

void Emulator::clearBasicConditionRules() {
  basicConditionRules_.clear();
  basicConditionRuleHitId_ = -1;
}

void Emulator::stepBasicLine() {
  // Get current BASIC line (use readRAM to bypass ALTZP)
  uint16_t curlin = mmu_->readRAM(0x75, false) | (mmu_->readRAM(0x76, false) << 8);

  // Set up line stepping mode - will pause when CURLIN changes
  basicStepFromLine_ = curlin;
  basicStepLineStart_ = 0;  // Not used for line stepping, but reset for cleanliness
  basicStepNextColon_ = 0;   // Not used for line stepping
  basicStepMode_ = BasicStepMode::Line;

  // Clear any hit flags, reset sample counter to prevent backlog, and resume
  basicBreakpointHit_ = false;
  samplesGenerated_ = 0;
  paused_ = false;
  basicBreakLine_ = 0;
}

void Emulator::stepBasicStatement() {
  // Step to the next BASIC statement by waiting for PC to hit $D820
  // (JSR EXECUTE_STATEMENT in the ROM). Both new-line and colon paths
  // converge there with correct CURLIN and TXTPTR.
  uint16_t pc = cpu_->getPC();
  basicStepSkipFirst_ = (pc == 0xD820);
  basicStepMode_ = BasicStepMode::Statement;

  // Clear any hit flags, reset sample counter to prevent backlog, and resume
  basicBreakpointHit_ = false;
  samplesGenerated_ = 0;
  paused_ = false;
  basicBreakLine_ = 0;
}

uint16_t Emulator::getBasicTxtptr() const {
  // Read TXTPTR respecting current ALTZP state - BASIC writes to whichever
  // bank is active, so we need to read from the same bank
  return mmu_->peek(0xB8) | (mmu_->peek(0xB9) << 8);
}

int Emulator::getBasicStatementIndex() {
  // Use readRAM to bypass ALTZP - BASIC always uses main RAM for zero page
  uint16_t curlin = mmu_->readRAM(0x75, false) | (mmu_->readRAM(0x76, false) << 8);
  uint16_t txtptr = mmu_->readRAM(0xB8, false) | (mmu_->readRAM(0xB9, false) << 8);

  // Find line start for the current line
  uint16_t lineStart = findCurrentLineStart(curlin);

  // If TXTPTR hasn't entered the current line's text area yet, we're at statement 0
  if (lineStart == 0 || txtptr < lineStart) {
    return 0;
  }

  return countColonsBetween(lineStart, txtptr);
}

int Emulator::getBasicStatementCountForLine(uint16_t lineNumber) {
  const uint16_t lineStart = findCurrentLineStart(lineNumber);

  // An unknown line still has one statement as far as the UI is concerned.
  if (lineStart == 0) return 1;

  // countColonsBetween stops at the line's NUL terminator, so an end address
  // beyond any possible line body scans exactly one whole line.
  return countColonsBetween(lineStart, 0xBFFF) + 1;
}

int Emulator::getBasicStatementIndexForLine(uint16_t lineNumber, uint16_t txtptr) {
  const uint16_t lineStart = findCurrentLineStart(lineNumber);

  // Before the line's text begins, execution is still on its first statement.
  if (lineStart == 0 || txtptr < lineStart) return 0;

  return countColonsBetween(lineStart, txtptr);
}

int Emulator::countColonsBetween(uint16_t lineStart, uint16_t txtptr) {
  // If TXTPTR is at or before line start, we're at statement 0
  if (lineStart == 0 || txtptr <= lineStart) return 0;

  // Count colons from line start to TXTPTR, respecting strings
  // Use readRAM to ensure we read from main RAM where BASIC program is stored
  int colonCount = 0;
  bool inQuote = false;
  bool inRem = false;

  for (uint16_t a = lineStart; a < txtptr; a++) {
    uint8_t byte = mmu_->readRAM(a, false);

    if (byte == 0) break;  // End of line

    if (inRem) continue;  // Skip everything after REM

    if (byte == 0x22) {  // Quote
      inQuote = !inQuote;
      continue;
    }

    if (inQuote) continue;  // Skip string contents

    if (byte == 0xB2) {  // REM token
      inRem = true;
      continue;
    }

    if (byte == 0x3A) {  // Colon
      colonCount++;
    }
  }

  return colonCount;
}

uint16_t Emulator::findNextColonAfter(uint16_t lineStart, uint16_t afterPos) {
  // Find the address of the next colon after afterPos within the line
  // Returns 0 if no colon found (i.e., afterPos is in the last statement)
  if (lineStart == 0) return 0;

  // Start searching from afterPos (or lineStart if afterPos is before it)
  uint16_t searchStart = (afterPos >= lineStart) ? afterPos : lineStart;
  bool inQuote = false;
  bool inRem = false;

  // First, establish quote/REM state at searchStart by scanning from lineStart
  for (uint16_t a = lineStart; a < searchStart; a++) {
    uint8_t byte = mmu_->readRAM(a, false);
    if (byte == 0) return 0;  // Already past end of line
    if (inRem) continue;
    if (byte == 0x22) inQuote = !inQuote;
    if (!inQuote && byte == 0xB2) inRem = true;
  }

  // Now search for the next colon
  for (uint16_t a = searchStart; a < searchStart + 256; a++) {  // Limit search
    uint8_t byte = mmu_->readRAM(a, false);

    if (byte == 0) return 0;  // End of line, no more colons

    if (inRem) continue;

    if (byte == 0x22) {
      inQuote = !inQuote;
      continue;
    }

    if (inQuote) continue;

    if (byte == 0xB2) {
      inRem = true;
      continue;
    }

    if (byte == 0x3A) {  // Found a colon!
      return a;
    }
  }

  return 0;  // No colon found
}

uint16_t Emulator::findCurrentLineStart(uint16_t lineNumber) {
  // Get TXTTAB (start of BASIC program)
  // Use readRAM to bypass ALTZP - BASIC always uses main RAM for zero page
  uint16_t txttab = mmu_->readRAM(0x67, false) | (mmu_->readRAM(0x68, false) << 8);

  if (lineNumber == 0xFFFF) return 0;  // Not running

  // Find the specified line in the program
  uint16_t addr = txttab;

  while (addr < 0xC000) {  // Reasonable upper bound
    uint16_t nextPtr = mmu_->readRAM(addr, false) | (mmu_->readRAM(addr + 1, false) << 8);
    if (nextPtr == 0) break;  // End of program

    uint16_t lineNum = mmu_->readRAM(addr + 2, false) | (mmu_->readRAM(addr + 3, false) << 8);
    if (lineNum == lineNumber) {
      return addr + 4;  // Start of tokenized text (after nextPtr and lineNum)
    }
    addr = nextPtr;
  }

  return 0;  // Line not found
}

// ============================================================================
// BASIC Heat Map
// ============================================================================

int Emulator::getBasicHeatMapData(uint16_t* lines, uint32_t* counts, int maxEntries) const {
  int i = 0;
  for (const auto& [line, count] : basicHeatMap_) {
    if (i >= maxEntries) break;
    lines[i] = line;
    counts[i] = count;
    i++;
  }
  return i;
}

// ============================================================================
// Watchpoints
// ============================================================================

void Emulator::addWatchpoint(uint16_t startAddr, uint16_t endAddr, WatchpointType type) {
  watchpoints_.push_back({startAddr, endAddr, type, true});
  watchpointsActive_ = true;
  mmu_->setWatchpointsActive(true);
}

void Emulator::removeWatchpoint(uint16_t startAddr) {
  watchpoints_.erase(
    std::remove_if(watchpoints_.begin(), watchpoints_.end(),
      [startAddr](const Watchpoint& wp) { return wp.startAddr == startAddr; }),
    watchpoints_.end());
  watchpointsActive_ = !watchpoints_.empty();
  mmu_->setWatchpointsActive(watchpointsActive_);
}

void Emulator::clearWatchpoints() {
  watchpoints_.clear();
  watchpointsActive_ = false;
  mmu_->setWatchpointsActive(false);
}

void Emulator::onWatchpointRead(uint16_t address, uint8_t value) {
  if (!watchpointsActive_ || watchpointHit_) return;
  for (const auto& wp : watchpoints_) {
    if (!wp.enabled) continue;
    if ((wp.type & WP_READ) && address >= wp.startAddr && address <= wp.endAddr) {
      watchpointHit_ = true;
      watchpointAddress_ = address;
      watchpointValue_ = value;
      watchpointIsWrite_ = false;
      paused_ = true;
      return;
    }
  }
}

void Emulator::onWatchpointWrite(uint16_t address, uint8_t value) {
  if (!watchpointsActive_ || watchpointHit_) return;
  for (const auto& wp : watchpoints_) {
    if (!wp.enabled) continue;
    if ((wp.type & WP_WRITE) && address >= wp.startAddr && address <= wp.endAddr) {
      watchpointHit_ = true;
      watchpointAddress_ = address;
      watchpointValue_ = value;
      watchpointIsWrite_ = true;
      paused_ = true;
      return;
    }
  }
}

// ============================================================================
// Beam Breakpoints
// ============================================================================

int32_t Emulator::addBeamBreakpoint(int16_t scanline, int16_t hPos) {
  if (beamBreakpoints_.size() >= MAX_BEAM_BREAKPOINTS) return -1;
  int32_t id = beamBreakNextId_++;
  beamBreakpoints_.push_back({scanline, hPos, true, id, UINT64_MAX, -1});
  return id;
}

void Emulator::removeBeamBreakpoint(int32_t id) {
  beamBreakpoints_.erase(
    std::remove_if(beamBreakpoints_.begin(), beamBreakpoints_.end(),
      [id](const BeamBreakpoint& bp) { return bp.id == id; }),
    beamBreakpoints_.end());
}

void Emulator::enableBeamBreakpoint(int32_t id, bool enabled) {
  for (auto& bp : beamBreakpoints_) {
    if (bp.id == id) {
      bp.enabled = enabled;
      return;
    }
  }
}

void Emulator::clearAllBeamBreakpoints() {
  beamBreakpoints_.clear();
  beamBreakNextId_ = 1;
  beamBreakHit_ = false;
  beamBreakHitId_ = -1;
  beamBreakHitScanline_ = -1;
  beamBreakHitHPos_ = -1;
}

// ============================================================================
// Trace Log
// ============================================================================

void Emulator::recordTrace() {
  if (traceBuffer_.empty()) {
    traceBuffer_.resize(10000);
  }

  auto& entry = traceBuffer_[traceHead_];
  entry.pc = cpu_->getPC();
  entry.opcode = mmu_->peek(entry.pc);
  entry.a = cpu_->getA();
  entry.x = cpu_->getX();
  entry.y = cpu_->getY();
  entry.sp = cpu_->getSP();
  entry.p = cpu_->getP();

  // Read operands
  entry.instrLen = 1;
  entry.operand1 = 0;
  entry.operand2 = 0;

  // Determine instruction length from opcode
  static const uint8_t instrLengths[256] = {
    1,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    3,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    1,2,1,1,1,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,1,2,2,2,1,3,1,1,1,3,3,3,
    1,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,1,2,2,2,1,3,1,1,1,3,3,3,
    2,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,1,2,2,2,1,3,1,1,1,3,3,3,
  };

  entry.instrLen = instrLengths[entry.opcode];
  if (entry.instrLen >= 2) entry.operand1 = mmu_->peek(entry.pc + 1);
  if (entry.instrLen >= 3) entry.operand2 = mmu_->peek(entry.pc + 2);

  entry.cycle = static_cast<uint32_t>(cpu_->getTotalCycles());
  entry.padding = 0;

  traceHead_ = (traceHead_ + 1) % traceBuffer_.size();
  if (traceCount_ < traceBuffer_.size()) traceCount_++;
}

} // namespace a2e
