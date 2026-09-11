/*
 * iigs_memory.cpp - An Apple IIgs's address space
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_memory.hpp"

#include "../mmu/mmu.hpp"

#include <algorithm>
#include <cstring>

namespace a2e::iigs {

namespace {
// The registers that are the IIgs's own. Everything else in $C0xx is the //e's
// and belongs to the Mega II.
constexpr uint16_t REG_SLOT_SELECT = 0xC02D;
constexpr uint16_t REG_DISK_SELECT = 0xC031;
constexpr uint16_t REG_CLOCK_DATA = 0xC033;
constexpr uint16_t REG_CLOCK_CONTROL = 0xC034;
constexpr uint16_t REG_NEW_VIDEO = 0xC029;
constexpr uint16_t REG_SHADOW = 0xC035;
constexpr uint16_t REG_SPEED = 0xC036;
constexpr uint16_t REG_STATE = 0xC068;
constexpr uint16_t REG_ADB_MOUSE = 0xC024;
constexpr uint16_t REG_ADB_MODIFIERS = 0xC025;
constexpr uint16_t REG_ADB_DATA = 0xC026;
constexpr uint16_t REG_ADB_STATUS = 0xC027;
constexpr uint16_t REG_SOUND_CONTROL = 0xC03C;
constexpr uint16_t REG_SOUND_DATA = 0xC03D;
constexpr uint16_t REG_SOUND_ADDRESS_LOW = 0xC03E;
constexpr uint16_t REG_SOUND_ADDRESS_HIGH = 0xC03F;

constexpr uint16_t IO_BASE = 0xC000;
constexpr uint16_t IO_END = 0xD000;
constexpr uint16_t LANGUAGE_CARD_BASE = 0xD000;

// Where the display lives on the Mega II side, which is what shadowing exists
// to keep up to date.
constexpr uint16_t TEXT_PAGE1_BASE = 0x0400;
constexpr uint16_t TEXT_PAGE1_END = 0x0800;
constexpr uint16_t HIRES_PAGE1_BASE = 0x2000;
constexpr uint16_t HIRES_PAGE1_END = 0x4000;
constexpr uint16_t HIRES_PAGE2_BASE = 0x4000;
constexpr uint16_t HIRES_PAGE2_END = 0x6000;
// Super Hi-Res is bank $01's $2000-$9FFF: the pixels, the scanline control
// bytes and the palettes, all of which the video reads from $E1.
constexpr uint16_t SHR_BASE = 0x2000;
constexpr uint16_t SHR_END = 0xA000;
} // namespace

IIgsMemory::IIgsMemory(size_t fastRamSize)
    : fastRam_(std::min(fastRamSize, FAST_RAM_SIZE_MAX), 0),
      // The Mega II is described by the //e's profile, not the IIgs's, and
      // that is not a shortcut: this object *is* a //e's memory and video
      // generator, with a //e's 128KB and a //e's 560-dot picture. The IIgs
      // profile's display section describes the machine's screen — 640 dots of
      // Super Hi-Res — which belongs to the video system that will composite
      // this one into it, and would size this one's framebuffer wrongly.
      megaII_(std::make_unique<MMU>(machineProfile(MachineId::AppleIIe))) {
  reset();
}

IIgsMemory::~IIgsMemory() = default;

void IIgsMemory::loadROM(const uint8_t *rom, size_t size) {
  rom_ = rom;
  romSize_ = rom ? size : 0;
  romHighBankFirst_ = false;
  if (!rom_ || romSize_ < 2 * BANK_SIZE) return;

  // Find bank $FF by looking for the emulation reset vector, which every IIgs
  // ROM has at $FF:FFFC and which points into the ROM's own firmware. If the
  // half that would be $FF under the obvious reading has nothing there and the
  // other half does, the image is stored high bank first.
  auto vectorAt = [this](size_t bankStart) {
    const size_t at = bankStart + 0xFFFC;
    return static_cast<uint16_t>(rom_[at] | (rom_[at + 1] << 8));
  };
  const uint16_t lastBankVector = vectorAt(romSize_ - BANK_SIZE);
  const uint16_t firstBankVector = vectorAt(0);
  romHighBankFirst_ = (lastBankVector == 0x0000) && (firstBankVector != 0x0000);
}

void IIgsMemory::reset() {
  // Shadowing all on, slow clock: a IIgs comes up pretending to be a //e as
  // hard as it can, and the firmware turns things on from there.
  resetClock();
  shadow_ = 0;
  speed_ = 0;
  newVideo_ = 0;
  slotSelect_ = 0;
  diskSelect_ = 0;
  adb_.reset();
  clock_.reset();
  sound_.reset();
  megaII_->reset();
}

// ============================================================================
// Where an address lands
// ============================================================================

IIgsMemory::Region IIgsMemory::regionFor(uint32_t address, uint8_t bank,
                                         uint16_t offset) const {
  (void)address;

  const bool megaIIBank = bank == SLOW_BANK_MAIN || bank == SLOW_BANK_AUX;
  const bool shadowedBank = bank == 0x00 || bank == 0x01;

  // $C000-$CFFF is I/O in the four banks that can see it — the Mega II's two,
  // and banks $00 and $01 unless the firmware has inhibited it. Everywhere
  // else it is ordinary memory, which is the whole point of a 24-bit bus: a
  // program can have sixteen megabytes without a hole in every bank.
  if (offset >= IO_BASE && offset < IO_END) {
    if (megaIIBank) return Region::IO;
    if (shadowedBank && ioAndLanguageCardVisible()) return Region::IO;
  }

  if (megaIIBank) return Region::MegaII;
  if (bank >= 0xF0) return Region::ROM;
  if (bankIsPopulated(bank)) return Region::FastRAM;
  return Region::Unpopulated;
}

// ============================================================================
// Reading and writing
// ============================================================================

uint8_t IIgsMemory::read(uint32_t address) {
  const uint8_t bank = static_cast<uint8_t>(address >> 16);
  const uint16_t offset = static_cast<uint16_t>(address);

  const Region region = regionFor(address, bank, offset);
  // Reaching the Mega II costs the Mega II's time, whichever clock the
  // processor is running at.
  if (region == Region::IO || region == Region::MegaII) slowCycles_++;

  switch (region) {
  case Region::IO:
    return readIO(offset);

  case Region::MegaII:
    // Above $D000 the Mega II's map has the language card in it, and RDROM
    // says whether that space reads the card's RAM or the machine's ROM. Below
    // it, a bank access is a bank access: $E0 is main and $E1 is aux, and the
    // //e's RAMRD has nothing to say about it.
    if (offset >= LANGUAGE_CARD_BASE) {
      if ((stateRegister() & STATE_RDROM) != 0) {
        return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
      }
      return megaII_->read(offset);
    }
    return megaII_->readRAM(offset, bank == SLOW_BANK_AUX);

  case Region::FastRAM:
    if (offset >= LANGUAGE_CARD_BASE && (bank == 0x00 || bank == 0x01) &&
        ioAndLanguageCardVisible()) {
      // Banks $00 and $01 share the Mega II's language card, which is what
      // makes //e software work unchanged on the fast side.
      if ((stateRegister() & STATE_RDROM) != 0) {
        return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
      }
      return megaII_->read(offset);
    }
    return fastRam_[static_cast<size_t>(bank) * BANK_SIZE + offset];

  case Region::ROM:
    return readROM(address);

  case Region::Unpopulated:
    // A bank with nothing in it. A real machine floats; zero is the honest
    // answer until there is a video scanner whose bus could be borrowed.
    return 0x00;
  }
  return 0x00;
}

void IIgsMemory::write(uint32_t address, uint8_t value) {
  const uint8_t bank = static_cast<uint8_t>(address >> 16);
  const uint16_t offset = static_cast<uint16_t>(address);

  const Region region = regionFor(address, bank, offset);
  if (region == Region::IO || region == Region::MegaII) slowCycles_++;

  switch (region) {
  case Region::IO:
    writeIO(offset, value);
    return;

  case Region::MegaII:
    if (offset >= LANGUAGE_CARD_BASE) {
      megaII_->write(offset, value); // The language card decides read or write
      return;
    }
    megaII_->writeRAM(offset, value, bank == SLOW_BANK_AUX);
    return;

  case Region::FastRAM:
    if (offset >= LANGUAGE_CARD_BASE && (bank == 0x00 || bank == 0x01) &&
        ioAndLanguageCardVisible()) {
      megaII_->write(offset, value);
      return;
    }
    fastRam_[static_cast<size_t>(bank) * BANK_SIZE + offset] = value;
    // ...and then again on the other side of the machine, if anything is
    // watching that address.
    shadowWrite(bank, offset, value);
    return;

  case Region::ROM:
  case Region::Unpopulated:
    return; // Writing to ROM is a program's mistake, not the bus's
  }
}

uint8_t IIgsMemory::peek(uint32_t address) const {
  const uint8_t bank = static_cast<uint8_t>(address >> 16);
  const uint16_t offset = static_cast<uint16_t>(address);

  switch (regionFor(address, bank, offset)) {
  case Region::IO:
    return peekIO(offset);
  case Region::MegaII:
    if (offset >= LANGUAGE_CARD_BASE) {
      if ((stateRegister() & STATE_RDROM) != 0) {
        return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
      }
      return megaII_->peek(offset);
    }
    return megaII_->readRAM(offset, bank == SLOW_BANK_AUX);
  case Region::FastRAM:
    if (offset >= LANGUAGE_CARD_BASE && (bank == 0x00 || bank == 0x01) &&
        ioAndLanguageCardVisible()) {
      if ((stateRegister() & STATE_RDROM) != 0) {
        return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
      }
      return megaII_->peek(offset);
    }
    return fastRam_[static_cast<size_t>(bank) * BANK_SIZE + offset];
  case Region::ROM:
    return readROM(address);
  case Region::Unpopulated:
    return 0x00;
  }
  return 0x00;
}

// ============================================================================
// I/O
//
// Three of these addresses are the IIgs's own and are taken here; the rest are
// the //e's and go to the Mega II, which is the machine that has them.
// ============================================================================

uint8_t IIgsMemory::readIO(uint16_t offset) {
  switch (offset) {
  case REG_ADB_MOUSE:
    return adb_.readMouseData();
  case REG_ADB_MODIFIERS:
    return adb_.readModifiers();
  case REG_ADB_DATA:
    return adb_.readData();
  case REG_ADB_STATUS:
    return adb_.readStatus();
  case REG_SOUND_CONTROL:
    return sound_.readControl();
  case REG_SOUND_DATA:
    return sound_.readData();
  case REG_SOUND_ADDRESS_LOW:
    return sound_.readAddressLow();
  case REG_SOUND_ADDRESS_HIGH:
    return sound_.readAddressHigh();
  case REG_SLOT_SELECT:
    return slotSelect_;
  case REG_DISK_SELECT:
    return diskSelect_;
  case REG_CLOCK_DATA:
    return clock_.readData();
  case REG_CLOCK_CONTROL:
    return clock_.readControl();
  case REG_NEW_VIDEO:
    return newVideo_;
  case REG_SHADOW:
    return shadow_;
  case REG_SPEED:
    return speed_;
  case REG_STATE:
    return stateRegister();
  default:
    break;
  }

  // $C100-$CFFF is the machine's own firmware. On a //e this is where a card's
  // ROM would answer; a IIgs has the same seven slots and a Control Panel
  // setting for each, and until that is modelled every one of them is
  // internal, which is how a machine with nothing fitted behaves anyway.
  if (offset >= 0xC100) {
    return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
  }

  return megaII_->read(offset);
}

void IIgsMemory::writeIO(uint16_t offset, uint8_t value) {
  switch (offset) {
  case REG_ADB_DATA:
    adb_.writeCommand(value);
    return;
  case REG_ADB_MOUSE:
  case REG_ADB_MODIFIERS:
  case REG_ADB_STATUS:
    return; // Read-only as far as the controller is concerned
  case REG_SOUND_CONTROL:
    sound_.writeControl(value);
    return;
  case REG_SOUND_DATA:
    sound_.writeData(value);
    return;
  case REG_SOUND_ADDRESS_LOW:
    sound_.writeAddressLow(value);
    return;
  case REG_SOUND_ADDRESS_HIGH:
    sound_.writeAddressHigh(value);
    return;
  case REG_SLOT_SELECT:
    slotSelect_ = value;
    return;
  case REG_DISK_SELECT:
    diskSelect_ = value;
    return;
  case REG_CLOCK_DATA:
    clock_.writeData(value);
    return;
  case REG_CLOCK_CONTROL:
    clock_.writeControl(value);
    return;
  case REG_NEW_VIDEO:
    newVideo_ = value;
    return;
  case REG_SHADOW:
    shadow_ = value;
    return;
  case REG_SPEED:
    speed_ = value;
    return;
  case REG_STATE:
    setStateRegister(value);
    return;
  default:
    break;
  }

  if (offset >= 0xC100) return; // Firmware space: nothing to write to
  megaII_->write(offset, value);
}

uint8_t IIgsMemory::peekIO(uint16_t offset) const {
  switch (offset) {
  // The controller's queues are consumed by reading them, so a debugger is
  // shown only what it can look at without taking anything: the modifiers and
  // the status.
  case REG_ADB_MOUSE:
  case REG_ADB_DATA:
    return 0x00;
  case REG_ADB_MODIFIERS:
    return adb_.readModifiers();
  case REG_ADB_STATUS:
    return adb_.readStatus();
  case REG_SOUND_CONTROL:
    return sound_.readControl();
  case REG_SOUND_ADDRESS_LOW:
    return sound_.readAddressLow();
  case REG_SOUND_ADDRESS_HIGH:
    return sound_.readAddressHigh();
  case REG_CLOCK_DATA:
    return clock_.readData();
  case REG_CLOCK_CONTROL:
    return clock_.readControl();
  case REG_NEW_VIDEO:
    return newVideo_;
  case REG_SHADOW:
    return shadow_;
  case REG_SPEED:
    return speed_;
  case REG_STATE:
    return stateRegister();
  default:
    break;
  }
  if (offset >= 0xC100) {
    return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
  }
  return megaII_->peek(offset);
}

// ============================================================================
// ROM
// ============================================================================

uint8_t IIgsMemory::readROM(uint32_t address) const {
  if (!rom_ || romSize_ == 0) return 0x00;

  // The ROM sits at the top of the address space: a 128KB ROM 01 fills banks
  // $FE-$FF, a 256KB ROM 3 fills $FC-$FF. So an address is an offset back from
  // $1000000, and a bank below where the image starts has nothing in it.
  const uint32_t top = 0x1000000;
  const uint32_t base = top - static_cast<uint32_t>(romSize_);
  if (address < base) return 0x00;

  uint32_t index = address - base;
  if (romHighBankFirst_) {
    // The banks are in the image the other way round, so the bank an address
    // is in is counted from the other end.
    const uint32_t bankIndex = index / BANK_SIZE;
    const uint32_t bankCount = static_cast<uint32_t>(romSize_ / BANK_SIZE);
    index = (bankCount - 1 - bankIndex) * BANK_SIZE + (index % BANK_SIZE);
  }
  return rom_[index];
}

// ============================================================================
// The state register
//
// Eight of the //e's soft switches in one byte. The switches themselves stay
// where they are — in the Mega II, which is the machine that has them — and
// this reads and writes them through their own addresses so that everything
// depending on them, the video included, sees the change the usual way.
// ============================================================================

uint8_t IIgsMemory::stateRegister() const {
  const SoftSwitches &sw = megaII_->getSoftSwitches();
  uint8_t value = 0;
  if (sw.altzp) value |= STATE_ALTZP;
  if (sw.page2) value |= STATE_PAGE2;
  if (sw.ramrd) value |= STATE_RAMRD;
  if (sw.ramwrt) value |= STATE_RAMWRT;
  if (!sw.lcram) value |= STATE_RDROM; // Reading ROM is not reading the card
  if (sw.lcram2) value |= STATE_LCBANK2;
  if (sw.intcxrom) value |= STATE_INTCXROM;
  // ROMBANK selects between the two halves of a ROM 3's firmware and means
  // nothing on a ROM 01, which is the machine being modelled.
  return value;
}

void IIgsMemory::setStateRegister(uint8_t value) {
  megaII_->write((value & STATE_ALTZP) ? 0xC009 : 0xC008, 0);
  megaII_->write((value & STATE_PAGE2) ? 0xC055 : 0xC054, 0);
  megaII_->write((value & STATE_RAMRD) ? 0xC003 : 0xC002, 0);
  megaII_->write((value & STATE_RAMWRT) ? 0xC005 : 0xC004, 0);
  megaII_->write((value & STATE_INTCXROM) ? 0xC007 : 0xC006, 0);

  // The language card is the awkward one. $C080-$C08F is a two-bit switch
  // wearing sixteen addresses, and which address to touch depends on three
  // things: the bank, whether reads come from RAM or ROM, and whether writes
  // are enabled. The state register carries the first two and says nothing
  // about the third, so the third is read back off the machine and preserved —
  // setting the memory map should not quietly write-protect the language card.
  //
  //   +0  read RAM, write protected      +8 the same, bank one
  //   +1  read ROM, write enabled
  //   +2  read ROM, write protected
  //   +3  read RAM, write enabled
  const bool bank2 = (value & STATE_LCBANK2) != 0;
  const bool readRom = (value & STATE_RDROM) != 0;
  const bool writeEnabled = megaII_->getSoftSwitches().lcwrite;
  const uint16_t bankBase = bank2 ? 0xC080 : 0xC088;
  const uint16_t select = readRom ? (writeEnabled ? 0x01 : 0x02)
                                  : (writeEnabled ? 0x03 : 0x00);
  const uint16_t lcAddress = static_cast<uint16_t>(bankBase + select);
  // Enabling writes takes two reads of the same address; doing it twice when
  // it was not asked for is harmless, since the second read of a write-protect
  // address protects it again just as firmly.
  megaII_->read(lcAddress);
  megaII_->read(lcAddress);
}

// ============================================================================
// Shadowing
// ============================================================================

bool IIgsMemory::isShadowed(uint8_t bank, uint16_t offset) const {
  if (bank != 0x00 && bank != 0x01) return false;

  // Super Hi-Res is bank $01 only, and covers everything the other regions do,
  // so it is asked first.
  if (bank == 0x01 && offset >= SHR_BASE && offset < SHR_END &&
      (shadow_ & SHADOW_SUPER_HIRES) == 0) {
    return true;
  }

  if (offset >= TEXT_PAGE1_BASE && offset < TEXT_PAGE1_END) {
    return (shadow_ & SHADOW_TEXT_PAGE1) == 0;
  }
  if (offset >= HIRES_PAGE1_BASE && offset < HIRES_PAGE1_END) {
    if (bank == 0x01 && (shadow_ & SHADOW_AUX_HIRES) != 0) return false;
    return (shadow_ & SHADOW_HIRES_PAGE1) == 0;
  }
  if (offset >= HIRES_PAGE2_BASE && offset < HIRES_PAGE2_END) {
    if (bank == 0x01 && (shadow_ & SHADOW_AUX_HIRES) != 0) return false;
    return (shadow_ & SHADOW_HIRES_PAGE2) == 0;
  }
  return false;
}

void IIgsMemory::shadowWrite(uint8_t bank, uint16_t offset, uint8_t value) {
  if (!isShadowed(bank, offset)) return;
  // Bank $00 shadows into $E0 and bank $01 into $E1: main to main, auxiliary
  // to auxiliary.
  megaII_->writeRAM(offset, value, bank == 0x01);
}

} // namespace a2e::iigs
