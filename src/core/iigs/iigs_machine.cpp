/*
 * iigs_machine.cpp - An Apple IIgs, assembled from its parts
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_machine.hpp"

#include "../cpu/65816/cpu65816.hpp"
#include "../disassembler/disassembler65816.hpp"
#include "../audio/audio.hpp"
#include "../mmu/mmu.hpp"
#include "../cards/disk_controller.hpp"
#include "../cards/iwm/iwm.hpp"
#include "../cards/smartport/smartport_card.hpp"
#include "../input/keyboard.hpp"
#include "../machine/machine_profile.hpp"
#include "../video/video.hpp"
#include "iigs_video.hpp"

#include <algorithm>
#include <cstring>

namespace a2e::iigs {

IIgsMachine::IIgsMachine(size_t fastRamSize)
    : memory_(std::make_unique<IIgsMemory>(fastRamSize)) {
  // Watchpoints are checked here rather than inside the memory, and that is
  // the difference between "the program touched this" and "something did": the
  // video scanner reads the text page every line, and a watchpoint on it that
  // fired for the scanner would never let the machine run.
  cpu_ = std::make_unique<CPU65816>(
      [this](uint32_t address) {
        const uint8_t value = memory_->read(address);
        if (debug_.hasWatchpoints() && debug_.onRead(address, value)) {
          paused_ = true;
        }
        return value;
      },
      [this](uint32_t address, uint8_t value) {
        memory_->write(address, value);
        if (debug_.hasWatchpoints() && debug_.onWrite(address, value)) {
          paused_ = true;
        }
      });

  // The picture is the Mega II's, and the Mega II is a //e: the same class,
  // reading the same memory through the same MMU.
  audio_ = std::make_unique<Audio>(machineProfile(MachineId::AppleIIgs));
  video_ = std::make_unique<Video>(memory_->megaII());
  screen_ = std::make_unique<IIgsVideo>(*video_, *memory_);

  // The 5.25" port. A IIgs has an IWM where a //e has a slot, and the firmware
  // that drives it is in the system ROM — so the chip goes into the Mega II's
  // slot 6, where its sixteen addresses are, and nothing needs a card ROM.
  {
    auto iwm = std::make_unique<IWM>();
      iwm->setCycleCallback([this]() { return memory_->slowCycles(); });
    disk_ = iwm.get();
    memory_->megaII().insertCard(6, std::move(iwm));
  }

  // $C036's motor detect bits: the one drive this machine has is the IWM's, in
  // slot 6, and a IIgs with it turning is a 1.023MHz machine whatever the
  // speed bit says. See IIgsMemory::speedRegister for why that is the
  // difference between booting a disk and failing every checksum on it.
  // $C022: the VGC's two text colours. The //e's video decodes a text line
  // into them rather than through a receiver, which is what a IIgs's picture
  // does and why its text can be white on blue and still be sharp.
  memory_->setTextColourCallback([this](uint8_t value) {
    video_->setTextColours(
        IIgsVideo::vgcColourARGB(static_cast<uint8_t>((value >> 4) & 0x0F)),
        IIgsVideo::vgcColourARGB(static_cast<uint8_t>(value & 0x0F)));
  });

  // The interrupt line is a level the processor samples every instruction,
  // and it is the OR of everything in the machine that can hold it down.
  cpu_->setIRQStatusCallback([this]() { return memory_->interruptPending(); });

  // Vector pulls come from ROM, whatever bank zero's language card is showing.
  // That is the FPI answering the processor's VPB line, and it is the reason
  // GS/OS can copy its kernel over $D000-$FFFF with interrupts enabled: the
  // vector it takes mid-copy is the firmware's, not whatever half-written
  // byte happens to be at $FFEE.
  cpu_->setVectorReadCallback([this](uint16_t vector) {
    const uint32_t at = (static_cast<uint32_t>(ROM_TOP_BANK) << 16) | vector;
    return static_cast<uint16_t>(memory_->read(at) | (memory_->read(at + 1) << 8));
  });

  memory_->setVolumeCallback([this](uint8_t nibble, uint64_t cycle) {
    if (volumeChanges_.size() < 4096) volumeChanges_.push_back({cycle, nibble});
  });

  memory_->setBeamQuery([this]() {
    const auto &timing = machineProfile(MachineId::AppleIIgs).timing;
    const uint64_t elapsed = memory_->slowCycles() - lastFrameCycle_;
    const int line = static_cast<int>((elapsed / timing.cyclesPerScanline) % timing.scanlinesPerFrame);
    const int column = static_cast<int>(elapsed % timing.cyclesPerScanline);
    return IIgsMemory::Beam{line, column};
  });

  memory_->setSlotMotorQuery([this](int slot) {
    return slot == 6 && disk_ && disk_->isMotorOn();
  });

  // Slot 5 is the SmartPort. On a real IIgs that means the machine's own
  // firmware driving a Sony 3.5" drive and whatever else is daisy-chained off
  // the port behind it; here it means the block devices the host hands over.
  // Either way it is part of the machine rather than a card somebody fitted,
  // so it is on the internal side of $C02D and needs no Control Panel setting
  // to answer. With no image loaded it has no ROM, and the machine's own
  // slot 5 firmware shows through unchanged.
  {
    auto smartPort = std::make_unique<SmartPortCard>();
    smartPort->setSlotNumber(SMARTPORT_SLOT);
    // The machine's own slot 5 firmware has its ProDOS entry at $C50A and its
    // SmartPort entry at $C50D, and software written for a IIgs hard-codes
    // those rather than reading $C5FF — a boot loader that did `JSR $C50D`
    // into a card laid out like a card found an RTS there, came back without
    // its inline parameters skipped, and executed them.
    smartPort->setProDOSEntry(0x0A);
    smartPort->setMemReadCallback(
        [this](uint16_t address) { return memory_->read(address); });
    smartPort->setMemWriteCallback([this](uint16_t address, uint8_t value) {
      memory_->write(address, value);
    });

    // The card drives the machine's registers directly — that is how a trap
    // card works: it does the read, puts the result where the caller expects
    // it, and adjusts the stack so the RTS it returns through lands on the
    // code it just loaded. So it needs the CPU, and the CPU it is being handed
    // is sixteen bits wide where it expects eight.
    //
    // The translations are the interesting part. The accumulator's high half
    // is B and must survive, so only the low byte is replaced. The stack
    // pointer it hands back is a page-one offset, because that is the only
    // kind a //e has and this card was written for a //e; in emulation mode,
    // which is where any of this runs, that is exactly what the 65816's is.
    smartPort->setGetA(
        [this]() { return static_cast<uint8_t>(cpu_->getA()); });
    smartPort->setSetA([this](uint8_t value) {
      cpu_->setA(static_cast<uint16_t>((cpu_->getA() & 0xFF00) | value));
    });
    smartPort->setGetP([this]() { return cpu_->getP(); });
    smartPort->setSetP([this](uint8_t value) { cpu_->setP(value); });
    smartPort->setGetSP([this]() { return cpu_->getSP(); });
    smartPort->setSetSP([this](uint16_t value) { cpu_->setSP(value); });
    // A 65816 reads the opcode and *then* advances, where a 6502 has already
    // advanced by the time the read arrives. The card must not guess at that.
    smartPort->setExecutingAt([this](uint16_t address) {
      return cpu_->getPBR() == 0x00 && cpu_->getPC() == address;
    });
    // Extended calls carry a bank, and this machine has banks to carry.
    smartPort->setMemRead24Callback(
        [this](uint32_t address) { return memory_->read(address); });
    smartPort->setMemWrite24Callback([this](uint32_t address, uint8_t value) {
      memory_->write(address, value);
    });
    smartPort->setGetPC([this]() { return cpu_->getPC(); });
    smartPort->setSetPC([this](uint16_t value) { cpu_->setPC(value); });
    smartPort->setSetX([this](uint8_t value) {
      cpu_->setX(static_cast<uint16_t>((cpu_->getX() & 0xFF00) | value));
    });
    smartPort_ = smartPort.get();
    memory_->megaII().insertCard(SMARTPORT_SLOT, std::move(smartPort));
    memory_->setInternalCardSlot(SMARTPORT_SLOT);
  }

  // The keyboard translation is the //e's — a browser key event becomes an
  // Apple II code the same way whichever machine is listening — and what it
  // feeds is the ADB controller, which is what a IIgs has instead of wires.
  keyboard_ = std::make_unique<Keyboard>();
  keyboard_->setKeyCallback([this](int key) { keyDown(key); });

  // And the Mega II reads the keyboard through the controller, which is how
  // //e software works on this machine without knowing the controller exists.
  memory_->megaII().setKeyboardCallback(
      [this]() { return memory_->adb().keyboardLatch(); });
  memory_->megaII().setKeyStrobeCallback(
      [this]() { memory_->adb().clearKeyboardStrobe(); });
  memory_->megaII().setAnyKeyDownCallback(
      [this]() { return memory_->adb().isAnyKeyDown(); });
  memory_->megaII().setButtonCallback([this](int button) -> uint8_t {
    // The Apple keys, which are buttons rather than keys on every Apple II.
    if (button == 0) return keyboard_->isOpenApplePressed() ? 0x80 : 0x00;
    if (button == 1) return keyboard_->isClosedApplePressed() ? 0x80 : 0x00;
    return 0x00;
  });
  video_->setCycleCallback([this]() { return memory_->slowCycles(); });

  // $C030 is the Mega II's, and so is the speaker behind it: the toggle is
  // timed on the slow clock, which is the one the profile's cpuClockHz names
  // and the one Audio turns into samples.
  memory_->megaII().setSpeakerCallback(
      [this]() { audio_->toggleSpeaker(memory_->slowCycles()); });
  memory_->megaII().setCycleCallback([this]() { return memory_->slowCycles(); });
  memory_->megaII().setVideoSwitchCallback(
      [this]() { video_->onVideoSwitchChanged(); });
}

IIgsMachine::~IIgsMachine() = default;

void IIgsMachine::init(const uint8_t *rom, size_t romSize,
                       const uint8_t *characterRom, size_t characterSize) {
  memory_->loadROM(rom, romSize);
  // The Mega II draws text from a character generator, and this machine's is
  // not in its ROM. See the note on init() in the header.
  memory_->megaII().loadROM(nullptr, 0, characterRom, characterSize);
  reset();
}

void IIgsMachine::reset() {
  memory_->reset();
  audio_->reset();
  lastFrameCycle_ = 0;
  linesFinished_ = 0;
  soundCycle_ = 0;
  volumeChanges_.clear();
  speakerGain_ = 0.0f;
  speakerNibble_ = 0;
  frameReady_ = false;
  samplesGenerated_ = 0;

  // The reset vector is read from bank zero, where $D000 upward is the
  // language card — and a machine that has just been powered on is reading ROM
  // there, so $00:FFFC really is the ROM's own vector. That is how a IIgs
  // starts, and it is why the memory map has to be right before the CPU is
  // allowed to fetch anything.
  cpu_->reset();
}

// One refresh cycle, ten fast cycles long in all, for every fifty 14M ticks
// of fast RAM access: GSSquared's rule, and about 2.5MHz effective.
static constexpr double REFRESH_STRETCH = 55.0 / 50.0;

double IIgsMachine::slowCyclesFor(int cpuCycles) const {
  if (!memory_->isFastSpeed()) return cpuCycles;
  // Fast RAM is refreshed as it runs: the FPI takes one refresh cycle for
  // every ten fast cycles, which is why a 2.8MHz IIgs measures nearer 2.5.
  // ROM needs no refresh, so code running from it goes at the full rate. The
  // diagnostic disk counts loop iterations between two changes of the
  // vertical counter and accepts exactly the numbers this produces.
  const bool fromRom = cpu_->getPBR() >= 0xF0;
  const double stretch = fromRom ? 1.0 : REFRESH_STRETCH;
  return cpuCycles * (SLOW_CLOCK_HZ / FAST_CLOCK_HZ) * stretch;
}

int IIgsMachine::step() {
  cpu_->executeInstruction();
  const int cycles = cpu_->getCycleCount();

  // The slow-side accesses have already charged themselves, as they happened;
  // what is left is the rest of the instruction, at whatever speed the machine
  // is running. Subtracting them is the whole point: a cycle spent waiting on
  // the Mega II is not also a cycle spent running.
  const uint64_t slowAccesses = memory_->takeSlowAccesses();
  const int fastCycles =
      cycles > static_cast<int>(slowAccesses)
          ? cycles - static_cast<int>(slowAccesses)
          : 0;
  memory_->addFastCycles(slowCyclesFor(fastCycles));

  // Draw the scanlines this instruction's time covered, and close the frame
  // when its time is up. Both halves matter: without the boundary the picture
  // is never finished and the screen stays black however much is drawn into
  // it. The boundary advances by exactly one frame rather than to the current
  // cycle, so it cannot drift away from where $C019 thinks vertical blanking
  // is — a program timing itself against the beam would see it wander.
  if (disk_) disk_->update(cycles);
  memory_->tickClocks();
  // The Ensoniq runs on the machine's clock, so its oscillators reach the ends
  // of their tables — and interrupt — when the machine says, not when the host
  // next asks for a buffer.
  memory_->sound().advance(
      static_cast<uint32_t>(memory_->slowCycles() - soundCycle_));
  memory_->scc().advance(
      static_cast<uint32_t>(memory_->slowCycles() - soundCycle_));
  soundCycle_ = memory_->slowCycles();
  video_->renderUpToCycle(memory_->slowCycles());
  raiseScanLineInterrupts();

  const auto &timing = machineProfile(MachineId::AppleIIgs).timing;
  if (memory_->slowCycles() - lastFrameCycle_ >=
      static_cast<uint64_t>(timing.cyclesPerFrame())) {
    lastFrameCycle_ += timing.cyclesPerFrame();
    linesFinished_ = 0;
    video_->renderFrame();
    video_->beginNewFrame(lastFrameCycle_);
    memory_->signalVerticalBlank();
    frameReady_ = true;
  }
  return cycles;
}

void IIgsMachine::raiseScanLineInterrupts() {
  // Each Super Hi-Res line has a control byte in bank $E1 at $9D00, and bit 6
  // of it asks the VGC to interrupt once that line has been drawn. QuickDraw II
  // runs the mouse pointer on this: it sets the bit on the line the pointer
  // sits on and redraws it in the handler, after the beam has passed, so the
  // pointer never tears. Without it the pointer was only redrawn from the
  // one-second tick, which shares the VGC's vector.
  //
  // The control bytes mean nothing unless Super Hi-Res is on — the VGC does
  // not read them in the //e's modes — and a line is checked once, as the
  // beam finishes it. The picture's 200 lines are counted from the top of the
  // frame; exactly where line zero falls against the Mega II's 262 is not
  // something a program can measure from here, and the pointer cares only
  // that its line is done.
  if (!memory_->superHiResEnabled()) {
    linesFinished_ = SHR_LINES;
    return;
  }
  const auto &timing = machineProfile(MachineId::AppleIIgs).timing;
  const uint64_t elapsed = memory_->slowCycles() - lastFrameCycle_;
  const int finished = static_cast<int>(
      std::min<uint64_t>(elapsed / timing.cyclesPerScanline, SHR_LINES));
  for (; linesFinished_ < finished; linesFinished_++) {
    const uint8_t control = memory_->peek(
        0xE10000u + SHR_SCB_BASE + static_cast<uint32_t>(linesFinished_));
    if (control & IIgsVideo::SCB_INTERRUPT) memory_->signalScanLine();
  }
}

void IIgsMachine::runCycles(int slowCyclesToRun) {
  if (paused_) return;

  const uint64_t target =
      memory_->slowCycles() + static_cast<uint64_t>(slowCyclesToRun);
  while (memory_->slowCycles() < target) {
    if (cpu_->isStopped()) {
      // STP: the clock is stopped until a reset, and there is nothing to run.
      memory_->addFastCycles(
          static_cast<double>(target - memory_->slowCycles()));
      break;
    }

    // A breakpoint is an address the program counter reaches, and on this
    // machine that address has a bank in it.
    if (debug_.shouldBreakBefore(cpu_->getPCFull())) {
      paused_ = true;
      return;
    }
    if (debug_.isTraceEnabled()) recordTrace();

    step();

    // A watchpoint fired inside the instruction, through the CPU's own bus.
    if (debug_.isWatchpointHit()) {
      paused_ = true;
      return;
    }

    if (debug_.hasBeamBreakpoints()) {
      uint64_t frameCycle = memory_->slowCycles() - lastFrameCycle_;
      const auto &timing = machineProfile(MachineId::AppleIIgs).timing;
      const auto perFrame = static_cast<uint64_t>(timing.cyclesPerFrame());
      if (frameCycle >= perFrame) frameCycle %= perFrame;
      const int scanline =
          static_cast<int>(frameCycle / timing.cyclesPerScanline);
      const int hPos = static_cast<int>(frameCycle % timing.cyclesPerScanline);
      if (debug_.shouldBreakAtBeam(lastFrameCycle_, scanline, hPos)) {
        paused_ = true;
        return;
      }
    }
  }
}

// ============================================================================
// Debugging
//
// The same facilities a //e has, through the same object; what differs is what
// the processor has to say about itself.
// ============================================================================

void IIgsMachine::setPaused(bool paused) {
  if (!paused && paused_ && debug_.isBreakpointHit()) {
    // Resuming from the breakpoint the machine is sitting on: let this one
    // instruction through, or continuing would stop again having run nothing.
    debug_.skipNextBreakpoint();
  }
  debug_.clearHits();
  if (!paused && paused_) samplesGenerated_ = 0;
  paused_ = paused;
}

void IIgsMachine::stepInstruction() {
  debug_.clearHits();
  if (debug_.isTraceEnabled()) recordTrace();
  step();
}

uint32_t IIgsMachine::stepOver() {
  debug_.clearTempBreakpoint();
  const uint32_t pc = cpu_->getPCFull();
  const uint8_t opcode = memory_->peek(pc);

  // A call, of which this processor has three, and they are not all the same
  // length: JSL is four bytes where the two JSRs are three.
  if (flowType816(opcode) == FlowType::CALL || opcode == 0x00 /* BRK */) {
    const int length =
        instructionLength816(opcode, cpu_->accumulator8(), cpu_->index8());
    // The return lands in the program bank, which a call does not leave until
    // it arrives — so the address after a JSL is still in this bank.
    const uint32_t after =
        (pc & 0xFF0000) | static_cast<uint16_t>((pc & 0xFFFF) + length);
    debug_.setTempBreakpoint(after);
    setPaused(false);
    return after;
  }

  stepInstruction();
  return 0;
}

