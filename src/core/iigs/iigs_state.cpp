/*
 * iigs_state.cpp - A IIgs in a save state
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_machine.hpp"

#include "../audio/audio.hpp"
#include "../cards/disk_controller.hpp"
#include "../cards/smartport/smartport_card.hpp"
#include "../cpu/65816/cpu65816.hpp"
#include "../emulator/drive_state.hpp"
#include "../emulator/state_stream.hpp"
#include "../machine/machine_profile.hpp"
#include "../video/video.hpp"

#include <vector>

namespace a2e::iigs {

/*
 * The same header a //e's state begins with — magic, version, machine id — so
 * the host treats every machine's state the same way and each machine refuses
 * the other's by its id. The version is this machine's own: the layout after
 * the header has nothing in common with a //e's, so there is no reason for
 * the two to move together.
 */
static constexpr uint32_t STATE_MAGIC = 0x53324541; // "A2ES"
static constexpr uint32_t STATE_VERSION = 1;

const uint8_t *IIgsMachine::exportState(size_t *size) {
  stateBuffer_.clear();

  // The fast RAM, the Mega II's, and whatever the two built-in cards weigh —
  // the SmartPort's state is its hard drive images, so with two 32MB volumes
  // that is the bulk of it. Reserved in one go because growing a vector
  // doubles it, and a doubling realloc part-way through briefly needs three
  // times the payload: that is what took the heap past its ceiling and
  // aborted the module rather than failing the save.
  size_t cardBytes = 0;
  for (ExpansionCard *c : {static_cast<ExpansionCard *>(disk_),
                           static_cast<ExpansionCard *>(smartPort_)}) {
    if (c) cardBytes += c->getStateSize() + 8;
  }
  stateBuffer_.reserve(memory_->fastRamSize() + 512 * 1024 + cardBytes);
  StateWriter w(stateBuffer_);

  w.u32(STATE_MAGIC);
  w.u32(STATE_VERSION);
  w.u32(static_cast<uint32_t>(MachineId::AppleIIgs));

  // The 65816, all of it: the width flags matter as much as the registers,
  // and the mode as much as the flags.
  w.boolean(cpu_->getEmulation());
  w.u8(cpu_->getP());
  w.u16(cpu_->getA());
  w.u16(cpu_->getX());
  w.u16(cpu_->getY());
  w.u16(cpu_->getSP());
  w.u16(cpu_->getD());
  w.u16(cpu_->getPC());
  w.u8(cpu_->getPBR());
  w.u8(cpu_->getDBR());
  w.boolean(cpu_->isStopped());
  w.boolean(cpu_->isWaiting());
  w.boolean(cpu_->isIRQPending());
  w.boolean(cpu_->isNMIPending());
  w.u64(cpu_->getTotalCycles());

  memory_->serialize(w);

  // The machine's own counters: where the frame is, what the Ensoniq has
  // been fed, and the speaker's amplifier.
  w.u64(lastFrameCycle_);
  w.u32(static_cast<uint32_t>(samplesGenerated_));
  w.i32(linesFinished_);
  w.u64(soundCycle_);
  w.f32(speakerGain_);
  w.u8(speakerNibble_);
  w.boolean(audio_->getSpeakerState());

  // The IWM and the SmartPort are the two cards in the Mega II's slots, and
  // both are the machine's rather than the user's. The IWM's state is its
  // registers; the SmartPort's is its hard drive images. The floppies follow
  // the IWM, as they do on a //e.
  auto writeCard = [&](ExpansionCard *card) {
    const size_t cardSize = card ? card->getStateSize() : 0;
    if (cardSize == 0) {
      w.u32(0);
      return;
    }
    w.blobFrom(cardSize, [&](uint8_t *dst) {
      return card->serialize(dst, cardSize);
    });
  };
  writeCard(disk_);
  writeDriveState(w, *disk_);
  writeCard(smartPort_);

  *size = stateBuffer_.size();
  return stateBuffer_.data();
}

bool IIgsMachine::importState(const uint8_t *data, size_t size) {
  StateReader r(data, size);
  if (r.u32() != STATE_MAGIC) return false;
  if (r.u32() != STATE_VERSION) return false;
  if (r.u32() != static_cast<uint32_t>(MachineId::AppleIIgs)) return false;
  if (r.failed()) return false;

  reset();

  // Mode first, then the flags, then the registers: setEmulation and setP
  // each force the widths the mode requires, and a 16-bit X restored before
  // the flags said it was 16 bits wide would be truncated.
  cpu_->setEmulation(r.boolean());
  cpu_->setP(r.u8());
  cpu_->setA(r.u16());
  cpu_->setX(r.u16());
  cpu_->setY(r.u16());
  cpu_->setSP(r.u16());
  cpu_->setD(r.u16());
  cpu_->setPC(r.u16());
  cpu_->setPBR(r.u8());
  cpu_->setDBR(r.u8());
  {
    const bool stopped = r.boolean();
    const bool waiting = r.boolean();
    const bool irq = r.boolean();
    const bool nmi = r.boolean();
    cpu_->restoreExecutionState(stopped, waiting, irq, nmi);
  }
  cpu_->setTotalCycles(r.u64());
  if (r.failed()) return false;

  if (!memory_->deserialize(r)) return false;

  lastFrameCycle_ = r.u64();
  samplesGenerated_ = static_cast<int>(r.u32());
  linesFinished_ = r.i32();
  soundCycle_ = r.u64();
  speakerGain_ = r.f32();
  speakerNibble_ = r.u8();
  (void)r.boolean(); // speaker level: the next toggle sets it
  volumeChanges_.clear();
  if (r.failed()) return false;

  auto readCard = [&](ExpansionCard *card) {
    size_t cardSize = 0;
    const uint8_t *cardState = r.blob(cardSize);
    if (card && cardState && cardSize > 0) card->deserialize(cardState, cardSize);
  };
  readCard(disk_);
  if (!readDriveState(r, *disk_)) return false;
  readCard(smartPort_);
  if (r.failed()) return false;

  // The video decodes from the switches as they are now, and the frame that
  // was in flight is gone.
  video_->onVideoSwitchChanged();
  frameReady_ = true;
  debug_.clearHits();
  paused_ = false;
  return true;
}

} // namespace a2e::iigs
