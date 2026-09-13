/*
 * emulator.cpp - Core emulator coordinator tying together CPU, memory, video, audio, and peripherals
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "emulator.hpp"
#include "cards/disk2/disk2_card.hpp"
#include "cards/iwm/iwm.hpp"
#include "cards/mockingboard/mockingboard_card.hpp"
#include "cards/thunderclock/thunderclock_card.hpp"
#include "cards/mouse/mouse_card.hpp"
#include "cards/serial/serial_port.hpp"
#include "iigs/iigs_spec.hpp"
#include "cards/smartport/smartport_card.hpp"
#include "cards/softcard/softcard_z80.hpp"
#include "debug/condition_evaluator.hpp"
#include "filesystem/dos33.hpp"
#include "filesystem/prodos.hpp"
#include <algorithm>
#include <cstring>

// Include generated ROM data directly
#include "roms.cpp" // namespace roms

namespace a2e {

Emulator::Emulator(MachineId machine) : machine_(&machineProfile(machine)) {
  mmu_ = std::make_unique<MMU>(*machine_);
  video_ = std::make_unique<Video>(*mmu_);
  audio_ = std::make_unique<Audio>(*machine_);
  keyboard_ = std::make_unique<Keyboard>();

  // Create cards, keep raw pointers, then insert into slots. Which drive
  // controller gets built is the machine's: a //e and a II+ take a Disk II
  // card in a slot, a //c has an IWM on the board with no ROM of its own.
  std::unique_ptr<DiskController> disk;
  if (const char *slot6 = machine_->slots[6].fixedCard;
      slot6 && strcmp(slot6, "iwm") == 0) {
    disk = std::make_unique<IWM>();
  } else {
    disk = std::make_unique<Disk2Card>();
  }
  auto mb = std::make_unique<MockingboardCard>();
  disk_ = disk.get();
  mockingboard_ = mb.get();

  // Create CPU with memory callbacks
  cpu_ = std::make_unique<CPU6502>(
      [this](uint16_t addr) { return cpuRead(addr); },
      [this](uint16_t addr, uint8_t val) { cpuWrite(addr, val); },
      machine_->cpu);

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

  // Whether anything is currently pulling the interrupt line down.
  //
  // The CPU samples this once per instruction, so it is deliberately a handful
  // of null checks against pointers this class already holds rather than a walk
  // of the slots asking each card: the slot array is eight virtual calls on the
  // hottest loop in the emulator, to serve devices that can be counted on one
  // hand. A card that can hold the line and is not named here is still heard
  // through its own edge — it just cannot re-interrupt a handler that returned
  // without servicing it.
  cpu_->setIRQStatusCallback([this]() {
    if (mockingboard_ && mockingboard_->isIRQActive()) return true;
    if (mouse_ && mouse_->isIRQActive()) return true;
    if (mouseIOU_ && mouseIOU_->isIRQActive()) return true;
    if (ssc_ && ssc_->isIRQActive()) return true;
    for (const SerialPort *port : serialPorts_) {
      if (port && port->isIRQActive()) return true;
    }
    return false;
  });

  // A //c's mouse is not a card in slot 4; it is the IOU, and the profile
  // names it in slot 4 only because that is where its firmware lives. A
  // machine with sockets gets a MouseCard instead, when the user fits one.
  if (!machine_->caps.hasExpansionSlots && machine_->slots[4].fixedCard &&
      strcmp(machine_->slots[4].fixedCard, "mouse") == 0) {
    mouseIOU_ = std::make_unique<MouseIOU>();
    mouseIOU_->setIRQCallback([this]() { cpu_->irq(); });
    mmu_->setMouseIOU(mouseIOU_.get());
  }

  // Set up disk timing callback - allows disk reads to get accurate cycle count
  // during instruction execution (before disk_->update() is called)
  disk_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });

  // Fit the cards this machine ships with. Which slots those are is the
  // machine's business — a //e comes with a Mockingboard in 4 and a Disk II in
  // 6, a II+ with only the Disk II — so the profile decides rather than this
  // constructor. Anything the machine does not ship is left constructed but
  // unfitted, so the host can install it later without rebuilding.
  for (int slot = machine_->firstSlot; slot <= machine_->lastSlot; slot++) {
    const char *card = machine_->slots[slot].defaultCard;
    if (!card) continue;
    if ((strcmp(card, "disk2") == 0 || strcmp(card, "iwm") == 0) && disk) {
      mmu_->insertCard(static_cast<uint8_t>(slot), std::move(disk));
    } else if (strcmp(card, "mockingboard") == 0 && mb) {
      mmu_->insertCard(static_cast<uint8_t>(slot), std::move(mb));
    } else if (strcmp(card, "serial1") == 0 || strcmp(card, "serial2") == 0) {
      // A //c's two ports. Built here rather than kept in storage like the
      // disk and the Mockingboard, because they are soldered to the board:
      // there is no state in which the machine exists without them.
      const uint8_t port = card[6] == '2' ? 2 : 1;
      auto serial = std::make_unique<SerialPort>(port);
      serial->setIRQCallback([this]() { cpu_->irq(); });
      if (serialTxCallback_) serial->setSerialTxCallback(serialTxCallback_);
      serialPorts_[port - 1] = serial.get();
      mmu_->insertCard(static_cast<uint8_t>(slot), std::move(serial));
    }
  }

  // A card this machine does not ship is parked rather than dropped. Both are
  // still pointed at by disk_ and mockingboard_, and setSlotCard() fits them
  // later from exactly these members, so letting either go out of scope here
  // would leave those pointers dangling.
  if (disk) diskStorage_ = std::move(disk);
  if (mb) mbStorage_ = std::move(mb);

  // Audio gets raw pointer
  audio_->setMockingboard(mockingboard_);
  applySpeedToAudio();
}

Emulator::~Emulator() = default;

namespace {

// The system ROM built in for a machine, or an empty span when there is none.
struct SystemRoms {
  const uint8_t *system;
  size_t systemSize;
  const uint8_t *chars;
  size_t charSize;
};

SystemRoms romsFor(MachineId machine) {
  // Named one at a time rather than defaulted: the fallback is the //e's ROM,
  // and a machine that fell through to it would boot someone else's firmware
  // and look like it worked.
  switch (machine) {
  case MachineId::AppleIIPlus:
    return {roms::ROM_SYSTEM_II_PLUS, roms::ROM_SYSTEM_II_PLUS_SIZE,
            roms::ROM_CHAR_II_PLUS, roms::ROM_CHAR_II_PLUS_SIZE};
  case MachineId::AppleIIc:
    return {roms::ROM_SYSTEM_IIC, roms::ROM_SYSTEM_IIC_SIZE,
            roms::ROM_CHAR_IIC, roms::ROM_CHAR_IIC_SIZE};
  case MachineId::AppleIIgs:
    // A IIgs's character generator is in neither of the places the other
    // machines keep theirs: not a part of its own, and not in the system ROM
    // either — searching a ROM 01 image for so much as one glyph finds
    // nothing. It is inside the video chip, which the CPU cannot read.
    //
    // So the machine is given the //e's set, which is the same font: the
    // enhanced //e, the //c and the IIgs draw the same characters, MouseText
    // included. Without it every glyph is blank and the screen shows solid
    // bars where the text should be.
    return {roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE, roms::ROM_CHAR,
            roms::ROM_CHAR_SIZE};
  case MachineId::AppleIIe:
    break;
  }
  return {roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE, roms::ROM_CHAR,
          roms::ROM_CHAR_SIZE};
}

} // namespace

const uint8_t *Emulator::systemROMFor(MachineId machine, size_t &size) {
  const SystemRoms rom = romsFor(machine);
  size = rom.systemSize;
  return rom.system;
}

const uint8_t *Emulator::characterROMFor(MachineId machine, size_t &size) {
  const SystemRoms rom = romsFor(machine);
  size = rom.charSize;
  return rom.chars;
}

bool Emulator::isMachineRunnable(MachineId machine) {
  // A IIgs is not built from this class's parts — it has its own coordinator,
  // IIgsMachine, and the host builds that instead when the machine is a IIgs.
  // What decides whether it can be started is the same thing that decides for
  // every other machine: whether its ROM is in the build. The IIgs's ROM is
  // banked and its profile describes a 64KB window rather than the whole
  // image, so the size to expect is the image's own.
  if (machineProfile(machine).family == MachineFamily::AppleIIgs) {
    return romsFor(machine).systemSize >= iigs::ROM_SIZE_ROM01;
  }

  // A machine's ROM has to actually fill the space its profile claims. A short
  // image would leave the reset vector reading whatever the array was
  // initialised to, which looks like a running machine that immediately goes
  // nowhere.
  return romsFor(machine).systemSize >= machineProfile(machine).memory.romSize;
}

void Emulator::init() {
  // Each machine has its own system and character ROMs. The II+ set is
  // optional at build time (see scripts/generate_roms.sh): when its files are
  // absent the arrays are empty, and rather than leave the machine silently
  // running a //e's ROM — or nothing — we record that it has none. The host
  // asks through hasSystemROM() and can say so instead of presenting a machine
  // that will never reach a prompt.
  const SystemRoms rom = romsFor(machine_->id);
  systemRomLoaded_ = isMachineRunnable(machine_->id);

  if (systemRomLoaded_) {
    mmu_->loadROM(rom.system, rom.systemSize, rom.chars, rom.charSize);
  }

  // The P5A boot ROM belongs to the Disk II card. A //c's IWM has no ROM
  // space of its own — its disk firmware is part of the system ROM — so there
  // is nothing to load into one.
  if (auto *card = dynamic_cast<Disk2Card *>(disk_)) {
    card->loadROM(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
  }

  reset();
}

void Emulator::reset() {
  mmu_->reset();
  cpu_->resetCycleCount(); // Clear cycle counter for fresh power-on state
  cpu_->reset();
  audio_->reset();
  if (disk_) disk_->reset();
  if (mouseIOU_) mouseIOU_->reset();
  keyboard_->reset();
  if (mockingboard_) mockingboard_->reset();

  // Clear Apple button states
  setButton(0, false);
  setButton(1, false);
  joyport_.reset();
  joyportResetGuardCycle_ = cpu_->getTotalCycles() + JOYPORT_RESET_GUARD_CYCLES;

  keyboardLatch_ = 0;
  keyDown_ = false;
  clearPasteBuffer();
  // speedMultiplier_ is deliberately left alone: it is a host preference (the
  // user's chosen clock speed, or a paste boost in flight), not machine state,
  // so a reset must not drop the machine back to 1 MHz behind their back.
  lastFrameCycle_ = 0;
  samplesGenerated_ = 0;
  frameReady_ = false;
  debug_.reset();
  // Keep beam breakpoints across reset (same as regular breakpoints)
  // MachineDebug::reset() above has already released every beam breakpoint
  // for the new frame.
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
  joyport_.reset();
  joyportResetGuardCycle_ = cpu_->getTotalCycles() + JOYPORT_RESET_GUARD_CYCLES;

  // Reset video to clean frame state
  video_->beginNewFrame(cpu_->getTotalCycles());

  // Clear debugger hit flags
  debug_.reset();

  // Clear BASIC debugger state
  basicBreakpointHit_ = false;
  basicStepMode_ = BasicStepMode::None;
  basicBreakLine_ = 0;

  paused_ = false;
  frameReady_ = false;
  samplesGenerated_ = 0;
}

void Emulator::setPaused(bool paused) {
  if (!paused && paused_ && debug_.isBreakpointHit()) {
    debug_.skipNextBreakpoint();
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
  debug_.clearHits();
  basicBreakpointHit_ = false;
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
      if (mouseIOU_)
        mouseIOU_->update(cpu_->getTotalCycles(), mmu_->isInVerticalBlank());
      softcard_->update(static_cast<int>(cyclesUsed));
      if (parallelCard_) parallelCard_->update(static_cast<int>(cyclesUsed));

      // Progressive rendering and frame boundary
      video_->renderUpToCycle(cpu_->getTotalCycles());
      uint64_t currentCycle = cpu_->getTotalCycles();
      if (currentCycle - lastFrameCycle_ >=
          static_cast<uint64_t>(machine_->timing.cyclesPerFrame())) {
        lastFrameCycle_ += machine_->timing.cyclesPerFrame();
        video_->renderFrame();
        video_->beginNewFrame(lastFrameCycle_);
        frameReady_ = true;
      }
      continue;
    }

    // Breakpoints, the temporary one behind step over and step out included
    if (debug_.shouldBreakBefore(cpu_->getPC())) {
      paused_ = true;
      return;
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
        // If we were stepping when the program ended, complete the step
        // so the UI doesn't stay stuck in "running" state
        if (basicStepMode_ != BasicStepMode::None) {
          basicStepMode_ = BasicStepMode::None;
          basicBreakpointHit_ = true;
          basicBreakLine_ = 0;
          paused_ = true;
          return;
        }
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
    if (debug_.isTraceEnabled()) recordTrace();

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
    if (mouseIOU_)
      mouseIOU_->update(cpu_->getTotalCycles(), mmu_->isInVerticalBlank());

    // Update Z-80 SoftCard (runs Z80 T-states when active)
    if (softcard_) softcard_->update(static_cast<int>(cyclesUsed));

    // Update parallel card Centronics BUSY/ACK handshake timer
    if (parallelCard_) parallelCard_->update(static_cast<int>(cyclesUsed));

    // Hand the next pasted character to the keyboard once its wait is up.
    // Nothing else would: the strobe is already clear, so no keyboard access
    // is coming to prompt it.
    if (!pasteBuffer_.empty()) loadNextPasteKey();

    // Progressive rendering: render scanlines up to current cycle
    video_->renderUpToCycle(cpu_->getTotalCycles());

    // Check for frame boundary
    uint64_t currentCycle = cpu_->getTotalCycles();
    if (currentCycle - lastFrameCycle_ >=
        static_cast<uint64_t>(machine_->timing.cyclesPerFrame())) {
      // Advance by exactly one frame to stay aligned with VBL detection ($C019
      // uses cycles modulo the frame length). Using currentCycle would drift
      // by a few cycles each frame, desynchronizing raster effects.
      lastFrameCycle_ += machine_->timing.cyclesPerFrame();
      video_->renderFrame();                   // Uses this frame's change log
      video_->beginNewFrame(lastFrameCycle_);   // Reset log, aligned to frame boundary
      frameReady_ = true;
    }

    // A watchpoint hit during the instruction, through the MMU's callbacks
    if (debug_.isWatchpointHit()) return;

    // Beam breakpoints, measured from the start of the frame in progress
    if (debug_.hasBeamBreakpoints()) {
      uint64_t frameCycle = cpu_->getTotalCycles() - lastFrameCycle_;
      const auto perFrame =
          static_cast<uint64_t>(machine_->timing.cyclesPerFrame());
      if (frameCycle >= perFrame) frameCycle %= perFrame;
      const int scanline =
          static_cast<int>(frameCycle / machine_->timing.cyclesPerScanline);
      const int hPos =
          static_cast<int>(frameCycle % machine_->timing.cyclesPerScanline);
      if (debug_.shouldBreakAtBeam(lastFrameCycle_, scanline, hPos)) {
        paused_ = true;
        return;
      }
    }
  }
}

void Emulator::setSpeedMultiplier(int multiplier) {
  if (multiplier < 1) multiplier = 1;
  if (multiplier > 8) multiplier = 8;
  speedMultiplier_ = multiplier;
  // Both sound sources measure their output rate in CPU cycles, so both have
  // to know the machine is running fast — otherwise the speaker discards most
  // of its window as implausible and the Mockingboard builds a backlog.
  applySpeedToAudio();
}

void Emulator::applySpeedToAudio() {
  if (audio_) audio_->setSpeedMultiplier(speedMultiplier_);
  if (mockingboard_) mockingboard_->setSpeedMultiplier(speedMultiplier_);
}

int Emulator::generateStereoAudioSamples(float *buffer, int sampleCount) {
  // Calculate cycles needed for this audio buffer, scaled by speed multiplier
  int cyclesToRun = static_cast<int>(
      sampleCount * machine_->timing.cyclesPerSample(AUDIO_SAMPLE_RATE) *
      speedMultiplier_);

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
                                bool alt, bool meta, bool capsLock,
                                int keyLocation) {
  int result = keyboard_->handleKeyDown(browserKeycode, shift, ctrl, alt, meta,
                                        capsLock, keyLocation);

  // Update button state from modifier keys
  setButton(0, keyboard_->isOpenApplePressed());   // Open Apple
  setButton(1, keyboard_->isClosedApplePressed()); // Closed Apple

  return result;
}

void Emulator::handleRawKeyUp(int browserKeycode, bool shift, bool ctrl,
                              bool alt, bool meta, int keyLocation) {
  keyboard_->handleKeyUp(browserKeycode, shift, ctrl, alt, meta, keyLocation);
  // The released key stops asserting AKD. Without this the line stayed high
  // from the first keystroke of the session onwards, so anything polling
  // $C010 for a still-held key — key repeat, game input — saw one forever.
  updateAnyKeyDown();

  // Update button state from modifier keys
  setButton(0, keyboard_->isOpenApplePressed());   // Open Apple
  setButton(1, keyboard_->isClosedApplePressed()); // Closed Apple
}

void Emulator::releaseModifiers() {
  keyboard_->releaseModifiers();
  updateAnyKeyDown();
  setButton(0, keyboard_->isOpenApplePressed());
  setButton(1, keyboard_->isClosedApplePressed());
}

void Emulator::keyDown(int keycode) {
  // Direct Apple II keycode input.
  // Sets the keyboard latch with high bit set.
  keyboardLatch_ = (keycode & 0x7F) | 0x80;
  // A key press wins the latch outright; whatever the paste buffer had put
  // there has been overwritten, so it no longer owns AKD.
  pasteHoldsKey_ = false;
  updateAnyKeyDown();
}

void Emulator::keyUp(int keycode) {
  (void)keycode;
  updateAnyKeyDown();
}

void Emulator::updateAnyKeyDown() {
  keyDown_ = pasteHoldsKey_ || (keyboard_ && keyboard_->isAnyKeyDown());
}

// ============================================================================
// Paste / typed-text buffer
// ============================================================================

size_t Emulator::pasteText(const char *utf8) {
  if (!utf8) return 0;

  size_t queued = 0;
  const auto *p = reinterpret_cast<const unsigned char *>(utf8);

  while (*p) {
    // Decode one UTF-8 code point. charToAppleKey() takes a code point, and
    // the host used to hand it one per character, so text pasted from a
    // browser must be decoded the same way rather than byte by byte.
    uint32_t cp = *p;
    int extra = 0;
    if (cp >= 0xF0) { cp &= 0x07; extra = 3; }
    else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
    else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
    else if (cp >= 0x80) { cp = 0xFFFD; }  // stray continuation byte
    ++p;
    for (int i = 0; i < extra && (*p & 0xC0) == 0x80; ++i) {
      cp = (cp << 6) | (*p & 0x3F);
      ++p;
    }

    int key = charToAppleKey(static_cast<int>(cp));
    if (key >= 0) {
      pasteBuffer_.push_back(static_cast<uint8_t>(key & 0x7F));
      ++queued;
    }
  }

  loadNextPasteKey();
  return queued;
}

void Emulator::pasteKey(int appleKey) {
  if (appleKey < 0) return;
  pasteBuffer_.push_back(static_cast<uint8_t>(appleKey & 0x7F));
  loadNextPasteKey();
}

void Emulator::clearPasteBuffer() {
  pasteBuffer_.clear();
  pasteReadyCycle_ = 0;
  if (pasteHoldsKey_) {
    pasteHoldsKey_ = false;
    updateAnyKeyDown();
  }
}

void Emulator::loadNextPasteKey() {
  // Only ever fill an empty latch. While the strobe is still set the program
  // has not read the previous character, and overwriting it would lose keys —
  // the buffer exists precisely so that cannot happen.
  if (keyboardLatch_ & 0x80) return;
  if (pasteBuffer_.empty()) {
    if (pasteHoldsKey_) {
      // The last buffered key has been read: it stops holding AKD.
      pasteHoldsKey_ = false;
      updateAnyKeyDown();
    }
    return;
  }

  // Still inside the gap after the previous character. runCycles() calls back
  // here as time passes, so the key appears on its own once the wait is over,
  // with no keyboard access needed to prompt it.
  if (cpu_->getTotalCycles() < pasteReadyCycle_) return;

  keyboardLatch_ = pasteBuffer_.front() | 0x80;
  pasteBuffer_.pop_front();
  pasteHoldsKey_ = true;
  updateAnyKeyDown();
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

void Emulator::setGamePortDevice(GamePortDevice device) {
  if (device == gamePortDevice_) return;
  gamePortDevice_ = device;
  // Whatever was held on the old device is not held on the new one, and a
  // switch mid-game would otherwise leave a direction stuck down.
  joyport_.reset();
  setButton(0, false);
  setButton(1, false);
  setButton(2, false);
}

void Emulator::setJoyportStick(int stick, int switches) {
  joyport_.setStickState(stick, static_cast<uint8_t>(switches));
}

uint8_t Emulator::getButtonState(int button) {
  if (gamePortDevice_ == GamePortDevice::SiriusJoyport) {
    // See joyportResetGuardCycle_: the Joyport's idle-high PB0/PB1 read as a
    // held Open and Closed Apple, which sends the //e's reset routine into the
    // self test. Let go of those two lines until the ROM has looked.
    if (button < 2 && cpu_->getTotalCycles() < joyportResetGuardCycle_) {
      return 0x00;
    }
    const SoftSwitches &sw = mmu_->getSoftSwitches();
    return joyport_.readPushButton(button, sw.an0, sw.an1);
  }
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

const uint8_t *Emulator::exportDiskDataAs(int drive, DiskSaveFormat format,
                                          size_t *size) {
  if (size) *size = 0;
  if (!disk_) return nullptr;

  DiskImage *image = disk_->getMutableDiskImage(drive);
  if (!image || !image->isLoaded()) return nullptr;

  if (!DiskConverter::convert(*image, format, diskExportBuffer_)) {
    return nullptr;
  }

  if (size) *size = diskExportBuffer_.size();
  return diskExportBuffer_.data();
}

const uint8_t *Emulator::getDiskSectorsDOSOrder(int drive, size_t *size) {
  if (size) *size = 0;
  if (!disk_) return nullptr;

  DiskImage *image = disk_->getMutableDiskImage(drive);
  if (!image || !image->isLoaded()) return nullptr;

  if (!DiskConverter::convert(*image, DiskSaveFormat::DOSOrder,
                              diskSectorBuffer_)) {
    return nullptr;
  }

  if (size) *size = diskSectorBuffer_.size();
  return diskSectorBuffer_.data();
}

bool Emulator::canExportDiskAs(int drive, DiskSaveFormat format) {
  if (!disk_) return false;
  DiskImage *image = disk_->getMutableDiskImage(drive);
  if (!image) return false;
  return DiskConverter::canConvert(*image, format);
}

DiskSaveFormat Emulator::getDiskNativeFormat(int drive) {
  if (!disk_) return DiskSaveFormat::DOSOrder;
  const DiskImage *image = disk_->getDiskImage(drive);
  if (!image) return DiskSaveFormat::DOSOrder;
  return DiskConverter::nativeFormat(*image);
}

FsWriteStatus Emulator::writeBinaryFileToDisk(int drive, const char *filename,
                                              uint16_t loadAddress,
                                              const uint8_t *data, size_t len) {
  if (!disk_) return FsWriteStatus::NoDisk;

  DiskImage *image = disk_->getMutableDiskImage(drive);
  if (!image || !image->isLoaded()) return FsWriteStatus::NoDisk;
  if (image->isWriteProtected()) return FsWriteStatus::WriteProtected;

  size_t sectorSize = 0;
  const uint8_t *sectors = image->getSectorData(&sectorSize);
  if (!sectors || sectorSize == 0) return FsWriteStatus::UnsupportedImage;

  // Work on a copy: the filesystem writer must not touch the live image unless
  // it succeeds, and writeSectorData is what makes a change visible to the
  // drive anyway.
  std::vector<uint8_t> working(sectors, sectors + sectorSize);

  // Which filesystem is on the disk is decided by the disk, not the file
  // extension — a .dsk holding a ProDOS volume is common.
  FsWriteStatus status;
  if (DOS33::isDOS33(working.data(), working.size())) {
    status = DOS33::writeBinaryFile(working.data(), working.size(), filename,
                                    loadAddress, data, len);
  } else if (ProDOS::isProDOS(working.data(), working.size())) {
    status = ProDOS::writeFile(working.data(), working.size(), filename, 0x06,
                               loadAddress, data, static_cast<uint32_t>(len));
  } else {
    return FsWriteStatus::NotFormatted;
  }

  if (status != FsWriteStatus::OK) return status;

  if (!image->writeSectorData(0, working.data(), working.size())) {
    // A bit-stream image (WOZ) has no sector data to replace
    return FsWriteStatus::UnsupportedImage;
  }

  return FsWriteStatus::OK;
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
  debug_.clearHits();

  // Record trace before execution
  if (debug_.isTraceEnabled()) recordTrace();

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
  if (mouseIOU_)
    mouseIOU_->update(cpu_->getTotalCycles(), mmu_->isInVerticalBlank());

  // Update Z-80 SoftCard
  if (softcard_) softcard_->update(static_cast<int>(cyclesUsed));

  // Update parallel card Centronics BUSY/ACK handshake timer
  if (parallelCard_) parallelCard_->update(static_cast<int>(cyclesUsed));

  // Progressive rendering: render scanlines up to current cycle
  video_->renderUpToCycle(cpu_->getTotalCycles());

  // Check for frame boundary
  uint64_t currentCycle = cpu_->getTotalCycles();
  if (currentCycle - lastFrameCycle_ >=
      static_cast<uint64_t>(machine_->timing.cyclesPerFrame())) {
    lastFrameCycle_ += machine_->timing.cyclesPerFrame();
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
  return packSoftSwitchState(mmu_->getSoftSwitches(), buttonState_[0],
                             buttonState_[1], buttonState_[2],
                             (keyboardLatch_ & 0x80) != 0);
}

uint8_t Emulator::cpuRead(uint16_t address) { return mmu_->read(address); }

void Emulator::cpuWrite(uint16_t address, uint8_t value) {
  mmu_->write(address, value);
}

uint8_t Emulator::getKeyboardData() { return keyboardLatch_; }

void Emulator::clearKeyboardStrobe() {
  const bool tookPastedKey = pasteHoldsKey_ && (keyboardLatch_ & 0x80);
  const uint8_t consumed = keyboardLatch_ & 0x7F;

  keyboardLatch_ &= 0x7F; // Clear high bit

  if (tookPastedKey) {
    // The character has been taken. Hold the next one back for a moment: a
    // flush arriving right behind the read must find the keyboard empty, the
    // way it would if a person were typing. A carriage return waits longer,
    // because the machine has the line to process before it wants more.
    pasteHoldsKey_ = false;
    const uint64_t now = cpu_->getTotalCycles();
    pasteReadyCycle_ =
        now + (consumed == 0x0D ? PASTE_LINE_GAP_CYCLES : PASTE_KEY_GAP_CYCLES);
    updateAnyKeyDown();
  }

  loadNextPasteKey();
}

void Emulator::toggleSpeaker() {
  audio_->toggleSpeaker(cpu_->getTotalCycles());
}

// ============================================================================
// Mouse Input
// ============================================================================

void Emulator::mouseMove(int dx, int dy) {
  // Whichever mouse this machine has. The host asks the machine to move a
  // mouse; what is on the other end of the question — a card's PIA or a //c's
  // IOU — is not its business.
  if (mouse_) {
    mouse_->addDelta(dx, dy);
  }
  if (mouseIOU_) {
    mouseIOU_->addDelta(dx, dy);
  }
}

void Emulator::mouseButton(bool pressed) {
  if (mouse_) {
    mouse_->setMouseButton(pressed);
  }
  if (mouseIOU_) {
    mouseIOU_->setButton(pressed);
  }
}

// ============================================================================
// Slot Management
// ============================================================================

const char* Emulator::getSlotCardName(uint8_t slot) const {
  if (!machine_->hasSlot(slot)) {
    return "invalid";
  }

  // A slot the machine fills itself. On a //e that is the 80-column card in
  // slot 3; on a II+ it is the language card in slot 0, and slot 3 is an
  // ordinary slot. Reporting "80col" for every machine's slot 3 put a card in
  // a II+ that it has never had.
  if (const char *fixed = machine_->slots[slot].fixedCard) {
    return fixed;
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
  if (strcmp(name, "IWM") == 0) {
    return "iwm";
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
  if (strcmp(name, "Serial Port 1") == 0) {
    return "serial1";
  }
  if (strcmp(name, "Serial Port 2") == 0) {
    return "serial2";
  }
  if (strcmp(name, "Parallel Card") == 0) {
    return "parallel";
  }

  return "empty";
}

bool Emulator::setSlotCard(uint8_t slot, const char* cardId) {
  if (slot < 1 || slot > 7) {
    return false;
  }

  // A slot the machine fills itself is not the user's to change: a //e's
  // 80-column card in slot 3, and on a //c every slot there is, because none
  // of them is a socket. This used to be a bare `slot == 3`, which was the //e
  // spelling of the same rule and would have let a caller pull the IWM out of
  // a machine that has no way to put one back.
  if (machine_->slots[slot].fixedCard) {
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
    } else if ((strcmp(existingName, "Disk II") == 0 ||
                strcmp(existingName, "IWM") == 0) &&
               slot == 6) {
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
    } else if (strcmp(existingName, "Parallel Card") == 0) {
      parallelCard_ = nullptr;
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
      disk_ = static_cast<DiskController*>(diskStorage_.get());
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
      // A card inserted while the machine is accelerated has to start at the
      // current speed, not 1x.
      applySpeedToAudio();
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
    card->setGetSP([this]() { return static_cast<uint16_t>(0x0100 | cpu_->getSP()); });
    card->setSetSP([this](uint16_t v) { cpu_->setSP(static_cast<uint8_t>(v)); });
    // A 6502 fetches with `read(pc_++)`, so by the time a read reaches a card
    // the counter has already moved past the opcode.
    card->setExecutingAt([this](uint16_t address) {
      return cpu_->getPC() == static_cast<uint16_t>(address + 1);
    });
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
    if (serialTxCallback_) card->setSerialTxCallback(serialTxCallback_);
    ssc_ = card.get();
    mmu_->insertCard(slot, std::move(card));
    return true;
  }

  // Handle Parallel Interface Card
  if (strcmp(cardId, "parallel") == 0) {
    auto card = std::make_unique<ParallelCard>();
    card->setSlotNumber(slot);
    if (parallelTxCallback_) card->setParallelTxCallback(parallelTxCallback_);
    parallelCard_ = card.get();
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
    return;
  }
  // On a //c a byte coming in from outside is arriving at the modem port,
  // which is port 2 — port 1 is the printer port, and a printer does not talk
  // back. A machine with only a port 1 would still be given it rather than
  // dropping it on the floor.
  if (serialPorts_[1]) {
    serialPorts_[1]->serialReceive(byte);
  } else if (serialPorts_[0]) {
    serialPorts_[0]->serialReceive(byte);
  }
}

void Emulator::setSerialTxCallback(SSCCard::SerialTxCallback cb) {
  serialTxCallback_ = cb;
  if (ssc_) {
    ssc_->setSerialTxCallback(cb);
  }
  // Both //c ports transmit to the same host callback, which is what the SSC
  // does for all three of its uses: what is on the other end — a printer, a
  // modem, a terminal — is the host's business, not the chip's.
  for (SerialPort *port : serialPorts_) {
    if (port) port->setSerialTxCallback(cb);
  }
}

void Emulator::setParallelTxCallback(ParallelCard::ParallelTxCallback cb) {
  parallelTxCallback_ = cb;
  if (parallelCard_) {
    parallelCard_->setParallelTxCallback(std::move(cb));
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
