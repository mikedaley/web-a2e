/*
 * emulator_state.cpp - State serialization (exportState / importState)
 *
 * Split from emulator.cpp to reduce file size. Implements Emulator member
 * methods for binary save-state export and import.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "../emulator.hpp"
#include "drive_state.hpp"
#include "state_stream.hpp"
#include "../cards/mockingboard/mockingboard_card.hpp"
#include "../input/mouse_iou.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace a2e {

/*
 * The header every machine's state begins with: the magic, the format
 * version, and which machine wrote it. Everything after the header is laid
 * out to that machine's shape — its RAM sizes, its cards, its processor — so a
 * state restored into a different machine would be read as garbage rather
 * than fail. The id is what lets importState refuse it instead, and it is at
 * the same offset in every machine's state so the host can read it without
 * knowing the rest of the layout.
 *
 * Version 9: the cards are recorded by slot and id rather than by a fixed
 * list of three types, so a Super Serial Card, a parallel card, a SoftCard
 * and a //c's built-in ports come back too; a //c's IWM keeps its mode
 * register and its mouse the steps it had banked; a card's state is sized
 * with 32 bits, because a SmartPort card's state is its hard drive image.
 */
static constexpr uint32_t STATE_VERSION = 9;
static constexpr uint32_t STATE_MAGIC = 0x53324541; // "A2ES" in little-endian

const uint8_t *Emulator::exportState(size_t *size) {
  stateBuffer_.clear();

  // ~200KB before any card's state: 128KB of RAM, 32KB of language card, the
  // rest small. The cards are added because a SmartPort card's state is its
  // hard drive images, and growing a vector doubles it — a doubling realloc
  // part-way through a state holding two 32MB volumes briefly needs three
  // times the payload, which is what took the heap over its ceiling and
  // aborted the module instead of failing the save.
  size_t cardBytes = 0;
  for (uint8_t slot = 0; slot < 8; slot++) {
    ExpansionCard *c = machine_->hasSlot(slot) ? mmu_->getCard(slot) : nullptr;
    if (c) cardBytes += c->getStateSize() + 8;
  }
  stateBuffer_.reserve(500000 + cardBytes);
  StateWriter w(stateBuffer_);

  w.u32(STATE_MAGIC);
  w.u32(STATE_VERSION);
  w.u32(static_cast<uint32_t>(machine_->id));

  // CPU
  w.u8(cpu_->getA());
  w.u8(cpu_->getX());
  w.u8(cpu_->getY());
  w.u8(cpu_->getSP());
  w.u8(cpu_->getP());
  w.u8(0); // reserved
  w.u16(cpu_->getPC());
  w.u64(cpu_->getTotalCycles());

  // Memory: main, aux, then the language card's two banks and high RAM for
  // each. Written whatever the machine has fitted, so the layout does not
  // depend on the profile; a II+'s auxiliary half is simply what its unused
  // array holds.
  w.bytes(mmu_->getMainRAM(), MAIN_RAM_SIZE);
  w.bytes(mmu_->getAuxRAM(), AUX_RAM_SIZE);
  for (bool aux : {false, true}) {
    w.bytes(mmu_->getLCBank1(aux), 0x1000);
    w.bytes(mmu_->getLCBank2(aux), 0x1000);
    w.bytes(mmu_->getLCHighRAM(aux), 0x2000);
  }

  w.u32(mmu_->packSwitchesForState());

  // Keyboard latch and the pushbuttons
  w.u8(keyboardLatch_);
  w.boolean(keyDown_);
  w.boolean(buttonState_[0]);
  w.boolean(buttonState_[1]);
  w.boolean(buttonState_[2]);

  // Timing
  w.u64(lastFrameCycle_);
  w.u32(static_cast<uint32_t>(samplesGenerated_));

  w.boolean(audio_->getSpeakerState());

  // Every slot the machine could have, with what is in it and that card's own
  // state. A slot the machine does not have, or an empty one, is recorded as
  // empty so the reader always finds eight entries.
  for (uint8_t slot = 0; slot < 8; slot++) {
    const bool present = machine_->hasSlot(slot);
    w.string(present ? getSlotCardName(slot) : "empty");
    ExpansionCard *card = present ? mmu_->getCard(slot) : nullptr;
    const size_t cardSize = card ? card->getStateSize() : 0;
    if (cardSize == 0) {
      w.u32(0);
      continue;
    }
    w.blobFrom(cardSize, [&](uint8_t *dst) {
      return card->serialize(dst, cardSize);
    });
  }

  // The media in the drives, after the controller so a restore fits the card
  // before it loads the disks into it.
  if (disk_) {
    w.boolean(true);
    writeDriveState(w, *disk_);
  } else {
    w.boolean(false);
  }

  w.boolean(mmu_->isNoSlotClockEnabled());

  // A //c's mouse is not a card, so it is not in the slot list.
  if (mouseIOU_) {
    w.boolean(true);
    mouseIOU_->serialize(w);
  } else {
    w.boolean(false);
  }

  *size = stateBuffer_.size();
  return stateBuffer_.data();
}

