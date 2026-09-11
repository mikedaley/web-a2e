/*
 * iigs_machine.cpp - An Apple IIgs, assembled from its parts
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_machine.hpp"

#include "../cpu/65816/cpu65816.hpp"
#include "../mmu/mmu.hpp"
#include "../cards/disk_controller.hpp"
#include "../cards/iwm/iwm.hpp"
#include "../input/keyboard.hpp"
#include "../machine/machine_profile.hpp"
#include "../video/video.hpp"
#include "iigs_video.hpp"

#include <algorithm>
#include <cstring>

namespace a2e::iigs {

IIgsMachine::IIgsMachine(size_t fastRamSize)
    : memory_(std::make_unique<IIgsMemory>(fastRamSize)) {
  cpu_ = std::make_unique<CPU65816>(
      [this](uint32_t address) { return memory_->read(address); },
      [this](uint32_t address, uint8_t value) { memory_->write(address, value); });

  // The picture is the Mega II's, and the Mega II is a //e: the same class,
  // reading the same memory through the same MMU.
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
  lastFrameCycle_ = 0;
  frameReady_ = false;
  samplesGenerated_ = 0;

  // The reset vector is read from bank zero, where $D000 upward is the
  // language card — and a machine that has just been powered on is reading ROM
  // there, so $00:FFFC really is the ROM's own vector. That is how a IIgs
  // starts, and it is why the memory map has to be right before the CPU is
  // allowed to fetch anything.
  cpu_->reset();
}

double IIgsMachine::slowCyclesFor(int cpuCycles) const {
  if (!memory_->isFastSpeed()) return cpuCycles;
  return cpuCycles * (SLOW_CLOCK_HZ / FAST_CLOCK_HZ);
}

int IIgsMachine::step() {
  cpu_->executeInstruction();
  const int cycles = cpu_->getCycleCount();

  // The slow-side accesses have already charged themselves, as they happened;
  // what is left is the rest of the instruction, at whatever speed the machine
  // is running.
  memory_->addFastCycles(slowCyclesFor(cycles));

  // Draw the scanlines this instruction's time covered, and close the frame
  // when its time is up. Both halves matter: without the boundary the picture
  // is never finished and the screen stays black however much is drawn into
  // it. The boundary advances by exactly one frame rather than to the current
  // cycle, so it cannot drift away from where $C019 thinks vertical blanking
  // is — a program timing itself against the beam would see it wander.
  if (disk_) disk_->update(cycles);
  video_->renderUpToCycle(memory_->slowCycles());

  const auto &timing = machineProfile(MachineId::AppleIIgs).timing;
  if (memory_->slowCycles() - lastFrameCycle_ >=
      static_cast<uint64_t>(timing.cyclesPerFrame())) {
    lastFrameCycle_ += timing.cyclesPerFrame();
    video_->renderFrame();
    video_->beginNewFrame(lastFrameCycle_);
    frameReady_ = true;
  }
  return cycles;
}

void IIgsMachine::runCycles(int slowCyclesToRun) {
  const uint64_t target =
      memory_->slowCycles() + static_cast<uint64_t>(slowCyclesToRun);
  while (memory_->slowCycles() < target) {
    if (cpu_->isStopped()) {
      // STP: the clock is stopped until a reset, and there is nothing to run.
      memory_->addFastCycles(
          static_cast<double>(target - memory_->slowCycles()));
      break;
    }
    step();
  }
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

  // ...and then ask the Ensoniq what it is playing.
  if (buffer) {
    memory_->sound().generateSamples(buffer, sampleCount, AUDIO_SAMPLE_RATE);
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