uint32_t IIgsMachine::stepOut() {
  debug_.clearTempBreakpoint();

  // The stack pointer is sixteen bits and the stack is anywhere in bank zero,
  // so the return address is read from where it actually points rather than
  // from page one.
  const uint16_t sp = cpu_->getSP();
  auto stack = [this](uint16_t at) { return memory_->peek(at); };

  // Which kind of return is on the stack is not knowable from the stack, so
  // the instruction the machine is sitting on decides: RTL took three bytes,
  // and anything else is assumed to be the two a JSR pushed, because a long
  // call is much the rarer of the two.
  const bool longReturn = memory_->peek(cpu_->getPCFull()) == 0x6B;
  const uint16_t offset =
      static_cast<uint16_t>((stack(sp + 1) | (stack(sp + 2) << 8)) + 1);
  const uint8_t bank = longReturn ? stack(sp + 3)
                                  : static_cast<uint8_t>(cpu_->getPBR());
  const uint32_t returnAddress = (static_cast<uint32_t>(bank) << 16) | offset;

  if (offset == 0) {
    // Nothing plausible on the stack; a step is the best that can be done.
    stepInstruction();
    return 0;
  }
  debug_.setTempBreakpoint(returnAddress);
  setPaused(false);
  return returnAddress;
}

BeamPosition IIgsMachine::beam() const {
  return beamPosition(memory_->slowCycles(),
                      machineProfile(MachineId::AppleIIgs).timing);
}

