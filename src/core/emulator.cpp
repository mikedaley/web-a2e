/*
 * emulator.cpp - Core emulator coordinator tying together CPU, memory, video, audio, and peripherals
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "emulator.hpp"
#include "cards/disk2_card.hpp"
#include "cards/mockingboard_card.hpp"
#include "cards/thunderclock_card.hpp"
#include "cards/mouse_card.hpp"
#include "cards/smartport/smartport_card.hpp"
#include "cards/softcard_z80.hpp"
#include "debug/condition_evaluator.hpp"
#include <algorithm>
#include <cstring>

// Include generated ROM data directly
#include "roms.cpp" // namespace roms

namespace a2e {

Emulator::Emulator() {
  mmu_ = std::make_unique<MMU>();
  video_ = std::make_unique<Video>(*mmu_);
  audio_ = std::make_unique<Audio>();
  keyboard_ = std::make_unique<Keyboard>();

  // Create cards, keep raw pointers, then insert into slots
  auto disk = std::make_unique<Disk2Card>();
  auto mb = std::make_unique<MockingboardCard>();
  disk_ = disk.get();
  mockingboard_ = mb.get();

  // Create CPU with memory callbacks
  cpu_ = std::make_unique<CPU6502>(
      [this](uint16_t addr) { return cpuRead(addr); },
      [this](uint16_t addr, uint8_t val) { cpuWrite(addr, val); },
      CPUVariant::CMOS_65C02);

  // Set up keyboard callback to receive translated keys
  keyboard_->setKeyCallback([this](int key) { keyDown(key); });

  // Set up MMU callbacks
  mmu_->setKeyboardCallback([this]() { return getKeyboardData(); });
  mmu_->setKeyStrobeCallback([this]() { clearKeyboardStrobe(); });
  mmu_->setAnyKeyDownCallback([this]() { return keyDown_; });
  mmu_->setSpeakerCallback([this]() { toggleSpeaker(); });
  mmu_->setButtonCallback([this](int btn) { return getButtonState(btn); });
  mmu_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });

  // Wire video subsystem callbacks
  video_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
  mmu_->setVideoSwitchCallback([this]() { video_->onVideoSwitchChanged(); });

  // Wire watchpoint callbacks (MMU -> Emulator)
  mmu_->setWatchpointCallbacks(
    [this](uint16_t addr, uint8_t val) { onWatchpointRead(addr, val); },
    [this](uint16_t addr, uint8_t val) { onWatchpointWrite(addr, val); });

  // Set up Mockingboard callbacks
  mockingboard_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
  mockingboard_->setIRQCallback([this]() { cpu_->irq(); });

  // Set up level-triggered IRQ polling for VIA/mouse interrupts
  cpu_->setIRQStatusCallback([this]() {
    bool active = mockingboard_ ? mockingboard_->isIRQActive() : false;
    if (mouse_) active = active || mouse_->isIRQActive();
    return active;
  });

  // Set up disk timing callback - allows disk reads to get accurate cycle count
  // during instruction execution (before disk_->update() is called)
  disk_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });

  // Insert cards into slots (transfers ownership to MMU)
  mmu_->insertCard(6, std::move(disk));
  mmu_->insertCard(4, std::move(mb));

  // Audio gets raw pointer
  audio_->setMockingboard(mockingboard_);
}

Emulator::~Emulator() = default;

void Emulator::init() {
  // Load system and character ROMs into MMU
  mmu_->loadROM(roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE,
                roms::ROM_CHAR, roms::ROM_CHAR_SIZE);

  // Load Disk II ROM into the card
  disk_->loadROM(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

  reset();
}

void Emulator::reset() {
  mmu_->reset();
  cpu_->resetCycleCount(); // Clear cycle counter for fresh power-on state
  cpu_->reset();
  audio_->reset();
  if (disk_) disk_->reset();
  keyboard_->reset();
  if (mockingboard_) mockingboard_->reset();

  // Clear Apple button states
  setButton(0, false);
  setButton(1, false);

  keyboardLatch_ = 0;
  keyDown_ = false;
  speedMultiplier_ = 1;
  lastFrameCycle_ = 0;
  samplesGenerated_ = 0;
  frameReady_ = false;
  breakpointHit_ = false;
  watchpointHit_ = false;
  skipBreakpointOnce_ = false;
  tempBreakpointActive_ = false;
  tempBreakpoint_ = 0;
  tempBreakpointHit_ = false;
  // Keep beam breakpoints across reset (same as regular breakpoints)
  for (auto& bp : beamBreakpoints_) {
    bp.lastFireFrame = UINT64_MAX;
    bp.lastFireScanline = -1;
  }
  beamBreakHit_ = false;
  beamBreakHitId_ = -1;
  beamBreakHitScanline_ = -1;
  beamBreakHitHPos_ = -1;
  paused_ = false;

  // Clear BASIC debugging state
  basicProgramRunning_ = false;
  basicBreakpointHit_ = false;
  basicErrorHit_ = false;
  basicErrorLine_ = 0;
  basicErrorCode_ = 0;
  basicStepMode_ = BasicStepMode::None;
  skipBasicBreakpointLine_ = 0xFFFF;
  skipBasicBreakpointStmt_ = -1;
  basicBreakLine_ = 0;

  video_->beginNewFrame(0);
}

void Emulator::warmReset() {
  // Warm reset - CPU jumps to reset vector, preserves memory and disk state
  // On real hardware, the reset signal resets soft switches via the IOU chip
  // but does not clear RAM, so programs in memory are preserved
  mmu_->warmReset();
  cpu_->reset();
  audio_->reset();
  keyboard_->reset();

  // Stop disk motor (real Apple IIe reset signal turns off motor)
  if (disk_) disk_->stopMotor();

  // Clear Apple button states
  setButton(0, false);
  setButton(1, false);

  // Reset video to clean frame state
  video_->beginNewFrame(cpu_->getTotalCycles());

  // Clear debugger hit flags
  breakpointHit_ = false;
  watchpointHit_ = false;
  skipBreakpointOnce_ = false;
  tempBreakpointActive_ = false;
  tempBreakpointHit_ = false;
  beamBreakHit_ = false;

  // Clear BASIC debugger state
  basicBreakpointHit_ = false;
  basicStepMode_ = BasicStepMode::None;
  basicBreakLine_ = 0;

  paused_ = false;
  frameReady_ = false;
  samplesGenerated_ = 0;
}

void Emulator::setPaused(bool paused) {
  if (!paused && paused_ && breakpointHit_) {
    skipBreakpointOnce_ = true;
  }
  if (!paused && paused_ && basicBreakpointHit_) {
    // Skip this BASIC breakpoint until we move to a different line/statement
    skipBasicBreakpointLine_ = basicBreakLine_;
    // Determine which statement we're on to set the skip correctly
    uint16_t lineStart = findCurrentLineStart(basicBreakLine_);
    uint16_t txtptr = mmu_->readRAM(0xB8, false) | (mmu_->readRAM(0xB9, false) << 8);
    int stmtIdx = (lineStart > 0 && txtptr >= lineStart)
        ? countColonsBetween(lineStart, txtptr) : 0;
    // Check if the breakpoint that hit was a statement-level one
    bool hasStmtBp = false;
    for (const auto& bp : basicBreakpoints_) {
      if (bp.lineNumber == basicBreakLine_ && bp.statementIndex >= 0 && bp.statementIndex == stmtIdx) {
        hasStmtBp = true;
        break;
      }
    }
    skipBasicBreakpointStmt_ = hasStmtBp ? static_cast<int8_t>(stmtIdx) : -1;
  }
  breakpointHit_ = false;
  basicBreakpointHit_ = false;
  watchpointHit_ = false;
  beamBreakHit_ = false;
  beamBreakHitId_ = -1;
  // Reset frame sample counter when unpausing to prevent backlog
  if (!paused && paused_) {
    samplesGenerated_ = 0;
  }
  paused_ = paused;
}

void Emulator::runCycles(int cycles) {
  if (paused_)
    return;

  uint64_t startCycles = cpu_->getTotalCycles();
  uint64_t targetCycles = startCycles + cycles;

  while (cpu_->getTotalCycles() < targetCycles) {
    // When Z80 SoftCard is active, the 6502 is halted via DMA.
    // Skip all 6502-specific checks and just advance timing.
    if (softcard_ && softcard_->isZ80Active()) {
      uint64_t cyclesBefore = cpu_->getTotalCycles();
      cpu_->setTotalCycles(cpu_->getTotalCycles() + 1);
      uint64_t cyclesUsed = cpu_->getTotalCycles() - cyclesBefore;
      if (disk_) disk_->update(static_cast<int>(cyclesUsed));
      if (mockingboard_) mockingboard_->update(static_cast<int>(cyclesUsed));
      if (mouse_) mouse_->update(static_cast<int>(cyclesUsed));
      softcard_->update(static_cast<int>(cyclesUsed));

      // Progressive rendering and frame boundary
      video_->renderUpToCycle(cpu_->getTotalCycles());
      uint64_t currentCycle = cpu_->getTotalCycles();
      if (currentCycle - lastFrameCycle_ >= CYCLES_PER_FRAME) {
        lastFrameCycle_ += CYCLES_PER_FRAME;
        video_->renderFrame();
        video_->beginNewFrame(lastFrameCycle_);
        frameReady_ = true;
      }
      continue;
    }

    // Check breakpoints (user breakpoints and temp breakpoint)
    {
      uint16_t pc = cpu_->getPC();

      // Check temp breakpoint (step over / step out)
      if (tempBreakpointActive_ && pc == tempBreakpoint_) {
        tempBreakpointHit_ = true;
        clearTempBreakpoint();
        breakpointHit_ = true;
        breakpointAddress_ = pc;
        paused_ = true;
        return;
      }

      // Check user breakpoints
      if (!breakpoints_.empty()) {
        if (skipBreakpointOnce_) {
          skipBreakpointOnce_ = false;
        } else if (breakpoints_.count(pc) && !disabledBreakpoints_.count(pc)) {
          breakpointHit_ = true;
          breakpointAddress_ = pc;
          paused_ = true;
          return;
        }
      }
    }

    // Track BASIC program running state by monitoring ROM entry points.
    // $D912 (RUN command) = program starting, $D43C (RESTART) = returning to ] prompt.
    // This is definitive because it hooks into the ROM's own execution flow.
    {
      uint16_t pc = cpu_->getPC();
      if (pc == 0xD912 && !basicProgramRunning_) {
        basicProgramRunning_ = true;
        basicErrorHit_ = false;  // Clear error state on new RUN
      } else if (pc == 0xD43C && basicProgramRunning_) {
        basicProgramRunning_ = false;
      }

      // ERROR handler entry at $D412 — X register holds error code offset,
      // CURLIN and TXTPTR still point to the offending location.
      // Only capture if ERRFLG ($D8) bit 7 is clear (no ONERR GOTO active).
      if (pc == 0xD412 && basicProgramRunning_) {
        uint8_t errflg = mmu_->readRAM(0xD8, false);
        if (!(errflg & 0x80)) {
          uint8_t curlinHi = mmu_->readRAM(0x76, false);
          if (curlinHi != 0xFF) {  // Not in direct mode
            basicErrorHit_ = true;
            basicErrorLine_ = mmu_->readRAM(0x75, false) | (static_cast<uint16_t>(curlinHi) << 8);
            basicErrorTxtptr_ = mmu_->readRAM(0xB8, false) | (mmu_->readRAM(0xB9, false) << 8);
            basicErrorCode_ = cpu_->getX();
          }
        }
      }
    }

    // Check BASIC stepping and breakpoints
    // CURLIN+1 ($76) = $FF means direct/immediate mode (only high byte matters,
    // matching how the ROM checks it at NEWSTT $D7DC: LDX CURLIN+1 / INX / BEQ)
    // Use readRAM to bypass ALTZP switch - BASIC always uses main RAM for zero page
    uint8_t curlinHi = mmu_->readRAM(0x76, false);
    bool basicDirectMode = (curlinHi == 0xFF);
    uint16_t curlin = mmu_->readRAM(0x75, false) | (static_cast<uint16_t>(curlinHi) << 8);

    // Clear skip line when returning to direct mode
    if (basicDirectMode && skipBasicBreakpointLine_ != 0xFFFF) {
      skipBasicBreakpointLine_ = 0xFFFF;
      skipBasicBreakpointStmt_ = -1;
    }

    if (!basicDirectMode) {
      uint16_t pc = cpu_->getPC();

      // All BASIC stepping and line breakpoints fire at $D820 (EXECUTE_STATEMENT).
      // At this ROM address, both new-line and colon paths have converged:
      // CURLIN is correct and TXTPTR points to the first token of the statement
      // about to execute. This ensures consistent state for statement highlighting.
      if (pc == 0xD820) {
        // Heat map: count every statement execution
        if (basicHeatMapEnabled_) {
          basicHeatMap_[curlin]++;
        }

        // BASIC line stepping - pause when CURLIN changes
        if (basicStepMode_ == BasicStepMode::Line) {
          if (curlin != basicStepFromLine_) {
            basicStepMode_ = BasicStepMode::None;
            basicBreakpointHit_ = true;
            basicBreakLine_ = curlin;
            paused_ = true;
            return;
          }
        }

        // BASIC statement stepping
        if (basicStepMode_ == BasicStepMode::Statement) {
          if (basicStepSkipFirst_) {
            basicStepSkipFirst_ = false;
          } else {
            basicStepMode_ = BasicStepMode::None;
            basicBreakpointHit_ = true;
            basicBreakLine_ = curlin;
            paused_ = true;
            return;
          }
        }

        // Check BASIC line and statement breakpoints
        if (!basicBreakpoints_.empty()) {
          bool matched = false;
          int currentStmtIndex = -2; // Sentinel: not yet computed
          for (const auto& bp : basicBreakpoints_) {
            if (bp.lineNumber != curlin) continue;
            if (bp.statementIndex == -1) {
              // Whole-line breakpoint
              matched = true;
              break;
            }
            // Statement-level: lazily compute current statement index
            if (currentStmtIndex == -2) {
              uint16_t lineStart = findCurrentLineStart(curlin);
              uint16_t txtptr = mmu_->readRAM(0xB8, false) | (mmu_->readRAM(0xB9, false) << 8);
              currentStmtIndex = (lineStart > 0 && txtptr >= lineStart)
                  ? countColonsBetween(lineStart, txtptr) : 0;
            }
            if (bp.statementIndex == currentStmtIndex) {
              matched = true;
              break;
            }
          }

          if (matched) {
            // Skip if we're stepping
            if (basicStepMode_ != BasicStepMode::None) {
              // Don't break while stepping
            }
            // Skip logic: check if this matches the skip (line, stmt) pair
            else if (curlin == skipBasicBreakpointLine_) {
              if (skipBasicBreakpointStmt_ == -1) {
                // Whole-line skip: skip all breakpoints on this line
              } else {
                // Statement-level skip: only skip this specific statement
                if (currentStmtIndex == -2) {
                  uint16_t lineStart = findCurrentLineStart(curlin);
                  uint16_t txtptr = mmu_->readRAM(0xB8, false) | (mmu_->readRAM(0xB9, false) << 8);
                  currentStmtIndex = (lineStart > 0 && txtptr >= lineStart)
                      ? countColonsBetween(lineStart, txtptr) : 0;
                }
                if (currentStmtIndex != skipBasicBreakpointStmt_) {
                  // Different statement on same line - break!
                  basicBreakpointHit_ = true;
                  basicBreakLine_ = curlin;
                  paused_ = true;
                  return;
                }
              }
            } else {
              basicBreakpointHit_ = true;
              basicBreakLine_ = curlin;
              paused_ = true;
              return;
            }
          }
        }

        // Condition-only rules: evaluate each expression in C++, pause only if one matches
        // Skip while stepping to allow step to complete
        // Skip on the line we just resumed from (same skip logic as line breakpoints)
        if (!basicConditionRules_.empty() && !basicBreakpointHit_ &&
            basicStepMode_ == BasicStepMode::None &&
            curlin != skipBasicBreakpointLine_) {
          for (auto& rule : basicConditionRules_) {
            if (!rule.enabled) continue;
            if (ConditionEvaluator::evaluate(rule.expression.c_str(), *this)) {
              basicBreakpointHit_ = true;
              basicBreakLine_ = curlin;
              basicConditionRuleHitId_ = rule.id;
              paused_ = true;
              return;
            }
          }
        }
      }

      // Clear skip-line when we move to a different line
      if (skipBasicBreakpointLine_ != 0xFFFF && curlin != skipBasicBreakpointLine_) {
        skipBasicBreakpointLine_ = 0xFFFF;
        skipBasicBreakpointStmt_ = -1;
      }
    }

    // Record trace before execution
    if (traceEnabled_) recordTrace();

    // Track cycles before instruction
    uint64_t cyclesBefore = cpu_->getTotalCycles();

    // Profile: record PC before execution
    uint16_t profilePC = profileEnabled_ ? cpu_->getPC() : 0;

    // Execute one instruction
    cpu_->executeInstruction();

    // Update disk controller with actual instruction cycles
    uint64_t cyclesUsed = cpu_->getTotalCycles() - cyclesBefore;
    if (disk_) disk_->update(static_cast<int>(cyclesUsed));

    // Accumulate cycle profiling
    if (profileEnabled_) {
      profileCycles_[profilePC] += static_cast<uint32_t>(cyclesUsed);
    }

    // Update Mockingboard timers BEFORE next instruction
    // This ensures timer IRQs fire before the CPU can disable them
    if (mockingboard_) mockingboard_->update(static_cast<int>(cyclesUsed));

    // Update mouse card for VBL interrupt detection
    if (mouse_) mouse_->update(static_cast<int>(cyclesUsed));

    // Update Z-80 SoftCard (runs Z80 T-states when active)
    if (softcard_) softcard_->update(static_cast<int>(cyclesUsed));

    // Progressive rendering: render scanlines up to current cycle
    video_->renderUpToCycle(cpu_->getTotalCycles());

    // Check for frame boundary
    uint64_t currentCycle = cpu_->getTotalCycles();
    if (currentCycle - lastFrameCycle_ >= CYCLES_PER_FRAME) {
      // Advance by exactly CYCLES_PER_FRAME to stay aligned with VBL detection
      // ($C019 uses cycles % CYCLES_PER_FRAME). Using currentCycle would drift
      // by a few cycles each frame, desynchronizing raster effects.
      lastFrameCycle_ += CYCLES_PER_FRAME;
      video_->renderFrame();                   // Uses this frame's change log
      video_->beginNewFrame(lastFrameCycle_);   // Reset log, aligned to frame boundary
      frameReady_ = true;
    }

    // Check watchpoint hit (set by MMU callbacks during execution)
    if (watchpointHit_) return;

    // Check beam breakpoints
    if (!beamBreakpoints_.empty()) {
      uint64_t fc = cpu_->getTotalCycles() - lastFrameCycle_;
      if (fc >= CYCLES_PER_FRAME) fc %= CYCLES_PER_FRAME;
      int16_t sl = static_cast<int16_t>(fc / 65);
      int16_t hp = static_cast<int16_t>(fc % 65);
      for (auto& bp : beamBreakpoints_) {
        if (!bp.enabled) continue;
        bool scanOk = (bp.scanline < 0) || (sl == bp.scanline);
        bool hPosOk = (bp.hPos < 0) || (hp >= bp.hPos);
        bool valid = (bp.scanline >= 0 || bp.hPos >= 0);
        if (!scanOk || !hPosOk || !valid) continue;

        // For wildcard-scanline breakpoints (HBLANK, Column), fire once per scanline.
        // For specific-scanline breakpoints (VBL, Scanline, ScanCol), fire once per frame.
        bool alreadyFired;
        if (bp.scanline < 0) {
          alreadyFired = (lastFrameCycle_ == bp.lastFireFrame && sl == bp.lastFireScanline);
        } else {
          alreadyFired = (lastFrameCycle_ == bp.lastFireFrame);
        }
        if (!alreadyFired) {
          beamBreakHit_ = true;
          beamBreakHitId_ = bp.id;
          beamBreakHitScanline_ = sl;
          beamBreakHitHPos_ = hp;
          bp.lastFireFrame = lastFrameCycle_;
          bp.lastFireScanline = sl;
          paused_ = true;
          return;
        }
      }
    }
  }
}

void Emulator::setSpeedMultiplier(int multiplier) {
  if (multiplier < 1) multiplier = 1;
  if (multiplier > 8) multiplier = 8;
  speedMultiplier_ = multiplier;
}

int Emulator::generateStereoAudioSamples(float *buffer, int sampleCount) {
  // Calculate cycles needed for this audio buffer, scaled by speed multiplier
  int cyclesToRun = static_cast<int>(sampleCount * CYCLES_PER_SAMPLE * speedMultiplier_);

  // Run emulation for the required cycles
  runCycles(cyclesToRun);

  // Track samples for frame synchronization
  samplesGenerated_ += sampleCount;

  // Generate stereo audio samples (interleaved L/R)
  return audio_->generateStereoSamples(buffer, sampleCount, cpu_->getTotalCycles());
}

int Emulator::consumeFrameSamples() {
  // Returns number of complete frames worth of samples generated
  // 48000 Hz / 60 Hz = 800 samples per frame
  int frames = samplesGenerated_ / SAMPLES_PER_FRAME;
  samplesGenerated_ %= SAMPLES_PER_FRAME;
  return frames;
}

const uint8_t *Emulator::getFramebuffer() const {
  return video_->getFramebuffer();
}

int Emulator::handleRawKeyDown(int browserKeycode, bool shift, bool ctrl,
                                bool alt, bool meta, bool capsLock) {
  int result = keyboard_->handleKeyDown(browserKeycode, shift, ctrl, alt, meta, capsLock);

  // Update button state from modifier keys
  setButton(0, keyboard_->isOpenApplePressed());   // Open Apple
  setButton(1, keyboard_->isClosedApplePressed()); // Closed Apple

  return result;
}

void Emulator::handleRawKeyUp(int browserKeycode, bool shift, bool ctrl,
                              bool alt, bool meta) {
  keyboard_->handleKeyUp(browserKeycode, shift, ctrl, alt, meta);

  // Update button state from modifier keys
  setButton(0, keyboard_->isOpenApplePressed());   // Open Apple
  setButton(1, keyboard_->isClosedApplePressed()); // Closed Apple
}

void Emulator::keyDown(int keycode) {
  // Direct Apple II keycode input (used for paste functionality)
  // Sets the keyboard latch with high bit set
  keyboardLatch_ = (keycode & 0x7F) | 0x80;
  keyDown_ = true;
}

void Emulator::keyUp(int keycode) {
  (void)keycode;
  keyDown_ = false;
}

void Emulator::setButton(int button, bool pressed) {
  if (button >= 0 && button < 3) {
    buttonState_[button] = pressed;
  }
}

void Emulator::setPaddleValue(int paddle, int value) {
  mmu_->setPaddleValue(paddle, static_cast<uint8_t>(value & 0xFF));
}

int Emulator::getPaddleValue(int paddle) const {
  return mmu_->getPaddleValue(paddle);
}

uint8_t Emulator::getButtonState(int button) {
  if (button >= 0 && button < 3 && buttonState_[button]) {
    return 0x80; // Bit 7 set = button pressed
  }
  return 0x00; // Button not pressed
}

bool Emulator::insertDisk(int drive, const uint8_t *data, size_t size,
                          const char *filename) {
  if (!disk_) return false;
  return disk_->insertDisk(drive, data, size, filename ? filename : "");
}

bool Emulator::insertBlankDisk(int drive) {
  if (!disk_) return false;
  return disk_->insertBlankDisk(drive);
}

void Emulator::ejectDisk(int drive) {
  if (disk_) disk_->ejectDisk(drive);
}

const uint8_t *Emulator::getDiskData(int drive, size_t *size) const {
  if (!disk_) return nullptr;
  return disk_->getDiskData(drive, size);
}

const uint8_t *Emulator::exportDiskData(int drive, size_t *size) {
  if (!disk_) return nullptr;
  return disk_->exportDiskData(drive, size);
}

const char *Emulator::getDiskFilename(int drive) const {
  if (!disk_) return nullptr;
  const auto *image = disk_->getDiskImage(drive);
  if (!image) {
    return nullptr;
  }
  return image->getFilename().c_str();
}

// Debug facilities (breakpoints, watchpoints, trace, beam) are in emulator_debug.cpp

void Emulator::stepInstruction() {
  breakpointHit_ = false;
  watchpointHit_ = false;
  beamBreakHit_ = false;
  beamBreakHitId_ = -1;

  // Record trace before execution
  if (traceEnabled_) recordTrace();

  // Track cycles before instruction
  uint64_t cyclesBefore = cpu_->getTotalCycles();

  // Profile: record PC before execution
  uint16_t profilePC = profileEnabled_ ? cpu_->getPC() : 0;

  cpu_->executeInstruction();

  // Update disk controller with actual instruction cycles
  uint64_t cyclesUsed = cpu_->getTotalCycles() - cyclesBefore;
  if (disk_) disk_->update(static_cast<int>(cyclesUsed));

  // Accumulate cycle profiling
  if (profileEnabled_) {
    profileCycles_[profilePC] += static_cast<uint32_t>(cyclesUsed);
  }

  // Update Mockingboard timers
  if (mockingboard_) mockingboard_->update(static_cast<int>(cyclesUsed));

  // Update mouse card
  if (mouse_) mouse_->update(static_cast<int>(cyclesUsed));

  // Update Z-80 SoftCard
  if (softcard_) softcard_->update(static_cast<int>(cyclesUsed));

  // Progressive rendering: render scanlines up to current cycle
  video_->renderUpToCycle(cpu_->getTotalCycles());

  // Check for frame boundary
  uint64_t currentCycle = cpu_->getTotalCycles();
  if (currentCycle - lastFrameCycle_ >= CYCLES_PER_FRAME) {
    lastFrameCycle_ += CYCLES_PER_FRAME;
    video_->renderFrame();
    video_->beginNewFrame(lastFrameCycle_);
    frameReady_ = true;
  }
}

uint8_t Emulator::readMemory(uint16_t address) const {
  return const_cast<MMU *>(mmu_.get())->read(address);
}

uint8_t Emulator::peekMemory(uint16_t address) const {
  return mmu_->peek(address);
}

void Emulator::writeMemory(uint16_t address, uint8_t value) {
  mmu_->write(address, value);
}

const char *Emulator::disassembleAt(uint16_t address) {
  disasmBuffer_ = cpu_->disassembleAt(address);
  return disasmBuffer_.c_str();
}

uint64_t Emulator::getSoftSwitchState() const {
  const auto &sw = mmu_->getSoftSwitches();
  uint64_t state = 0;

  // Pack soft switch state into a 64-bit value
  // Display switches (bits 0-5)
  if (sw.text)
    state |= (1ULL << 0);
  if (sw.mixed)
    state |= (1ULL << 1);
  if (sw.page2)
    state |= (1ULL << 2);
  if (sw.hires)
    state |= (1ULL << 3);
  if (sw.col80)
    state |= (1ULL << 4);
  if (sw.altCharSet)
    state |= (1ULL << 5);

  // Memory switches (bits 6-12)
  if (sw.store80)
    state |= (1ULL << 6);
  if (sw.ramrd)
    state |= (1ULL << 7);
  if (sw.ramwrt)
    state |= (1ULL << 8);
  if (sw.intcxrom)
    state |= (1ULL << 9);
  if (sw.altzp)
    state |= (1ULL << 10);
  if (sw.slotc3rom)
    state |= (1ULL << 11);
  if (sw.intc8rom)
    state |= (1ULL << 12);

  // Language card (bits 13-16)
  if (sw.lcram)
    state |= (1ULL << 13);
  if (sw.lcram2)
    state |= (1ULL << 14);
  if (sw.lcwrite)
    state |= (1ULL << 15);
  if (sw.lcprewrite)
    state |= (1ULL << 16);

  // Annunciators (bits 17-20)
  if (sw.an0)
    state |= (1ULL << 17);
  if (sw.an1)
    state |= (1ULL << 18);
  if (sw.an2)
    state |= (1ULL << 19);
  if (sw.an3)
    state |= (1ULL << 20);

  // I/O state (bits 21-23)
  if (sw.vblBar)
    state |= (1ULL << 21);
  if (sw.cassetteOut)
    state |= (1ULL << 22);
  if (sw.cassetteIn)
    state |= (1ULL << 23);

  // Buttons (bits 24-26)
  if (buttonState_[0])
    state |= (1ULL << 24);
  if (buttonState_[1])
    state |= (1ULL << 25);
  if (buttonState_[2])
    state |= (1ULL << 26);

  // Keyboard (bit 27)
  if (keyboardLatch_ & 0x80)
    state |= (1ULL << 27);

  // DHIRES (bit 28) - computed from AN3 off + 80COL + HIRES
  bool dhires = !sw.an3 && sw.col80 && sw.hires;
  if (dhires)
    state |= (1ULL << 28);

  // IOUDIS (bit 29)
  if (sw.ioudis)
    state |= (1ULL << 29);

  return state;
}

uint8_t Emulator::cpuRead(uint16_t address) { return mmu_->read(address); }

void Emulator::cpuWrite(uint16_t address, uint8_t value) {
  mmu_->write(address, value);
}

uint8_t Emulator::getKeyboardData() { return keyboardLatch_; }

void Emulator::clearKeyboardStrobe() {
  keyboardLatch_ &= 0x7F; // Clear high bit
}

void Emulator::toggleSpeaker() {
  audio_->toggleSpeaker(cpu_->getTotalCycles());
}

// ============================================================================
// Mouse Input
// ============================================================================

void Emulator::mouseMove(int dx, int dy) {
  if (mouse_) {
    mouse_->addDelta(dx, dy);
  }
}

void Emulator::mouseButton(bool pressed) {
  if (mouse_) {
    mouse_->setMouseButton(pressed);
  }
}

// ============================================================================
// Slot Management
// ============================================================================

const char* Emulator::getSlotCardName(uint8_t slot) const {
  if (slot < 1 || slot > 7) {
    return "invalid";
  }

  // Slot 3 is built-in 80-column
  if (slot == 3) {
    return "80col";
  }

  // Check the slot array for all cards
  ExpansionCard* card = mmu_->getCard(slot);
  if (!card) {
    return "empty";
  }

  // Identify card type by name
  const char* name = card->getName();
  if (strcmp(name, "Disk II") == 0) {
    return "disk2";
  }
  if (strcmp(name, "Mockingboard") == 0) {
    return "mockingboard";
  }
  if (strcmp(name, "Thunderclock") == 0) {
    return "thunderclock";
  }
  if (strcmp(name, "Mouse") == 0) {
    return "mouse";
  }
  if (strcmp(name, "SmartPort") == 0) {
    return "smartport";
  }
  if (strcmp(name, "Z-80 SoftCard") == 0) {
    return "softcard";
  }
  if (strcmp(name, "Super Serial Card") == 0) {
    return "ssc";
  }

  return "empty";
}

bool Emulator::setSlotCard(uint8_t slot, const char* cardId) {
  if (slot < 1 || slot > 7) {
    return false;
  }

  // Slot 3 is built-in 80-column and cannot be changed
  if (slot == 3) {
    return false;
  }

  // Before any slot change, clean up existing special card pointers.
  // insertCard() destroys the old card, so dangling pointers must be cleared.
  ExpansionCard* existing = mmu_->getCard(slot);
  if (existing) {
    const char* existingName = existing->getName();
    if (strcmp(existingName, "Mockingboard") == 0 && slot == 4) {
      // Move Mockingboard to storage so it can be restored later
      if (!mbStorage_) {
        mbStorage_ = mmu_->removeCard(4);
        mockingboard_ = nullptr;
        audio_->setMockingboard(nullptr);
      }
    } else if (strcmp(existingName, "Disk II") == 0 && slot == 6) {
      if (!diskStorage_) {
        diskStorage_ = mmu_->removeCard(6);
        disk_ = nullptr;
      }
    } else if (strcmp(existingName, "Mouse") == 0) {
      mouse_ = nullptr;
      mmu_->removeCard(slot);
    } else if (strcmp(existingName, "SmartPort") == 0) {
      smartport_ = nullptr;
      mmu_->removeCard(slot);
    } else if (strcmp(existingName, "Z-80 SoftCard") == 0) {
      softcard_ = nullptr;
      mmu_->removeCard(slot);
    } else if (strcmp(existingName, "Super Serial Card") == 0) {
      ssc_ = nullptr;
      mmu_->removeCard(slot);
    } else {
      mmu_->removeCard(slot);
    }
  }

  // Handle empty slot - cleanup above already removed the card
  if (strcmp(cardId, "empty") == 0) {
    return true;
  }

  // Handle Disk II card
  if (strcmp(cardId, "disk2") == 0) {
    if (slot != 6) {
      return false;
    }
    // Re-insert from storage
    if (diskStorage_) {
      disk_ = static_cast<Disk2Card*>(diskStorage_.get());
      mmu_->insertCard(6, std::move(diskStorage_));
    }
    return true;
  }

  // Handle Mockingboard card
  if (strcmp(cardId, "mockingboard") == 0) {
    if (slot != 4) {
      return false;
    }
    // Re-insert from storage
    if (mbStorage_) {
      mockingboard_ = static_cast<MockingboardCard*>(mbStorage_.get());
      mmu_->insertCard(4, std::move(mbStorage_));
      audio_->setMockingboard(mockingboard_);
    }
    return true;
  }

  // Handle Thunderclock card
  if (strcmp(cardId, "thunderclock") == 0) {
    auto card = std::make_unique<ThunderclockCard>();
    mmu_->insertCard(slot, std::move(card));
    return true;
  }

  // Handle Mouse card
  if (strcmp(cardId, "mouse") == 0) {
    auto card = std::make_unique<MouseCard>();
    card->setSlotNumber(slot);
    card->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
    card->setIRQCallback([this]() { cpu_->irq(); });
    mouse_ = card.get();
    mmu_->insertCard(slot, std::move(card));
    return true;
  }

  // Handle SmartPort card
  if (strcmp(cardId, "smartport") == 0) {
    auto card = std::make_unique<SmartPortCard>();
    card->setSlotNumber(slot);
    card->setMemReadCallback([this](uint16_t addr) { return mmu_->read(addr); });
    card->setMemWriteCallback([this](uint16_t addr, uint8_t val) { mmu_->write(addr, val); });
    card->setGetA([this]() { return cpu_->getA(); });
    card->setSetA([this](uint8_t v) { cpu_->setA(v); });
    card->setGetP([this]() { return cpu_->getP(); });
    card->setSetP([this](uint8_t v) { cpu_->setP(v); });
    card->setGetSP([this]() { return cpu_->getSP(); });
    card->setSetSP([this](uint8_t v) { cpu_->setSP(v); });
    card->setGetPC([this]() { return cpu_->getPC(); });
    card->setSetPC([this](uint16_t v) { cpu_->setPC(v); });
    card->setSetX([this](uint8_t v) { cpu_->setX(v); });
    card->setSetY([this](uint8_t v) { cpu_->setY(v); });
    smartport_ = card.get();
    mmu_->insertCard(slot, std::move(card));
    return true;
  }

  // Handle Z-80 SoftCard
  if (strcmp(cardId, "softcard") == 0) {
    auto card = std::make_unique<SoftCardZ80>();
    card->setSlotNumber(slot);
    card->setMemReadCallback([this](uint16_t addr) { return mmu_->read(addr); });
    card->setMemWriteCallback([this](uint16_t addr, uint8_t val) { mmu_->write(addr, val); });
    card->setCpuHaltCallback([this](bool halt) {
      // When Z80 activates, halt 6502; when Z80 deactivates, resume
      // The 6502 just idles — the card's update() runs Z80 cycles instead
    });
    softcard_ = card.get();
    mmu_->insertCard(slot, std::move(card));
    return true;
  }

  // Handle Super Serial Card
  if (strcmp(cardId, "ssc") == 0) {
    auto card = std::make_unique<SSCCard>();
    card->setSlotNumber(slot);
    card->setIRQCallback([this]() { cpu_->irq(); });
    ssc_ = card.get();
    mmu_->insertCard(slot, std::move(card));
    return true;
  }

  return false;
}

bool Emulator::isSlotEmpty(uint8_t slot) const {
  if (slot < 1 || slot > 7) {
    return true;
  }

  // Slot 3 is never empty (built-in 80-column)
  if (slot == 3) {
    return false;
  }

  return mmu_->isSlotEmpty(slot);
}

// State serialization (exportState / importState) is in emulator_state.cpp

// ============================================================================
// SmartPort Hard Drive Management
// ============================================================================

bool Emulator::insertSmartPortImage(int device, const uint8_t* data, size_t size, const char* filename) {
  if (!smartport_) return false;
  return smartport_->insertImage(device, data, size, filename ? filename : "");
}

void Emulator::ejectSmartPortImage(int device) {
  if (smartport_) smartport_->ejectImage(device);
}

bool Emulator::isSmartPortImageInserted(int device) const {
  if (!smartport_) return false;
  return smartport_->isImageInserted(device);
}

const char* Emulator::getSmartPortImageFilename(int device) const {
  if (!smartport_) return nullptr;
  const auto& fn = smartport_->getImageFilename(device);
  return fn.empty() ? nullptr : fn.c_str();
}

bool Emulator::isSmartPortImageModified(int device) const {
  if (!smartport_) return false;
  return smartport_->isImageModified(device);
}

const uint8_t* Emulator::exportSmartPortImageData(int device, size_t* size) const {
  if (!smartport_) {
    if (size) *size = 0;
    return nullptr;
  }
  return smartport_->exportImageData(device, size);
}

const uint8_t* Emulator::getSmartPortBlockData(int device, size_t* size) const {
  if (!smartport_) {
    if (size) *size = 0;
    return nullptr;
  }
  return smartport_->getBlockData(device, size);
}

// ==========================================================================
// Super Serial Card
// ==========================================================================

void Emulator::serialReceive(uint8_t byte) {
  if (ssc_) {
    ssc_->serialReceive(byte);
  }
}

void Emulator::setSerialTxCallback(SSCCard::SerialTxCallback cb) {
  if (ssc_) {
    ssc_->setSerialTxCallback(std::move(cb));
  }
}

// ==========================================================================
// Screen text extraction
// ==========================================================================

// Apple II text screen row base addresses (non-linear memory layout)
static constexpr uint16_t TEXT_ROW_BASES[24] = {
  0x400, 0x480, 0x500, 0x580, 0x600, 0x680, 0x700, 0x780,
  0x428, 0x4A8, 0x528, 0x5A8, 0x628, 0x6A8, 0x728, 0x7A8,
  0x450, 0x4D0, 0x550, 0x5D0, 0x650, 0x6D0, 0x750, 0x7D0
};

int Emulator::screenCodeToAscii(uint8_t code) {
  if (code >= 0xE0) return code - 0x80;        // Normal lowercase
  if (code >= 0xC0) return code - 0x80;        // Normal uppercase / MouseText
  if (code >= 0xA0) return code - 0x80;        // Normal symbols/digits
  if (code >= 0x80) return code - 0x40;        // Normal uppercase @A-Z[\]^_
  if (code >= 0x60) return code - 0x40;        // Flash symbols
  if (code >= 0x40) return code;               // Flash uppercase
  if (code >= 0x20) return code;               // Inverse symbols/digits
  return code + 0x40;                          // Inverse uppercase
}

const char* Emulator::readScreenText(int startRow, int startCol,
                                     int endRow, int endCol) {
  screenTextBuffer_.clear();

  const auto& sw = mmu_->getSoftSwitches();
  if (!sw.text) {
    return screenTextBuffer_.c_str();
  }

  bool col80 = sw.col80;
  bool page2 = sw.page2;
  int cols = col80 ? 80 : 40;
  uint16_t page2Offset = page2 ? 0x400 : 0;

  // Clamp
  if (startRow < 0) startRow = 0;
  if (startCol < 0) startCol = 0;
  if (endRow > 23) endRow = 23;
  if (endCol >= cols) endCol = cols - 1;

  for (int row = startRow; row <= endRow; row++) {
    int colStart = (row == startRow) ? startCol : 0;
    int colEnd = (row == endRow) ? endCol : cols - 1;

    int lineEnd = colEnd; // Track last non-space for trimming

    // First pass: find last non-space character for trimming
    for (int col = colEnd; col >= colStart; col--) {
      uint8_t charCode;
      if (col80) {
        int memCol = col / 2;
        bool isAux = (col % 2) == 0;
        uint16_t addr = TEXT_ROW_BASES[row] + page2Offset + memCol;
        charCode = isAux ? mmu_->peekAux(addr) : mmu_->peek(addr);
      } else {
        uint16_t addr = TEXT_ROW_BASES[row] + page2Offset + col;
        charCode = mmu_->peek(addr);
      }
      int ascii = screenCodeToAscii(charCode);
      if (ascii != 0x20) {
        lineEnd = col;
        break;
      }
      if (col == colStart) {
        lineEnd = colStart - 1; // All spaces
      }
    }

    // Second pass: emit characters up to lineEnd
    for (int col = colStart; col <= lineEnd; col++) {
      uint8_t charCode;
      if (col80) {
        int memCol = col / 2;
        bool isAux = (col % 2) == 0;
        uint16_t addr = TEXT_ROW_BASES[row] + page2Offset + memCol;
        charCode = isAux ? mmu_->peekAux(addr) : mmu_->peek(addr);
      } else {
        uint16_t addr = TEXT_ROW_BASES[row] + page2Offset + col;
        charCode = mmu_->peek(addr);
      }
      int ascii = screenCodeToAscii(charCode);
      screenTextBuffer_ += static_cast<char>(ascii);
    }

    if (row < endRow) {
      screenTextBuffer_ += '\n';
    }
  }

  return screenTextBuffer_.c_str();
}

} // namespace a2e