bool Emulator::importState(const uint8_t *data, size_t size) {
  StateReader r(data, size);

  // The header is checked before anything is touched, so a bad file leaves a
  // running machine exactly as it was.
  if (r.u32() != STATE_MAGIC) return false;
  if (r.u32() != STATE_VERSION) return false;
  if (r.u32() != static_cast<uint32_t>(machine_->id)) return false;
  if (r.failed()) return false;

  // Start from a clean machine so nothing of the old one survives underneath.
  reset();

  // CPU
  cpu_->setA(r.u8());
  cpu_->setX(r.u8());
  cpu_->setY(r.u8());
  cpu_->setSP(r.u8());
  cpu_->setP(r.u8());
  r.u8(); // reserved
  cpu_->setPC(r.u16());
  cpu_->setTotalCycles(r.u64());

  // Memory
  if (const uint8_t *main = r.bytes(MAIN_RAM_SIZE)) {
    for (uint32_t addr = 0; addr < MAIN_RAM_SIZE; addr++) {
      mmu_->writeRAM(static_cast<uint16_t>(addr), main[addr], false);
    }
  }
  if (const uint8_t *aux = r.bytes(AUX_RAM_SIZE)) {
    for (uint32_t addr = 0; addr < AUX_RAM_SIZE; addr++) {
      mmu_->writeRAM(static_cast<uint16_t>(addr), aux[addr], true);
    }
  }
  for (bool aux : {false, true}) {
    const uint8_t *b1 = r.bytes(0x1000);
    const uint8_t *b2 = r.bytes(0x1000);
    const uint8_t *hi = r.bytes(0x2000);
    if (!hi) return false;
    mmu_->setLCBank1(b1, aux);
    mmu_->setLCBank2(b2, aux);
    mmu_->setLCHighRAM(hi, aux);
  }

  mmu_->restoreSwitchesFromState(r.u32());

  keyboardLatch_ = r.u8();
  keyDown_ = r.boolean();
  buttonState_[0] = r.boolean();
  buttonState_[1] = r.boolean();
  buttonState_[2] = r.boolean();

  lastFrameCycle_ = r.u64();
  samplesGenerated_ = static_cast<int>(r.u32());

  (void)r.boolean(); // speaker level: the next toggle sets it
  if (r.failed()) return false;

  // The cards. A slot the user can change is refitted with whatever the state
  // had in it, through the same path the Expansion Slots window uses, so the
  // card is built with its callbacks; a fixed slot's card is the machine's
  // and is only ever told its state.
  for (uint8_t slot = 0; slot < 8; slot++) {
    const std::string id = r.string();
    size_t cardSize = 0;
    const uint8_t *cardState = r.blob(cardSize);
    if (r.failed()) return false;
    if (!machine_->hasSlot(slot)) continue;

    if (!machine_->slots[slot].fixedCard && id != getSlotCardName(slot)) {
      setSlotCard(slot, id.c_str());
    }
    ExpansionCard *card = mmu_->getCard(slot);
    if (card && cardState && cardSize > 0) {
      card->deserialize(cardState, cardSize);
    }
  }

  if (r.boolean()) {
    if (!disk_) return false; // the state had a controller this machine lost
    if (!readDriveState(r, *disk_)) return false;
  }
  if (r.failed()) return false;

  mmu_->enableNoSlotClock(r.boolean());

  if (r.boolean()) {
    if (mouseIOU_) mouseIOU_->deserialize(r);
    else return false;
  }
  if (r.failed()) return false;

  frameReady_ = true;
  debug_.clearHits();
  paused_ = false;

  // basicProgramRunning_ is inferred from ROM entry points ($D912 RUN, $D43C
  // RESTART), so reset() above cleared it and a state captured mid-RUN would
  // never see $D912 again — the flag would stay false for the rest of the
  // program. Rederive it from the restored RAM using the ROM's own direct-mode
  // test: CURLIN+1 ($76) == $FF means we are at the ] prompt, anything else
  // means a program is executing.
  basicProgramRunning_ = mmu_->readRAM(0x76, false) != 0xFF;

  return true;
}

} // namespace a2e