void IIgsMachine::recordTrace() {
  MachineDebug::TraceEntry *entry = debug_.beginTraceEntry();
  if (!entry) return;

  entry->pc = cpu_->getPCFull();
  entry->a = cpu_->getA();
  entry->x = cpu_->getX();
  entry->y = cpu_->getY();
  entry->sp = cpu_->getSP();
  entry->d = cpu_->getD();
  entry->p = cpu_->getP();
  entry->dbr = cpu_->getDBR();
  entry->opcode = memory_->peek(entry->pc);
  entry->widths =
      static_cast<uint8_t>((cpu_->getEmulation() ? MachineDebug::WIDTH_EMULATION : 0) |
                           (cpu_->accumulator8() ? MachineDebug::WIDTH_A8 : 0) |
                           (cpu_->index8() ? MachineDebug::WIDTH_INDEX8 : 0));
  entry->instrLen = static_cast<uint8_t>(
      instructionLength816(entry->opcode, cpu_->accumulator8(), cpu_->index8()));
  // The operands are read inside the program bank, which is where the
  // processor is about to read them from.
  const uint32_t bank = entry->pc & 0xFF0000;
  for (int i = 1; i < entry->instrLen && i <= 3; i++) {
    const uint32_t at = bank | static_cast<uint16_t>((entry->pc & 0xFFFF) + i);
    (&entry->operand1)[i - 1] = memory_->peek(at);
  }
  entry->cycle = static_cast<uint32_t>(memory_->slowCycles());
}

