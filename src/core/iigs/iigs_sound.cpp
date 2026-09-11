/*
 * iigs_sound.cpp - The Ensoniq's RAM and the window the CPU reaches it through
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_sound.hpp"

namespace a2e::iigs {

void IIgsSound::reset() {
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

} // namespace a2e::iigs
