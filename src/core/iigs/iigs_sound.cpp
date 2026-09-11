/*
 * iigs_sound.cpp - The Ensoniq's RAM and the window the CPU reaches it through
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_sound.hpp"

namespace a2e::iigs {

void IIgsSound::reset() {
  accumulator_.fill(0);
  ram_.fill(0);
  doc_.fill(0);
  address_ = 0;
  control_ = 0;
  latch_ = 0;
}

void IIgsSound::advance() {
  if (autoIncrements()) address_ = static_cast<uint16_t>(address_ + 1);
}

uint8_t IIgsSound::readData() {
  // One behind: the chip hands back what it fetched last time and goes to get
  // the next byte. A program that sets the address and reads once gets the
  // previous window's byte, which is why firmware always reads twice.
  // The pointer moves first and the fetch follows it, which is what makes the
  // window one byte behind: what comes back was fetched for the *previous*
  // address. Latching before advancing would hand back the same byte twice and
  // look like memory that repeats.
  const uint8_t value = latch_;
  advance();
  latch_ = addressesRam() ? ram_[address_] : doc_[address_ & 0xFF];
  return value;
}

void IIgsSound::writeData(uint8_t value) {
  // Writes are not delayed — only reads are.
  if (addressesRam()) {
    ram_[address_] = value;
  } else {
    doc_[address_ & 0xFF] = value;
  }
  advance();
}

void IIgsSound::writeAddressLow(uint8_t value) {
  address_ = static_cast<uint16_t>((address_ & 0xFF00) | value);
  latch_ = addressesRam() ? ram_[address_] : doc_[address_ & 0xFF];
}

void IIgsSound::writeAddressHigh(uint8_t value) {
  address_ = static_cast<uint16_t>((address_ & 0x00FF) | (value << 8));
  latch_ = addressesRam() ? ram_[address_] : doc_[address_ & 0xFF];
}


// ============================================================================
// The synthesiser
//
// An oscillator is a pointer walking through the sound RAM: its frequency
// register is added to an accumulator every chip tick, and the top of the
// accumulator picks the byte. What it reads is an unsigned sample, scaled by
// the oscillator's volume and added to one of sixteen channels — of which a
// IIgs wires the even ones to one speaker and the odd ones to the other.
//
// The thing that makes a sample end is a zero byte. The chip halts an
// oscillator that reads one, and firmware relies on it: a sound is a run of
// bytes with a zero after it, and nobody has to count the length.
// ============================================================================

namespace {
// The chip's own rate: it divides a fixed clock between however many
// oscillators are switched on, so eight running oscillators each step twice as
// often as sixteen would.
constexpr double DOC_CLOCK_HZ = 894886.0;

constexpr uint8_t oscillatorRegister(uint8_t base, int index) {
  return static_cast<uint8_t>(base + index);
}
} // namespace

int IIgsSound::activeOscillators() const {
  // $E1 holds "the number of oscillators enabled, times two, minus two", which
  // is the chip's own encoding and is what the firmware writes.
  const int encoded = doc_[DOC_OSCILLATOR_ENABLE];
  const int count = (encoded / 2) + 1;
  return count < 1 ? 1 : (count > DOC_OSCILLATOR_COUNT ? DOC_OSCILLATOR_COUNT
                                                       : count);
}

IIgsSound::Oscillator IIgsSound::oscillator(int index) const {
  Oscillator osc;
  if (index < 0 || index >= DOC_OSCILLATOR_COUNT) return osc;
  osc.frequency = static_cast<uint16_t>(
      doc_[oscillatorRegister(DOC_FREQUENCY_LOW, index)] |
      (doc_[oscillatorRegister(DOC_FREQUENCY_HIGH, index)] << 8));
  osc.volume = doc_[oscillatorRegister(DOC_VOLUME, index)];
  osc.waveTablePointer = doc_[oscillatorRegister(DOC_WAVE_POINTER, index)];
  osc.control = doc_[oscillatorRegister(DOC_CONTROL, index)];
  osc.tableSize = doc_[oscillatorRegister(DOC_WAVE_SIZE, index)];
  osc.accumulator = accumulator_[index];
  return osc;
}

bool IIgsSound::oscillatorHalted(int index) const {
  if (index < 0 || index >= DOC_OSCILLATOR_COUNT) return true;
  return (doc_[oscillatorRegister(DOC_CONTROL, index)] & OSC_HALT) != 0;
}

void IIgsSound::generateSamples(float *buffer, int frames, int rate) {
  if (!buffer || frames <= 0 || rate <= 0) return;

  const int enabled = activeOscillators();
  // How many chip ticks fall inside one host sample.
  const double ticksPerSample = (DOC_CLOCK_HZ / enabled) / rate;

  for (int frame = 0; frame < frames; frame++) {
    double left = 0.0;
    double right = 0.0;

    for (int index = 0; index < enabled; index++) {
      const uint8_t control = doc_[oscillatorRegister(DOC_CONTROL, index)];
      if (control & OSC_HALT) continue;

      // The wave table's size is encoded as a power of two in bits 3-5 of the
      // size register, and how much of the accumulator is address rather than
      // fraction follows from it.
      const uint8_t size = doc_[oscillatorRegister(DOC_WAVE_SIZE, index)];
      const int tableBits = 8 + ((size >> 3) & 0x07);
      const uint32_t tableMask = (1u << tableBits) - 1;

      const uint16_t frequency = static_cast<uint16_t>(
          doc_[oscillatorRegister(DOC_FREQUENCY_LOW, index)] |
          (doc_[oscillatorRegister(DOC_FREQUENCY_HIGH, index)] << 8));

      // One host sample is many chip ticks; stepping by all of them at once is
      // the same arithmetic and a great deal less of it.
      accumulator_[index] += static_cast<uint32_t>(frequency * ticksPerSample);

      const uint32_t offset = (accumulator_[index] >> 9) & tableMask;
      const uint32_t address =
          ((static_cast<uint32_t>(
                doc_[oscillatorRegister(DOC_WAVE_POINTER, index)])
            << 8) +
           offset) &
          (SOUND_RAM_SIZE - 1);

      const uint8_t sample = ram_[address];
      if (sample == 0) {
        // A zero byte is the end of the sound. Free-running oscillators start
        // again; the others stop, and the chip sets the halt bit itself so the
        // program can see that it finished.
        if ((control & OSC_MODE_MASK) == OSC_MODE_FREE_RUN) {
          accumulator_[index] = 0;
        } else {
          doc_[oscillatorRegister(DOC_CONTROL, index)] =
              static_cast<uint8_t>(control | OSC_HALT);
        }
        continue;
      }

      // An unsigned sample centred on 128, scaled by the volume.
      const double value =
          ((static_cast<double>(sample) - 128.0) / 128.0) *
          (doc_[oscillatorRegister(DOC_VOLUME, index)] / 255.0);

      // Sixteen channels, and a IIgs sends the even ones one way and the odd
      // ones the other.
      if ((control & OSC_CHANNEL_MASK) & 0x10) {
        right += value;
      } else {
        left += value;
      }
    }

    // Enough headroom that a handful of oscillators at full volume do not
    // clip; the chip's own mixer is no louder.
    constexpr double SCALE = 0.25;
    buffer[frame * 2] = static_cast<float>(left * SCALE);
    buffer[frame * 2 + 1] = static_cast<float>(right * SCALE);
  }
}

} // namespace a2e::iigs
