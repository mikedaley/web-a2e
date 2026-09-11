/*
 * iigs_machine.cpp - An Apple IIgs, assembled from its parts
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_machine.hpp"

#include "../cpu/65816/cpu65816.hpp"
#include "../mmu/mmu.hpp"
#include "../video/video.hpp"

namespace a2e::iigs {

IIgsMachine::IIgsMachine(size_t fastRamSize)
    : memory_(std::make_unique<IIgsMemory>(fastRamSize)) {
  cpu_ = std::make_unique<CPU65816>(
      [this](uint32_t address) { return memory_->read(address); },
      [this](uint32_t address, uint8_t value) { memory_->write(address, value); });

  // The picture is the Mega II's, and the Mega II is a //e: the same class,
  // reading the same memory through the same MMU.
  video_ = std::make_unique<Video>(memory_->megaII());
  video_->setCycleCallback([this]() { return slowCycles_; });
  memory_->megaII().setCycleCallback([this]() { return slowCycles_; });
  memory_->megaII().setVideoSwitchCallback(
      [this]() { video_->onVideoSwitchChanged(); });
}

IIgsMachine::~IIgsMachine() = default;

void IIgsMachine::init(const uint8_t *rom, size_t romSize) {
  memory_->loadROM(rom, romSize);
  reset();
}

void IIgsMachine::reset() {
  memory_->reset();
  slowCycles_ = 0;
  slowCycleRemainder_ = 0.0;

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

  slowCycleRemainder_ += slowCyclesFor(cycles);
  const uint64_t whole = static_cast<uint64_t>(slowCycleRemainder_);
  slowCycleRemainder_ -= static_cast<double>(whole);
  slowCycles_ += whole;

  video_->renderUpToCycle(slowCycles_);
  return cycles;
}

void IIgsMachine::runCycles(int slowCyclesToRun) {
  const uint64_t target = slowCycles_ + static_cast<uint64_t>(slowCyclesToRun);
  while (slowCycles_ < target) {
    if (cpu_->isStopped()) {
      // STP: the clock is stopped until a reset, and there is nothing to run.
      slowCycles_ = target;
      break;
    }
    step();
  }
}

std::string IIgsMachine::screenText() const {
  // The 40-column text screen, read out of the Mega II's main RAM: the same
  // interleaved layout every Apple II has had, because it is the same chip
  // generating it.
  std::string text;
  for (int row = 0; row < 24; row++) {
    const uint16_t base = static_cast<uint16_t>(0x400 + (row % 8) * 0x80 +
                                                (row / 8) * 0x28);
    for (int column = 0; column < 40; column++) {
      uint8_t byte = memory_->megaII().readRAM(
          static_cast<uint16_t>(base + column), false);
      byte &= 0x7F;
      text += (byte < 0x20) ? ' ' : static_cast<char>(byte);
    }
    text += '\n';
  }
  return text;
}

} // namespace a2e::iigs
