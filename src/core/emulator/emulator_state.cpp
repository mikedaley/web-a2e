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
#include "../cards/disk2/disk2_card.hpp"
#include "../cards/mockingboard/mockingboard_card.hpp"
#include "../cards/thunderclock/thunderclock_card.hpp"
#include "../cards/mouse/mouse_card.hpp"
#include "../cards/smartport/smartport_card.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace a2e {

// State format version - increment when format changes
static constexpr uint32_t STATE_VERSION = 8;  // Machine id in header
static constexpr uint32_t STATE_MAGIC = 0x53324541; // "A2ES" in little-endian

// Helper to write little-endian values
static void writeLE16(std::vector<uint8_t> &buf, uint16_t val) {
  buf.push_back(val & 0xFF);
  buf.push_back((val >> 8) & 0xFF);
}

static void writeLE32(std::vector<uint8_t> &buf, uint32_t val) {
  buf.push_back(val & 0xFF);
  buf.push_back((val >> 8) & 0xFF);
  buf.push_back((val >> 16) & 0xFF);
  buf.push_back((val >> 24) & 0xFF);
}

static void writeLE64(std::vector<uint8_t> &buf, uint64_t val) {
  for (int i = 0; i < 8; i++) {
    buf.push_back((val >> (i * 8)) & 0xFF);
  }
}

// Helper to read little-endian values
static uint16_t readLE16(const uint8_t *data) {
  return data[0] | (data[1] << 8);
}

