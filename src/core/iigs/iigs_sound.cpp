/*
 * iigs_sound.cpp - The Ensoniq 5503, as the 65816 sees it and as the speaker
 * hears it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_sound.hpp"

#include <algorithm>
#include <cmath>

namespace a2e::iigs {

void IIgsSound::reset() {
  voices_.fill(Voice{});
  oscillatorsEnabled_ = 1;
  enableRegister_ = 0;
  interruptRegister_ = 0xFF;
  ram_.fill(0);
  address_ = 0;
  control_ = 0;
  latch_ = 0;
  ticks_ = 0.0;
  ring_.assign(RING_FRAMES * 2, 0.0f);
  produced_ = 0;
  consumed_ = 0.0;
  lastLeft_ = lastRight_ = 0.0f;
}

// ============================================================================
// The window
// ============================================================================

void IIgsSound::writeControl(uint8_t value) {
  // Bit 7 is the chip's to set, and it never is here: a transfer finishes
  // before the processor can ask about it.
  control_ = static_cast<uint8_t>(value & 0x7F);
  // The chip's registers are a page; with the window on them the pointer's
  // high byte means nothing and reads back as nothing.
  if (!addressesRam()) address_ &= 0x00FF;
}

void IIgsSound::advancePointer() {
  if (autoIncrements()) address_ = static_cast<uint16_t>(address_ + 1);
}

uint8_t IIgsSound::readData() {
  // One behind: the chip hands back what it fetched last time and goes to
  // fetch the byte at the current address, which the *next* read returns. A
  // program that sets the address and reads once gets whatever was there
  // before, which is why firmware always reads twice. The fetch is at the
  // address as set, before the pointer moves on.
  const uint8_t value = latch_;
  latch_ = addressesRam() ? ram_[address_]
                          : readDocRegister(static_cast<uint8_t>(address_));
  advancePointer();
  return value;
}

void IIgsSound::writeData(uint8_t value) {
  // Writes are not delayed — only reads are.
  latch_ = value;
  if (addressesRam()) {
    ram_[address_] = value;
  } else {
    writeDocRegister(static_cast<uint8_t>(address_), value);
  }
  advancePointer();
}

void IIgsSound::writeAddressLow(uint8_t value) {
  address_ = static_cast<uint16_t>((address_ & 0xFF00) | value);
}

void IIgsSound::writeAddressHigh(uint8_t value) {
  address_ = static_cast<uint16_t>((address_ & 0x00FF) | (value << 8));
  if (!addressesRam()) address_ &= 0x00FF;
}

// ============================================================================
// The chip's registers
// ============================================================================

uint8_t IIgsSound::docRegister(uint8_t index) const {
  if (index < DOC_INTERRUPT) {
    const Voice &v = voices_[index & 0x1F];
    switch (index & 0xE0) {
    case DOC_FREQUENCY_LOW: return static_cast<uint8_t>(v.frequency);
    case DOC_FREQUENCY_HIGH: return static_cast<uint8_t>(v.frequency >> 8);
    case DOC_VOLUME: return v.volume;
    case DOC_DATA: return v.data;
    case DOC_WAVE_POINTER: return static_cast<uint8_t>(v.wavePointer >> 8);
    case DOC_CONTROL: return v.control;
    case DOC_WAVE_SIZE:
      return static_cast<uint8_t>(((v.wavePointer & 0x10000) ? SIZE_BANK : 0) |
                                  (v.sizeCode << 3) | v.resolution);
    }
    return 0;
  }
  switch (index) {
  case DOC_INTERRUPT: return static_cast<uint8_t>(interruptRegister_ | 0x41);
  case DOC_OSCILLATOR_ENABLE: return enableRegister_;
  case DOC_ADC: return 0; // No microphone on the other end
  }
  return 0;
}

uint8_t IIgsSound::readDocRegister(uint8_t reg) {
  if (reg != DOC_INTERRUPT) return docRegister(reg);

  // $E0 is active low: bit 7 clear means an oscillator has interrupted and
  // bits 5-1 say which. Reading it reports and clears the first one waiting;
  // if another is, the line stays down for it. Bits 0 and 6 always read set.
  // With nothing waiting it says so — the ROM's interrupt manager asks this
  // register on every interrupt it cannot otherwise place, and a zero here
  // is "Unclaimed Sound Interrupt" on a machine whose chip did nothing.
  uint8_t value = interruptRegister_;
  for (int i = 0; i < oscillatorsEnabled_; i++) {
    if (voices_[i].interruptPending) {
      value = static_cast<uint8_t>(i << 1);
      interruptRegister_ = static_cast<uint8_t>(value | 0x80);
      voices_[i].interruptPending = false;
      break;
    }
  }
  return static_cast<uint8_t>(value | 0x41);
}

void IIgsSound::writeDocRegister(uint8_t reg, uint8_t value) {
  if (reg < DOC_INTERRUPT) {
    Voice &v = voices_[reg & 0x1F];
    switch (reg & 0xE0) {
    case DOC_FREQUENCY_LOW:
      v.frequency = static_cast<uint16_t>((v.frequency & 0xFF00) | value);
      break;
    case DOC_FREQUENCY_HIGH:
      v.frequency = static_cast<uint16_t>((v.frequency & 0x00FF) | (value << 8));
      break;
    case DOC_VOLUME:
      v.volume = value;
      break;
    case DOC_DATA:
      break; // What the oscillator read; the processor cannot write it
    case DOC_WAVE_POINTER:
      v.wavePointer = (v.wavePointer & 0x10000) | (static_cast<uint32_t>(value) << 8);
      break;
    case DOC_CONTROL:
      // Key on: clearing the halt bit starts the sound from the top.
      if ((v.control & OSC_HALT) && !(value & OSC_HALT)) v.accumulator = 0;
      // Halting a running oscillator whose new mode is one-shot or swap goes
      // through the same motions as reaching the end — in swap mode that is
      // what starts the partner.
      if (!(v.control & OSC_HALT) && (value & OSC_HALT) && (value & 0x02)) {
        haltOscillator(reg & 0x1F, true, value);
      }
      v.control = value;
      break;
    case DOC_WAVE_SIZE:
      v.wavePointer = (value & SIZE_BANK) ? (v.wavePointer | 0x10000)
                                          : (v.wavePointer & 0xFFFF);
      v.sizeCode = static_cast<uint8_t>((value >> 3) & 0x07);
      v.resolution = static_cast<uint8_t>(value & 0x07);
      break;
    }
    return;
  }
  switch (reg) {
  case DOC_OSCILLATOR_ENABLE:
    // "The number of oscillators enabled, times two, minus two", which is the
    // chip's own encoding and what the firmware writes.
    enableRegister_ = value;
    oscillatorsEnabled_ = ((value >> 1) & 0x1F) + 1;
    break;
  case DOC_INTERRUPT:
  case DOC_ADC:
    break; // Read only
  }
}

// ============================================================================
// The synthesiser
// ============================================================================

double IIgsSound::sampleRate() const {
  // One oscillator per eight ticks, and two spare slots every scan.
  return (DOC_CLOCK_HZ / 8.0) / (oscillatorsEnabled_ + 2);
}

bool IIgsSound::interruptPending() const {
  for (int i = 0; i < oscillatorsEnabled_; i++) {
    if (voices_[i].interruptPending) return true;
  }
  return false;
}

void IIgsSound::haltOscillator(int index, bool fromEnd, uint8_t newControl) {
  Voice &v = voices_[index];
  Voice &partner = voices_[index ^ 1];
  int mode = (v.control & OSC_MODE_MASK) >> 1;
  const int partnerMode = (partner.control & OSC_MODE_MASK) >> 1;

  if (mode == (OSC_MODE_SYNC >> 1)) {
    // An even oscillator reaching its end restarts the odd one below it, if
    // that one is running; for its own part it loops like a free-running one.
    if ((index & 1) == 0 && index > 0 && !(voices_[index - 1].control & OSC_HALT)) {
      voices_[index - 1].accumulator = 0;
    }
    mode = OSC_MODE_FREE_RUN >> 1;
  }

  if (mode != (OSC_MODE_FREE_RUN >> 1) || !fromEnd) {
    // A zero byte stops every mode; the end of the table stops all but
    // free-run.
    v.control |= OSC_HALT;
  } else {
    // Free-running: back to the start of the table, keeping the fraction so
    // the pitch does not stumble at the join.
    v.accumulator -= (tableLength(v) - 1) << resolutionShift(v);
  }

  if (mode == (OSC_MODE_SWAP >> 1)) {
    // Hand over to the partner, from its top.
    partner.control &= static_cast<uint8_t>(~OSC_HALT);
    partner.accumulator = 0;
  } else if (partnerMode == (OSC_MODE_SWAP >> 1) && (index & 1) == 0) {
    // The even half of a swapped pair whose partner is the swapping one
    // retriggers itself.
    v.control &= static_cast<uint8_t>(~OSC_HALT);
    v.accumulator -= (tableLength(v) - 1) << resolutionShift(v);
  }

  if (v.control & OSC_INTERRUPT_ENABLE) {
    // A halt the processor wrote raises an interrupt only if the value it
    // wrote still asks for one: a player switching a voice off with the
    // interrupt bit clear does not want to hear about it.
    if (fromEnd && !(newControl & OSC_INTERRUPT_ENABLE)) return;
    v.interruptPending = true;
  }
}

void IIgsSound::scan() {
  // Everything the oscillators produce is summed, because the chip has one
  // analogue output pin: it visits its channels in turn and puts each one's
  // sample on that same pin, with the channel strobes saying which channel is
  // on it at the time. A stock machine low-pass filters the pin and hears the
  // sum of all of them; only a stereo card in a slot uses the strobes to pull
  // the channels apart, and there is no such card here.
  //
  // Splitting by the channel field instead — odd channels one side, even the
  // other — is what a stereo card would do, and it put a game's bass in one
  // speaker and its melody in the other. Spy Hunter played one or the other
  // rather than both.
  float mono = 0.0f;

  for (int index = 0; index < oscillatorsEnabled_; index++) {
    Voice &v = voices_[index];
    if (v.control & OSC_HALT) continue;

    const int shift = resolutionShift(v);
    const uint32_t length = tableLength(v);
    // The table's start is the pointer with as many low bits masked as the
    // table is long: a 4K table starts on a 4K boundary.
    const uint32_t start = v.wavePointer & ~(length - 1) & 0x1FFFF;
    const uint32_t position = v.accumulator >> shift;
    const uint32_t offset = position & (length - 1);
    v.accumulator += v.frequency;

    const uint8_t byte = ram_[(start + offset) & (SOUND_RAM_SIZE - 1)];
    v.data = byte;
    if (byte == 0) {
      haltOscillator(index, false, v.control);
      continue;
    }

    const int mode = (v.control & OSC_MODE_MASK) >> 1;
    const float sample = static_cast<float>(static_cast<int8_t>(byte ^ 0x80)) *
                         static_cast<float>(v.volume);
    if (mode == (OSC_MODE_SYNC >> 1) && (index & 1)) {
      // In AM mode an odd oscillator is heard through the even one above it:
      // what it reads becomes that one's volume.
      if (index + 1 < DOC_OSCILLATOR_COUNT &&
          !(voices_[index + 1].control & OSC_HALT)) {
        voices_[index + 1].volume = static_cast<uint8_t>(byte);
      }
    } else {
      // The chip's own quirk: the last enabled oscillator is heard three
      // times over.
      const float weight = (index == oscillatorsEnabled_ - 1) ? 3.0f : 1.0f;
      // Which channel it is assigned to makes no difference without a card to
      // separate them: it reaches the same pin either way.
      mono += sample * weight;
    }

    if (position >= length - 1) haltOscillator(index, true, v.control);
  }

  // **The $C03C volume nibble is deliberately not applied here.**
  //
  // On the real machine the amplifier attenuates everything, this chip
  // included, and this used to model that. It sounds wrong, and the reason is
  // what the nibble is used for in practice rather than what it is for on
  // paper: firmware and sound tools drop it to about 5 and put it back to 15
  // around every burst of DOC access, in flips lasting well under ten
  // milliseconds. Scaling the chip's output by that turns a steady note into
  // one wobbling at the rate the software happens to be transferring at, and
  // leaves the average level low into the bargain. Applying the flips
  // instantly is worse still: it chops the waveform.
  //
  // So the chip plays at its own level and the host's volume control is the
  // amplifier. GSSquared does the same, for the same reason, and notes two
  // more: a stereo card taps these channels ahead of the volume control, and
  // at least one game (Alien Mind) sets the nibble to zero while playing
  // through one. The speaker keeps the nibble — see IIgsMachine — because the
  // ROM's bell fades by walking it down, and that is audible and right.
  //
  // Eight bits of sample by eight of volume, and the chip's own mixer divides
  // by eight: one full-volume oscillator is an eighth of full scale. That
  // eighth is the same one GSSquared uses.
  constexpr float SCALE = 1.0f / (128.0f * 255.0f * 8.0f);
  const size_t slot = static_cast<size_t>(produced_ % RING_FRAMES) * 2;
  // The one signal goes to both speakers, which is what a mono machine's
  // output does when the host plays it in stereo.
  ring_[slot] = mono * SCALE;
  ring_[slot + 1] = mono * SCALE;
  produced_++;
}

void IIgsSound::advance(uint32_t slowCycles) {
  // The slow clock is the 7.16MHz clock divided by seven — the same crystal,
  // fourteen dots to a cycle and two ticks to a dot.
  ticks_ += static_cast<double>(slowCycles) * 7.0;
  const double ticksPerScan = 8.0 * (oscillatorsEnabled_ + 2);
  while (ticks_ >= ticksPerScan) {
    ticks_ -= ticksPerScan;
    scan();
  }
}

void IIgsSound::generateSamples(float *buffer, int frames, int rate) {
  if (!buffer || frames <= 0 || rate <= 0) return;

  // The chip's rate over the host's is the step; the backlog says whether
  // the two are drifting apart, and the step leans up to half a percent the
  // other way to hold it near a few milliseconds' worth. A step that was
  // simply "whatever spreads the backlog over this buffer" warbled by the
  // rounding of each buffer's share.
  const double nominal = sampleRate() / rate;
  const double target = sampleRate() * 0.004;
  const double available = static_cast<double>(produced_) - consumed_;
  double error = (available - target) / target;
  if (error > 1.0) error = 1.0;
  if (error < -1.0) error = -1.0;
  const double step = nominal * (1.0 + 0.005 * error);

  // If the host fell behind by more than the ring holds, the oldest of it is
  // gone; start from what is still there.
  const double oldest = static_cast<double>(produced_) - (RING_FRAMES - 1);
  if (consumed_ < oldest) consumed_ = oldest;

  for (int frame = 0; frame < frames; frame++) {
    if (consumed_ + 1.0 >= static_cast<double>(produced_)) {
      // The chip has not got this far yet: hold the last frame rather than
      // drop to silence, so a short buffer does not click.
      buffer[frame * 2] = lastLeft_;
      buffer[frame * 2 + 1] = lastRight_;
      continue;
    }
    const uint64_t whole = static_cast<uint64_t>(consumed_);
    const float fraction = static_cast<float>(consumed_ - static_cast<double>(whole));
    const size_t a = static_cast<size_t>(whole % RING_FRAMES) * 2;
    const size_t b = static_cast<size_t>((whole + 1) % RING_FRAMES) * 2;
    lastLeft_ = ring_[a] + (ring_[b] - ring_[a]) * fraction;
    lastRight_ = ring_[a + 1] + (ring_[b + 1] - ring_[a + 1]) * fraction;
    buffer[frame * 2] = lastLeft_;
    buffer[frame * 2 + 1] = lastRight_;
    consumed_ += step;
  }
}

IIgsSound::Oscillator IIgsSound::oscillator(int index) const {
  Oscillator osc;
  if (index < 0 || index >= DOC_OSCILLATOR_COUNT) return osc;
  const Voice &v = voices_[index];
  osc.frequency = v.frequency;
  osc.volume = v.volume;
  osc.waveTablePointer = static_cast<uint8_t>(v.wavePointer >> 8);
  osc.control = v.control;
  osc.tableSize = docRegister(static_cast<uint8_t>(DOC_WAVE_SIZE + index));
  osc.accumulator = v.accumulator;
  osc.data = v.data;
  osc.interruptPending = v.interruptPending;
  return osc;
}

bool IIgsSound::oscillatorHalted(int index) const {
  if (index < 0 || index >= DOC_OSCILLATOR_COUNT) return true;
  return (voices_[index].control & OSC_HALT) != 0;
}


void IIgsSound::serialize(StateWriter &w) const {
  for (const Voice &v : voices_) {
    w.u16(v.frequency);
    w.u8(v.volume);
    w.u32(v.wavePointer);
    w.u8(v.control);
    w.u8(v.sizeCode);
    w.u8(v.resolution);
    w.u32(v.accumulator);
    w.u8(v.data);
    w.boolean(v.interruptPending);
  }
  w.i32(oscillatorsEnabled_);
  w.u8(enableRegister_);
  w.u8(interruptRegister_);
  w.bytes(ram_.data(), ram_.size());
  w.u16(address_);
  w.u8(control_);
  w.u8(latch_);
  w.f64(ticks_);
}

void IIgsSound::deserialize(StateReader &r) {
  for (Voice &v : voices_) {
    v.frequency = r.u16();
    v.volume = r.u8();
    v.wavePointer = r.u32();
    v.control = r.u8();
    v.sizeCode = r.u8();
    v.resolution = r.u8();
    v.accumulator = r.u32();
    v.data = r.u8();
    v.interruptPending = r.boolean();
  }
  oscillatorsEnabled_ = r.i32();
  enableRegister_ = r.u8();
  interruptRegister_ = r.u8();
  if (const uint8_t *p = r.bytes(ram_.size())) std::copy(p, p + ram_.size(), ram_.begin());
  address_ = r.u16();
  control_ = r.u8();
  latch_ = r.u8();
  ticks_ = r.f64();
  ring_.assign(RING_FRAMES * 2, 0.0f);
  produced_ = 0;
  consumed_ = 0.0;
  lastLeft_ = lastRight_ = 0.0f;
}

} // namespace a2e::iigs