bool IIgsMachine::insertBlockImage(int device, const uint8_t *data, size_t size,
                                   const std::string &filename) {
  return smartPort_ && smartPort_->insertImage(device, data, size, filename);
}

void IIgsMachine::ejectBlockImage(int device) {
  if (smartPort_) smartPort_->ejectImage(device);
}

const uint8_t *IIgsMachine::exportDiskDataAs(int drive, DiskSaveFormat format,
                                            size_t *size) {
  if (size) *size = 0;
  if (!disk_) return nullptr;

  DiskImage *image = disk_->getMutableDiskImage(drive);
  if (!image || !image->isLoaded()) return nullptr;
  if (!DiskConverter::convert(*image, format, diskExportBuffer_)) return nullptr;

  if (size) *size = diskExportBuffer_.size();
  return diskExportBuffer_.data();
}

const uint8_t *IIgsMachine::getDiskSectorsDOSOrder(int drive, size_t *size) {
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

bool IIgsMachine::canExportDiskAs(int drive, DiskSaveFormat format) {
  if (!disk_) return false;
  DiskImage *image = disk_->getMutableDiskImage(drive);
  return image && DiskConverter::canConvert(*image, format);
}

DiskSaveFormat IIgsMachine::getDiskNativeFormat(int drive) {
  if (!disk_) return DiskSaveFormat::DOSOrder;
  const DiskImage *image = disk_->getDiskImage(drive);
  if (!image) return DiskSaveFormat::DOSOrder;
  return DiskConverter::nativeFormat(*image);
}

const char *IIgsMachine::getDiskFilename(int drive) const {
  if (!disk_) return nullptr;
  const auto *image = disk_->getDiskImage(drive);
  return image ? image->getFilename().c_str() : nullptr;
}

bool IIgsMachine::insertDisk(int drive, const uint8_t *data, size_t size,
                             const std::string &filename) {
  return disk_ && disk_->insertDisk(drive, data, size, filename);
}

void IIgsMachine::ejectDisk(int drive) {
  if (disk_) disk_->ejectDisk(drive);
}

bool IIgsMachine::hasDisk(int drive) const {
  return disk_ && disk_->hasDisk(drive);
}

int IIgsMachine::handleRawKeyDown(int browserKeycode, bool shift, bool ctrl,
                                  bool alt, bool meta, bool capsLock,
                                  int keyLocation) {
  const int key = keyboard_->handleKeyDown(browserKeycode, shift, ctrl, alt,
                                           meta, capsLock, keyLocation);
  memory_->adb().setAnyKeyDown(keyboard_->isAnyKeyDown());
  return key;
}

void IIgsMachine::handleRawKeyUp(int browserKeycode, bool shift, bool ctrl,
                                 bool alt, bool meta, int keyLocation) {
  keyboard_->handleKeyUp(browserKeycode, shift, ctrl, alt, meta, keyLocation);
  memory_->adb().setAnyKeyDown(keyboard_->isAnyKeyDown());
}

void IIgsMachine::keyDown(int keycode) {
  memory_->adb().queueKeyboard(static_cast<uint8_t>(keycode & 0x7F));
  memory_->adb().setAnyKeyDown(true);
}

void IIgsMachine::mouseMove(int dx, int dy) {
  memory_->adb().queueMouse(dx, dy);
}

void IIgsMachine::mouseButton(bool pressed) {
  memory_->adb().setMouseButton(pressed);
}

std::string IIgsMachine::screenText() const { return screenText(0, 0, 23, 39); }

std::string IIgsMachine::screenText(int startRow, int startColumn, int endRow,
                                    int endColumn) const {
  std::string text;
  startRow = std::max(0, startRow);
  startColumn = std::max(0, startColumn);
  endRow = std::min(23, endRow);
  endColumn = std::min(39, endColumn);

  for (int row = startRow; row <= endRow; row++) {
    const uint16_t base = static_cast<uint16_t>(0x400 + (row % 8) * 0x80 +
                                                (row / 8) * 0x28);
    for (int column = startColumn; column <= endColumn; column++) {
      uint8_t byte = memory_->megaII().readRAM(
          static_cast<uint16_t>(base + column), false);
      byte &= 0x7F;
      text += (byte < 0x20) ? ' ' : static_cast<char>(byte);
    }
    if (row < endRow) text += '\n';
  }
  return text;
}

// ============================================================================
// What the host drives it through
// ============================================================================

int IIgsMachine::generateStereoAudioSamples(float *buffer, int sampleCount) {
  // Producing the samples is what runs the machine: the worker asks for a
  // buffer, and the time that buffer represents is the time the machine gets.
  const auto &profile = machineProfile(MachineId::AppleIIgs);
  const int cyclesToRun = static_cast<int>(
      sampleCount * profile.timing.cyclesPerSample(AUDIO_SAMPLE_RATE));
  runCycles(cyclesToRun);

  samplesGenerated_ += sampleCount;

  // ...and then ask both of the machine's sound sources what they are playing.
  // The speaker writes the buffer and the Ensoniq is added on top, because a
  // IIgs has one amplifier and everything reaches the same one.
  if (buffer) {
    const uint64_t endCycle = memory_->slowCycles();
    audio_->generateStereoSamples(buffer, sampleCount, endCycle);

    // The volume nibble in $C03C is the amplifier's, and the speaker is on
    // the same amplifier as the synthesiser: the ROM's bell fades out by
    // turning it down, and a bell that did not was a flat buzz. The gain
    // follows the nibble's changes at the times they happened, through the
    // same twenty-millisecond slew the synthesiser's path has.
    const double cyclesPerSample = static_cast<double>(cyclesToRun) / sampleCount;
    const uint64_t startCycle = endCycle - static_cast<uint64_t>(cyclesToRun);
    constexpr float SLEW = 1.0f / (0.020f * AUDIO_SAMPLE_RATE);
    size_t change = 0;
    for (int i = 0; i < sampleCount; i++) {
      const uint64_t at = startCycle + static_cast<uint64_t>(i * cyclesPerSample);
      while (change < volumeChanges_.size() && volumeChanges_[change].cycle <= at) {
        speakerNibble_ = volumeChanges_[change++].nibble;
      }
      speakerGain_ += SLEW * (static_cast<float>(amplifierGain(speakerNibble_)) -
                              speakerGain_);
      buffer[i * 2] *= speakerGain_;
      buffer[i * 2 + 1] *= speakerGain_;
    }
    // Whatever is left is later than this buffer; keep it for the next.
    volumeChanges_.erase(volumeChanges_.begin(), volumeChanges_.begin() + change);

    ensoniqMix_.resize(static_cast<size_t>(sampleCount) * 2);
    memory_->sound().generateSamples(ensoniqMix_.data(), sampleCount,
                                     AUDIO_SAMPLE_RATE);
    for (size_t at = 0; at < ensoniqMix_.size(); at++) {
      buffer[at] += ensoniqMix_[at];
    }
  }
  return sampleCount;
}

int IIgsMachine::consumeFrameSamples() {
  // A frame's worth of samples at the rate the host mixes at: the same
  // arithmetic Emulator does, and the same answer, because both machines put
  // sixty frames a second on the same screen.
  constexpr int SAMPLES_PER_FRAME = AUDIO_SAMPLE_RATE / 60;
  const int frames = samplesGenerated_ / SAMPLES_PER_FRAME;
  samplesGenerated_ %= SAMPLES_PER_FRAME;
  return frames;
}

bool IIgsMachine::isFrameReady() const { return frameReady_; }

void IIgsMachine::clearFrameReady() { frameReady_ = false; }

size_t IIgsMachine::framebufferSize() const {
  return screen_->framebufferSize();
}

const uint8_t *IIgsMachine::framebuffer() { return screen_->render(); }

} // namespace a2e::iigs