static uint32_t readLE32(const uint8_t *data) {
  return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

static uint64_t readLE64(const uint8_t *data) {
  uint64_t val = 0;
  for (int i = 0; i < 8; i++) {
    val |= static_cast<uint64_t>(data[i]) << (i * 8);
  }
  return val;
}

const uint8_t *Emulator::exportState(size_t *size) {
  stateBuffer_.clear();

  // Reserve approximate size (slightly over to avoid reallocations)
  // ~200KB total (128KB main/aux + 32KB LC RAM + overhead + disk images)
  stateBuffer_.reserve(500000);

  // Header
  writeLE32(stateBuffer_, STATE_MAGIC);
  writeLE32(stateBuffer_, STATE_VERSION);

  // Which machine this state came off. Everything after this point is laid out
  // to that machine's shape — its RAM sizes, its cards — so a state restored
  // into a different machine would be read as garbage rather than fail. The id
  // is what lets importState refuse it instead.
  writeLE32(stateBuffer_, static_cast<uint32_t>(machine_->id));

  // CPU state
  stateBuffer_.push_back(cpu_->getA());
  stateBuffer_.push_back(cpu_->getX());
  stateBuffer_.push_back(cpu_->getY());
  stateBuffer_.push_back(cpu_->getSP());
  stateBuffer_.push_back(cpu_->getP());
  stateBuffer_.push_back(0); // Reserved
  writeLE16(stateBuffer_, cpu_->getPC());
  writeLE64(stateBuffer_, cpu_->getTotalCycles());

  // Memory - Main RAM (64KB)
  for (uint32_t addr = 0; addr < MAIN_RAM_SIZE; addr++) {
    stateBuffer_.push_back(mmu_->readRAM(addr, false));
  }

  // Memory - Aux RAM (64KB)
  for (uint32_t addr = 0; addr < AUX_RAM_SIZE; addr++) {
    stateBuffer_.push_back(mmu_->readRAM(addr, true));
  }

  // Language Card RAM - Main (16KB total: 4KB bank1 + 4KB bank2 + 8KB high)
  const uint8_t *lcb1 = mmu_->getLCBank1(false);
  const uint8_t *lcb2 = mmu_->getLCBank2(false);
  const uint8_t *lchi = mmu_->getLCHighRAM(false);
  stateBuffer_.insert(stateBuffer_.end(), lcb1, lcb1 + 0x1000);
  stateBuffer_.insert(stateBuffer_.end(), lcb2, lcb2 + 0x1000);
  stateBuffer_.insert(stateBuffer_.end(), lchi, lchi + 0x2000);

  // Language Card RAM - Aux (16KB total)
  const uint8_t *alcb1 = mmu_->getLCBank1(true);
  const uint8_t *alcb2 = mmu_->getLCBank2(true);
  const uint8_t *alchi = mmu_->getLCHighRAM(true);
  stateBuffer_.insert(stateBuffer_.end(), alcb1, alcb1 + 0x1000);
  stateBuffer_.insert(stateBuffer_.end(), alcb2, alcb2 + 0x1000);
  stateBuffer_.insert(stateBuffer_.end(), alchi, alchi + 0x2000);

  // Soft switches - pack into bytes
  const auto &sw = mmu_->getSoftSwitches();
  uint32_t switches1 = 0;
  if (sw.text) switches1 |= (1 << 0);
  if (sw.mixed) switches1 |= (1 << 1);
  if (sw.page2) switches1 |= (1 << 2);
  if (sw.hires) switches1 |= (1 << 3);
  if (sw.col80) switches1 |= (1 << 4);
  if (sw.altCharSet) switches1 |= (1 << 5);
  if (sw.store80) switches1 |= (1 << 6);
  if (sw.ramrd) switches1 |= (1 << 7);
  if (sw.ramwrt) switches1 |= (1 << 8);
  if (sw.intcxrom) switches1 |= (1 << 9);
  if (sw.altzp) switches1 |= (1 << 10);
  if (sw.slotc3rom) switches1 |= (1 << 11);
  if (sw.intc8rom) switches1 |= (1 << 12);
  if (sw.lcram) switches1 |= (1 << 13);
  if (sw.lcram2) switches1 |= (1 << 14);
  if (sw.lcwrite) switches1 |= (1 << 15);
  if (sw.lcprewrite) switches1 |= (1 << 16);
  if (sw.an0) switches1 |= (1 << 17);
  if (sw.an1) switches1 |= (1 << 18);
  if (sw.an2) switches1 |= (1 << 19);
  if (sw.an3) switches1 |= (1 << 20);
  if (sw.ioudis) switches1 |= (1 << 21);
  writeLE32(stateBuffer_, switches1);

  // Keyboard latch
  stateBuffer_.push_back(keyboardLatch_);
  stateBuffer_.push_back(keyDown_ ? 1 : 0);

  // Button state
  stateBuffer_.push_back(buttonState_[0] ? 1 : 0);
  stateBuffer_.push_back(buttonState_[1] ? 1 : 0);
  stateBuffer_.push_back(buttonState_[2] ? 1 : 0);

  // Emulator timing
  writeLE64(stateBuffer_, lastFrameCycle_);
  writeLE32(stateBuffer_, samplesGenerated_);

  // Disk controller state
  auto &disk = *disk_;
  stateBuffer_.push_back(disk.isMotorOn() ? 1 : 0);
  stateBuffer_.push_back(static_cast<uint8_t>(disk.getSelectedDrive()));
  stateBuffer_.push_back(disk.getQ6() ? 1 : 0);
  stateBuffer_.push_back(disk.getQ7() ? 1 : 0);
  stateBuffer_.push_back(disk.getPhaseStates());
  stateBuffer_.push_back(disk.getDataLatch());
  stateBuffer_.push_back(disk.getSequencerState());
  stateBuffer_.push_back(disk.getBusData());
  stateBuffer_.push_back(disk.getLSSClock());

  // Per-drive state (track positions, disk image data, and filenames)
  for (int drive = 0; drive < 2; drive++) {
    if (disk.hasDisk(drive)) {
      const auto *image = disk.getDiskImage(drive);
      stateBuffer_.push_back(1); // Disk present
      writeLE16(stateBuffer_, static_cast<uint16_t>(image->getQuarterTrack()));

      // Export the disk image data
      size_t diskSize = 0;
      const uint8_t *diskData = disk.exportDiskData(drive, &diskSize);
      writeLE32(stateBuffer_, static_cast<uint32_t>(diskSize));
      if (diskData && diskSize > 0) {
        stateBuffer_.insert(stateBuffer_.end(), diskData, diskData + diskSize);
      }

      // Save filename
      const std::string &filename = image->getFilename();
      writeLE16(stateBuffer_, static_cast<uint16_t>(filename.length()));
      stateBuffer_.insert(stateBuffer_.end(), filename.begin(), filename.end());
    } else {
      stateBuffer_.push_back(0); // No disk
      writeLE16(stateBuffer_, 0);
      writeLE32(stateBuffer_, 0); // Zero disk size
      writeLE16(stateBuffer_, 0); // Zero filename length
    }
  }

  // Audio state (speaker)
  stateBuffer_.push_back(audio_->getSpeakerState() ? 1 : 0);

  // Mockingboard state
  uint8_t mbState[MockingboardCard::STATE_SIZE];
  size_t mbSize = 0;
  if (mockingboard_) {
    mbSize = mockingboard_->serialize(mbState, sizeof(mbState));
  }
  writeLE16(stateBuffer_, static_cast<uint16_t>(mbSize));
  if (mbSize > 0) {
    stateBuffer_.insert(stateBuffer_.end(), mbState, mbState + mbSize);
  }

  // Expansion card states (slots 1-7, excluding 4 and 6 which are handled above)
  // First, count how many cards have state to save
  uint8_t cardCount = 0;
  for (uint8_t slot = 1; slot <= 7; slot++) {
    if (slot == 4 || slot == 6) continue;  // Handled by legacy system
    ExpansionCard* card = mmu_->getCard(slot);
    if (card && card->getStateSize() > 0) {
      cardCount++;
    }
  }
  stateBuffer_.push_back(cardCount);

  // Save each card's state
  for (uint8_t slot = 1; slot <= 7; slot++) {
    if (slot == 4 || slot == 6) continue;
    ExpansionCard* card = mmu_->getCard(slot);
    if (card && card->getStateSize() > 0) {
      // Slot number
      stateBuffer_.push_back(slot);

      // Card type identifier (use name for identification)
      const char* name = card->getName();
      uint8_t cardType = 0;
      if (strcmp(name, "Thunderclock") == 0) cardType = 1;
      if (strcmp(name, "Mouse") == 0) cardType = 2;
      if (strcmp(name, "SmartPort") == 0) cardType = 3;
      stateBuffer_.push_back(cardType);

      // Card state
      size_t stateSize = card->getStateSize();
      writeLE16(stateBuffer_, static_cast<uint16_t>(stateSize));
      size_t currentSize = stateBuffer_.size();
      stateBuffer_.resize(currentSize + stateSize);
      card->serialize(stateBuffer_.data() + currentSize, stateSize);
    }
  }

  // No-Slot Clock state
  stateBuffer_.push_back(mmu_->isNoSlotClockEnabled() ? 1 : 0);

  *size = stateBuffer_.size();
  return stateBuffer_.data();
}

bool Emulator::importState(const uint8_t *data, size_t size) {
  // Minimum size check
  if (size < 8) {
    return false;
  }

  size_t offset = 0;

  // Check magic and version before resetting
  uint32_t magic = readLE32(data + offset);
  offset += 4;
  if (magic != STATE_MAGIC) {
    return false;
  }

  uint32_t version = readLE32(data + offset);
  offset += 4;
  if (version != STATE_VERSION) {
    return false;
  }

  // Refuse a state saved off a different machine. The remaining bytes are sized
  // and ordered by the saving machine's profile, so importing them here would
  // quietly load nonsense rather than fail.
  if (offset + 4 > size) return false;
  uint32_t stateMachine = readLE32(data + offset);
  offset += 4;
  if (stateMachine != static_cast<uint32_t>(machine_->id)) {
    return false;
  }

  // Reset emulator to clean state before importing
  // This ensures no old state is left floating around
  reset();

  // CPU state
  if (offset + 16 > size) return false;
  cpu_->setA(data[offset++]);
  cpu_->setX(data[offset++]);
  cpu_->setY(data[offset++]);
  cpu_->setSP(data[offset++]);
  cpu_->setP(data[offset++]);
  offset++; // Reserved

  cpu_->setPC(readLE16(data + offset));
  offset += 2;

  // Restore total cycles for accurate disk timing
  uint64_t totalCycles = readLE64(data + offset);
  offset += 8;
  cpu_->setTotalCycles(totalCycles);

  // Memory - Main RAM (64KB)
  if (offset + MAIN_RAM_SIZE > size) return false;
  for (uint32_t addr = 0; addr < MAIN_RAM_SIZE; addr++) {
    mmu_->writeRAM(addr, data[offset++], false);
  }

  // Memory - Aux RAM (64KB)
  if (offset + AUX_RAM_SIZE > size) return false;
  for (uint32_t addr = 0; addr < AUX_RAM_SIZE; addr++) {
    mmu_->writeRAM(addr, data[offset++], true);
  }

  // Language Card RAM - Main (16KB: 4KB + 4KB + 8KB)
  if (offset + 0x4000 > size) return false;
  mmu_->setLCBank1(data + offset, false);
  offset += 0x1000;
  mmu_->setLCBank2(data + offset, false);
  offset += 0x1000;
  mmu_->setLCHighRAM(data + offset, false);
  offset += 0x2000;

  // Language Card RAM - Aux (16KB)
  if (offset + 0x4000 > size) return false;
  mmu_->setLCBank1(data + offset, true);
  offset += 0x1000;
  mmu_->setLCBank2(data + offset, true);
  offset += 0x1000;
  mmu_->setLCHighRAM(data + offset, true);
  offset += 0x2000;

  // Soft switches
  if (offset + 4 > size) return false;
  uint32_t switches1 = readLE32(data + offset);
  offset += 4;

  // Restore soft switches by writing to appropriate addresses
  // TEXT/GRAPHICS
  mmu_->write((switches1 & (1 << 0)) ? 0xC051 : 0xC050, 0);
  // MIXED
  mmu_->write((switches1 & (1 << 1)) ? 0xC053 : 0xC052, 0);
  // PAGE1/PAGE2
  mmu_->write((switches1 & (1 << 2)) ? 0xC055 : 0xC054, 0);
  // LORES/HIRES
  mmu_->write((switches1 & (1 << 3)) ? 0xC057 : 0xC056, 0);
  // 80COL
  mmu_->write((switches1 & (1 << 4)) ? 0xC00D : 0xC00C, 0);
  // ALTCHARSET
  mmu_->write((switches1 & (1 << 5)) ? 0xC00F : 0xC00E, 0);
  // 80STORE
  mmu_->write((switches1 & (1 << 6)) ? 0xC001 : 0xC000, 0);
  // RAMRD
  mmu_->write((switches1 & (1 << 7)) ? 0xC003 : 0xC002, 0);
  // RAMWRT
  mmu_->write((switches1 & (1 << 8)) ? 0xC005 : 0xC004, 0);
  // INTCXROM
  mmu_->write((switches1 & (1 << 9)) ? 0xC007 : 0xC006, 0);
  // ALTZP
  mmu_->write((switches1 & (1 << 10)) ? 0xC009 : 0xC008, 0);
  // SLOTC3ROM
  mmu_->write((switches1 & (1 << 11)) ? 0xC00B : 0xC00A, 0);
  // INTC8ROM - this is set automatically by slot access
  // LCRAM, LCRAM2, LCWRITE - need special handling via language card switches
  // AN0-AN3
  mmu_->write((switches1 & (1 << 17)) ? 0xC059 : 0xC058, 0);
  mmu_->write((switches1 & (1 << 18)) ? 0xC05B : 0xC05A, 0);
  mmu_->write((switches1 & (1 << 19)) ? 0xC05D : 0xC05C, 0);
  mmu_->write((switches1 & (1 << 20)) ? 0xC05F : 0xC05E, 0);

  // Language card state restoration
  bool lcram = switches1 & (1 << 13);
  bool lcram2 = switches1 & (1 << 14);
  bool lcwrite = switches1 & (1 << 15);
  // Select the appropriate language card mode
  // lcram2 means bank 2 is selected (not bank 1)
  // $C080: bank2, read RAM, no write
  // $C081: bank2, read ROM, write (needs double read)
  // $C083: bank2, read RAM, write (needs double read)
  // $C088: bank1, read RAM, no write
  // etc.
  if (lcram) {
    if (lcram2) {
      if (lcwrite) {
        mmu_->read(0xC083);
        mmu_->read(0xC083); // Double read to enable write
      } else {
        mmu_->read(0xC080);
      }
    } else {
      if (lcwrite) {
        mmu_->read(0xC08B);
        mmu_->read(0xC08B);
      } else {
        mmu_->read(0xC088);
      }
    }
  } else {
    if (lcram2) {
      mmu_->read(0xC082);
    } else {
      mmu_->read(0xC08A);
    }
  }

  // Keyboard state
  if (offset + 5 > size) return false;
  keyboardLatch_ = data[offset++];
  keyDown_ = data[offset++] != 0;

  // Button state
  buttonState_[0] = data[offset++] != 0;
  buttonState_[1] = data[offset++] != 0;
  buttonState_[2] = data[offset++] != 0;

  // Emulator timing
  if (offset + 12 > size) return false;
  lastFrameCycle_ = readLE64(data + offset);
  offset += 8;
  samplesGenerated_ = static_cast<int>(readLE32(data + offset));
  offset += 4;

  // Disk controller state
  if (offset + 9 > size) return false;
  bool motorOn = data[offset++] != 0;
  int selectedDrive = data[offset++];
  bool q6 = data[offset++] != 0;
  bool q7 = data[offset++] != 0;
  uint8_t phaseStates = data[offset++];
  uint8_t dataLatch = data[offset++];
  uint8_t seqState = data[offset++];
  uint8_t busDataVal = data[offset++];
  uint8_t lssClockVal = data[offset++];

  // Restore disk controller state (including motor for mid-load restores)
  if (disk_) {
    disk_->setMotorOn(motorOn);
    disk_->setSelectedDrive(selectedDrive);
    disk_->setQ6(q6);
    disk_->setQ7(q7);
    disk_->setPhaseStates(phaseStates);
    disk_->setDataLatch(dataLatch);
    disk_->setSequencerState(seqState);
    disk_->setBusData(busDataVal);
    disk_->setLSSClock(lssClockVal);
  }

  // Per-drive state (disk images and track positions)
  for (int drive = 0; drive < 2; drive++) {
    if (offset + 3 > size) return false;
    bool hasDisk = data[offset++] != 0;
    uint16_t quarterTrack = readLE16(data + offset);
    offset += 2;

    // Read disk size
    if (offset + 4 > size) return false;
    uint32_t diskSize = readLE32(data + offset);
    offset += 4;

    if (hasDisk && diskSize > 0) {
      if (offset + diskSize > size) return false;

      // Read disk data offset for insertion
      const uint8_t *diskData = data + offset;
      offset += diskSize;

      // Read filename
      if (offset + 2 > size) return false;
      uint16_t filenameLen = readLE16(data + offset);
      offset += 2;

      std::string filename = "state.dsk";
      if (filenameLen > 0 && offset + filenameLen <= size) {
        filename = std::string(reinterpret_cast<const char*>(data + offset), filenameLen);
        offset += filenameLen;
      }

      // Insert the disk image from the saved state
      if (disk_) {
        disk_->insertDisk(drive, diskData, diskSize, filename);

        // Restore track position
        auto *image = disk_->getMutableDiskImage(drive);
        if (image) {
          image->setQuarterTrack(quarterTrack);
        }
      }
    } else {
      // Skip filename length field (will be 0)
      if (offset + 2 <= size) {
        offset += 2;
      }
      // Eject any existing disk
      if (disk_) disk_->ejectDisk(drive);
    }
  }

  // Audio state
  if (offset + 1 > size) return false;
  bool speakerState = data[offset++] != 0;
  (void)speakerState;

  // Mockingboard state
  if (offset + 2 <= size) {
    uint16_t mbSize = readLE16(data + offset);
    offset += 2;
    if (mbSize > 0 && offset + mbSize <= size) {
      if (mockingboard_) {
        mockingboard_->deserialize(data + offset, mbSize);
      }
      offset += mbSize;
    }
  }

  // Expansion card states
  if (offset + 1 <= size) {
    uint8_t cardCount = data[offset++];
    for (uint8_t i = 0; i < cardCount && offset + 4 <= size; i++) {
      uint8_t slot = data[offset++];
      uint8_t cardType = data[offset++];
      uint16_t stateSize = readLE16(data + offset);
      offset += 2;

      if (offset + stateSize > size) break;

      // Create card based on type if slot is empty
      if (slot >= 1 && slot <= 7 && slot != 4 && slot != 6) {
        ExpansionCard* existingCard = mmu_->getCard(slot);

        // Create appropriate card type if needed
        if (!existingCard) {
          switch (cardType) {
            case 1: {  // Thunderclock
              auto card = std::make_unique<ThunderclockCard>();
              mmu_->insertCard(slot, std::move(card));
              existingCard = mmu_->getCard(slot);
              break;
            }
            case 2: {  // Mouse
              auto card = std::make_unique<MouseCard>();
              card->setSlotNumber(slot);
              card->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
              card->setIRQCallback([this]() { cpu_->irq(); });
              mouse_ = card.get();
              mmu_->insertCard(slot, std::move(card));
              existingCard = mmu_->getCard(slot);
              break;
            }
            case 3: {  // SmartPort
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
              card->setGetPC([this]() { return cpu_->getPC(); });
              card->setSetPC([this](uint16_t v) { cpu_->setPC(v); });
              card->setSetX([this](uint8_t v) { cpu_->setX(v); });
              card->setSetY([this](uint8_t v) { cpu_->setY(v); });
              smartport_ = card.get();
              mmu_->insertCard(slot, std::move(card));
              existingCard = mmu_->getCard(slot);
              break;
            }
          }
        }

        // Restore card state
        if (existingCard) {
          existingCard->deserialize(data + offset, stateSize);
        }
      }

      offset += stateSize;
    }
  }

  // No-Slot Clock state (optional, backwards compatible)
  if (offset + 1 <= size) {
    bool nscEnabled = data[offset++] != 0;
    mmu_->enableNoSlotClock(nscEnabled);
  }

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
