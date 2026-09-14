/*
 * iigs_memory.cpp - An Apple IIgs's address space
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_memory.hpp"

#include "../cards/expansion_card.hpp"
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
constexpr uint16_t REG_TEXT_COLOUR = 0xC022;
constexpr uint16_t REG_VGC_INTERRUPT = 0xC023;
constexpr uint16_t REG_VGC_INTERRUPT_CLEAR = 0xC032;
constexpr uint16_t REG_INTERRUPT_ENABLE = 0xC041;
constexpr uint16_t REG_INTERRUPT_STATUS = 0xC046;
constexpr uint16_t REG_INTERRUPT_CLEAR = 0xC047;
constexpr uint16_t REG_SCC_COMMAND_B = 0xC038;
constexpr uint16_t REG_SCC_COMMAND_A = 0xC039;
constexpr uint16_t REG_SCC_DATA_B = 0xC03A;
constexpr uint16_t REG_SCC_DATA_A = 0xC03B;
constexpr uint16_t REG_SHADOW = 0xC035;
constexpr uint16_t REG_SPEED = 0xC036;
constexpr uint16_t REG_STATE = 0xC068;
constexpr uint16_t REG_ADB_MOUSE = 0xC024;
constexpr uint16_t REG_ADB_MODIFIERS = 0xC025;
constexpr uint16_t REG_ADB_DATA = 0xC026;
constexpr uint16_t REG_ADB_STATUS = 0xC027;
constexpr uint16_t REG_VERTICAL_COUNT = 0xC02E;
constexpr uint16_t REG_HORIZONTAL_COUNT = 0xC02F;
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
  interruptEnable_ = 0;
  vgcInterrupt_ = 0;
  scc_.reset();
  vblPending_ = quarterSecondPending_ = oneSecondPending_ = false;
  scanLinePending_ = false;
  lastQuarterSecond_ = lastSecond_ = 0;
  setNewVideoRegister(0);
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
  if (region == Region::IO || region == Region::MegaII) slowAccess();

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
      // The bank names the half of the language card, exactly as it names the
      // half of the RAM below it. Going through the //e's map here would ask
      // ALTZP instead, and then $E0 and $E1 would be the same 48K — with the
      // toolbox and GS/OS living in $E1's.
      return megaII_->readLanguageCardRAM(offset, bank == SLOW_BANK_AUX);
    }
    return megaII_->readRAM(offset, bank == SLOW_BANK_AUX);

  case Region::FastRAM: {
    const uint8_t at = effectiveBank(bank, offset, false);
    if (offset >= LANGUAGE_CARD_BASE && (at == 0x00 || at == 0x01) &&
        ioAndLanguageCardVisible()) {
      // The language card a //e program finds in these banks is made of the
      // bank's own RAM — see fastLanguageCardAddress — and reads ROM when the
      // switches say so, exactly as a //e's does.
      if ((stateRegister() & STATE_RDROM) != 0) {
        return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
      }
      return fastRam_[fastLanguageCardAddress(at, offset)];
    }
    return fastRam_[static_cast<size_t>(at) * BANK_SIZE + offset];
  }

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
  if (region == Region::IO || region == Region::MegaII) slowAccess();

  switch (region) {
  case Region::IO:
    writeIO(offset, value);
    return;

  case Region::MegaII:
    if (offset >= LANGUAGE_CARD_BASE) {
      // The card's write enable still decides whether this lands; which of its
      // two halves it lands in is the bank's business, not ALTZP's.
      megaII_->writeLanguageCardRAM(offset, value, bank == SLOW_BANK_AUX);
      return;
    }
    megaII_->writeRAM(offset, value, bank == SLOW_BANK_AUX);
    return;

  case Region::FastRAM: {
    const uint8_t at = effectiveBank(bank, offset, true);
    if (offset >= LANGUAGE_CARD_BASE && (at == 0x00 || at == 0x01) &&
        ioAndLanguageCardVisible()) {
      // The card's write enable is the //e's switch, and it still decides.
      if (!megaII_->getSoftSwitches().lcwrite) return;
      fastRam_[fastLanguageCardAddress(at, offset)] = value;
      return;
    }
    fastRam_[static_cast<size_t>(at) * BANK_SIZE + offset] = value;
    // ...and then again on the other side of the machine, if anything is
    // watching that address — the side being the bank the write landed in.
    shadowWrite(at, offset, value);
    return;
  }

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
      // As in read(): the bank names the half, so a debugger looking at $E1
      // sees $E1 rather than whichever half ALTZP happens to point at.
      return megaII_->readLanguageCardRAM(offset, bank == SLOW_BANK_AUX);
    }
    return megaII_->readRAM(offset, bank == SLOW_BANK_AUX);
  case Region::FastRAM: {
    const uint8_t at = effectiveBank(bank, offset, false);
    if (offset >= LANGUAGE_CARD_BASE && (at == 0x00 || at == 0x01) &&
        ioAndLanguageCardVisible()) {
      if ((stateRegister() & STATE_RDROM) != 0) {
        return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
      }
      return fastRam_[fastLanguageCardAddress(at, offset)];
    }
    return fastRam_[static_cast<size_t>(at) * BANK_SIZE + offset];
  }
  case Region::ROM:
    return readROM(address);
  case Region::Unpopulated:
    return 0x00;
  }
  return 0x00;
}

uint8_t IIgsMemory::effectiveBank(uint8_t bank, uint16_t offset,
                                  bool write) const {
  if (bank != 0x00) return bank;
  const SoftSwitches &sw = megaII_->getSoftSwitches();
  const uint8_t page = static_cast<uint8_t>(offset >> 8);

  // 80STORE hands the text page, and with HIRES the first hi-res page, to
  // PAGE2 — and takes them away from RAMRD and RAMWRT, whatever those say.
  if (sw.store80 && page >= 0x04 && page <= 0x07) return sw.page2 ? 0x01 : 0x00;
  if (sw.store80 && sw.hires && page >= 0x20 && page <= 0x3F) {
    return sw.page2 ? 0x01 : 0x00;
  }
  // Zero page, the stack and everything from $C000 up follow ALTZP.
  if (page <= 0x01 || page >= 0xC0) return sw.altzp ? 0x01 : 0x00;
  // The rest is RAMRD's on a read and RAMWRT's on a write.
  return (write ? sw.ramwrt : sw.ramrd) ? 0x01 : 0x00;
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
  case REG_VERTICAL_COUNT:
  case REG_HORIZONTAL_COUNT:
    // Reading either clears the scan-line interrupt, as GSSquared has it.
    scanLinePending_ = false;
    return offset == REG_VERTICAL_COUNT ? verticalCountRegister()
                                        : horizontalCountRegister();
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
    return clockControlRegister();
  case REG_TEXT_COLOUR:
    return textColour_;
  case REG_VGC_INTERRUPT:
    return vgcInterruptRegister();
  case REG_INTERRUPT_ENABLE:
    return interruptEnable_;
  case REG_INTERRUPT_STATUS:
    return interruptStatusRegister();
  case REG_SCC_COMMAND_B:
  case REG_SCC_COMMAND_A:
  case REG_SCC_DATA_B:
  case REG_SCC_DATA_A:
    return scc_.read(static_cast<uint8_t>(offset - REG_SCC_COMMAND_B));
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

  // $C100-$CFFF is either the machine's own firmware or a card's, and $C02D
  // is what chooses — which is the Control Panel's "Your Card" setting in a
  // register. A IIgs comes up with slots 1 to 6 internal and slot 7 expecting
  // a card, and the firmware writes exactly that.
  if (offset >= 0xC100) {
    if (ExpansionCard *card = cardForSlotRom(offset)) {
      return card->readROM(static_cast<uint8_t>(offset & 0xFF));
    }
    // A slot the Control Panel has given to "Your Card" with nothing in the
    // socket answers the way an empty slot does on any Apple II: with the
    // bus. Showing the internal firmware there instead would defeat the
    // setting — ProDOS 8 2.4.1 finds the AppleTalk firmware's "ATLK" in slot
    // 7 and calls into it, which ends in a BRK on a ROM 01, and the known
    // way round that is to set slot 7 to Your Card.
    if (slotIsExternalAndEmpty(offset)) return 0xFF; // nothing driving the bus
    return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
  }

  // $C071-$C07F is firmware, not I/O. It is where the machine's IRQ and BRK
  // vectors point — $C071 for BRK, $C074 for IRQ — and it holds a few bytes
  // of 8-bit code whose whole job is to reach the 16-bit interrupt manager:
  // set V or not to say which it was, then JML into bank $E1. A //e has
  // nothing here and reads the bus; a IIgs that did the same would take
  // every interrupt and every BRK straight into a page of zeros, and sit
  // executing BRK after BRK on the spot where its handler should be.
  if (offset > 0xC070 && offset < 0xC080) {
    return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
  }

  return megaII_->read(offset);
}

void IIgsMemory::writeIO(uint16_t offset, uint8_t value) {
  switch (offset) {
  case REG_ADB_DATA:
    adb_.writeCommand(value);
    return;
  case REG_ADB_STATUS:
    adb_.writeStatus(value);
    return;
  case REG_ADB_MOUSE:
  case REG_ADB_MODIFIERS:
    return; // Read-only as far as the controller is concerned
  case REG_VGC_INTERRUPT:
    vgcInterrupt_ = static_cast<uint8_t>(
        value & (VGC_ONE_SECOND_ENABLE | VGC_SCANLINE_ENABLE));
    return;
  case REG_VGC_INTERRUPT_CLEAR:
    // A clear bit clears: writing $C032 with bit 5 low acknowledges the
    // scan-line match, and with bit 6 low the one-second tick — the same
    // positions the flags have in $C023. QuickDraw II's scan-line handler
    // writes $DF, and so does the ROM's.
    if ((value & VGC_ONE_SECOND_PENDING) == 0) oneSecondPending_ = false;
    if ((value & VGC_SCANLINE_PENDING) == 0) scanLinePending_ = false;
    return;
  case REG_INTERRUPT_ENABLE:
    interruptEnable_ = value;
    return;
  case REG_INTERRUPT_CLEAR:
    vblPending_ = false;
    quarterSecondPending_ = false;
    return;
  case REG_SCC_COMMAND_B:
  case REG_SCC_COMMAND_A:
  case REG_SCC_DATA_B:
  case REG_SCC_DATA_A:
    scc_.write(static_cast<uint8_t>(offset - REG_SCC_COMMAND_B), value);
    return;
  case REG_SOUND_CONTROL: {
    const uint8_t before = sound_.volume();
    sound_.writeControl(value);
    if (sound_.volume() != before && volumeChanged_) {
      volumeChanged_(sound_.volume(), slowCycles_);
    }
    return;
  }
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
    // Two registers share this address. The top nibble is the clock's; the
    // bottom four bits are the border colour, which is why the firmware can
    // write $06 here and mean "medium blue" without starting a transaction.
    border_ = static_cast<uint8_t>(value & BORDER_MASK);
    clock_.writeControl(value);
    return;
  case REG_TEXT_COLOUR:
    setTextColourRegister(value);
    return;
  case REG_NEW_VIDEO:
    setNewVideoRegister(value);
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
    return adb_.peekModifiers();
  case REG_ADB_STATUS:
    return adb_.readStatus();
  case REG_SOUND_CONTROL:
    return sound_.readControl();
  case REG_SOUND_ADDRESS_LOW:
    return sound_.readAddressLow();
  case REG_SOUND_ADDRESS_HIGH:
    return sound_.readAddressHigh();
  case REG_VERTICAL_COUNT:
    return verticalCountRegister();
  case REG_HORIZONTAL_COUNT:
    return horizontalCountRegister();
  case REG_CLOCK_DATA:
    return clock_.readData();
  case REG_CLOCK_CONTROL:
    return clockControlRegister();
  case REG_TEXT_COLOUR:
    return textColour_;
  case REG_VGC_INTERRUPT:
    return vgcInterruptRegister();
  case REG_INTERRUPT_ENABLE:
    return interruptEnable_;
  case REG_INTERRUPT_STATUS:
    return interruptStatusRegister();
  case REG_SCC_COMMAND_B:
  case REG_SCC_COMMAND_A:
  case REG_SCC_DATA_B:
  case REG_SCC_DATA_A:
    return scc_.peek(static_cast<uint8_t>(offset - REG_SCC_COMMAND_B));
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
    if (ExpansionCard *card = cardForSlotRom(offset)) {
      return card->peekROM(static_cast<uint8_t>(offset & 0xFF));
    }
    if (slotIsExternalAndEmpty(offset)) return 0xFF; // the bus, without disturbing it
    return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
  }
  // The vectors' firmware, as in readIO — so a debugger looking at $C074
  // sees what the processor will execute there.
  if (offset > 0xC070 && offset < 0xC080) {
    return readROM((static_cast<uint32_t>(ROM_TOP_BANK) << 16) | offset);
  }
  return megaII_->peek(offset);
}

// ============================================================================
// ROM
// ============================================================================

ExpansionCard *IIgsMemory::cardForSlotRom(uint16_t offset) const {
  // $C100-$C7FF is a slot's own 256 bytes; above that is the expansion ROM
  // space, which is not modelled here yet.
  if (offset < 0xC100 || offset >= 0xC800) return nullptr;

  const uint8_t slot = static_cast<uint8_t>((offset >> 8) & 0x07);
  if (slot < 1 || slot > 7) return nullptr;

  ExpansionCard *card = megaII_->getCard(slot);
  if (!card || !card->hasROM()) return nullptr;

  // A slot the machine fitted itself answers whatever $C02D says, because it
  // is not a card in a socket — it is the machine's firmware for that slot,
  // the way the disk port in slot 6 is. Anything else answers only when the
  // Control Panel has been set to "Your Card".
  if (slot == internalCardSlot_) return card;
  return (slotSelect_ & (1u << slot)) ? card : nullptr;
}

bool IIgsMemory::slotIsExternalAndEmpty(uint16_t offset) const {
  if (offset < 0xC100 || offset >= 0xC800) return false;
  const uint8_t slot = static_cast<uint8_t>((offset >> 8) & 0x07);
  if (slot < 1 || slot > 7 || slot == internalCardSlot_) return false;
  if ((slotSelect_ & (1u << slot)) == 0) return false; // internal firmware
  ExpansionCard *card = megaII_->getCard(slot);
  return !card || !card->hasROM();
}

namespace {
uint16_t verticalCounter(int line) {
  if (line < 192) return static_cast<uint16_t>(0x100 + line);
  if (line < 256) return static_cast<uint16_t>(0x1C0 + (line - 192));
  return static_cast<uint16_t>(0xFA + (line - 256));
}
uint8_t horizontalCounter(int column) {
  return column == 0 ? 0 : static_cast<uint8_t>(0x40 + column - 1);
}
} // namespace

uint8_t IIgsMemory::verticalCountRegister() const {
  const Beam beam = beamQuery_ ? beamQuery_() : Beam{0, 0};
  return static_cast<uint8_t>(verticalCounter(beam.line) >> 1);
}

uint8_t IIgsMemory::horizontalCountRegister() const {
  const Beam beam = beamQuery_ ? beamQuery_() : Beam{0, 0};
  return static_cast<uint8_t>(((verticalCounter(beam.line) & 1) << 7) |
                              horizontalCounter(beam.column));
}

uint8_t IIgsMemory::vgcInterruptRegister() const {
  uint8_t value = vgcInterrupt_;
  if (oneSecondPending_ && (vgcInterrupt_ & VGC_ONE_SECOND_ENABLE)) {
    value |= VGC_ONE_SECOND_PENDING | VGC_ANY_PENDING;
  }
  if (scanLinePending_ && (vgcInterrupt_ & VGC_SCANLINE_ENABLE)) {
    value |= VGC_SCANLINE_PENDING | VGC_ANY_PENDING;
  }
  return value;
}

uint8_t IIgsMemory::interruptStatusRegister() const {
  // $C046 INTFLAG: the flags say what has happened, whether or not it was
  // allowed to interrupt — a handler that switches its source off before
  // looking (the diagnostic's does) must still find the flag — and only a
  // write to $C047 clears them. Bit 7 says whether anything is holding the
  // line down right now, which is enable and flag together, across every
  // source. GSSquared reads the same.
  uint8_t value = 0;
  if (vblPending_) value |= INT_VBL;
  if (quarterSecondPending_) value |= INT_QUARTER_SECOND;
  if (interruptPending()) value |= INT_STATUS_ANY;
  return value;
}

bool IIgsMemory::interruptPending() const {
  if (adb_.interruptPending()) return true;
  if (sound_.interruptPending()) return true;
  if (scc_.interruptPending()) return true;
  if (vgcInterruptRegister() & VGC_ANY_PENDING) return true;
  if (vblPending_ && (interruptEnable_ & INT_VBL)) return true;
  if (quarterSecondPending_ && (interruptEnable_ & INT_QUARTER_SECOND)) return true;
  return false;
}

void IIgsMemory::tickClocks() {
  // Both ticks are counted in the Mega II's clock, which is what the video
  // and the drive already run on. A quarter of a second is a quarter of a
  // second whatever speed the processor is set to.
  constexpr uint64_t QUARTER_SECOND = 1023000 / 4;
  constexpr uint64_t SECOND = 1023000;
  if (slowCycles_ - lastQuarterSecond_ >= QUARTER_SECOND) {
    lastQuarterSecond_ = slowCycles_;
    quarterSecondPending_ = true;
  }
  if (slowCycles_ - lastSecond_ >= SECOND) {
    lastSecond_ = slowCycles_;
    oneSecondPending_ = true;
    clock_.tick(); // the interrupt is the clock chip's second
  }
}

uint32_t IIgsMemory::fastLanguageCardAddress(uint8_t bank,
                                             uint16_t offset) const {
  const SoftSwitches &sw = megaII_->getSoftSwitches();
  const uint8_t which = (bank == 0x00 && sw.altzp) ? 0x01 : bank;
  // Bank 2 of the card is the bank's own $D000-$DFFF; bank 1 is the 4K under
  // the I/O space. $E000-$FFFF is not switched.
  uint16_t at = offset;
  if (offset < 0xE000 && !sw.lcram2) at = static_cast<uint16_t>(offset - 0x1000);
  return (static_cast<uint32_t>(which) << 16) | at;
}

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
  if (offset >= HIRES_PAGE1_BASE && offset < HIRES_PAGE2_END) {
    // Bank $01's hi-res pages answer to their own bit, and — asked above —
    // to Super Hi-Res, which covers the same addresses: they are shadowed
    // unless both say not to. The two per-page bits are bank $00's.
    if (bank == 0x01) return (shadow_ & SHADOW_AUX_HIRES) == 0;
    if (offset < HIRES_PAGE1_END) return (shadow_ & SHADOW_HIRES_PAGE1) == 0;
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


void IIgsMemory::serialize(StateWriter &w) const {
  w.blob(fastRam_);

  w.bytes(megaII_->getMainRAM(), MAIN_RAM_SIZE);
  w.bytes(megaII_->getAuxRAM(), AUX_RAM_SIZE);
  for (bool aux : {false, true}) {
    w.bytes(megaII_->getLCBank1(aux), 0x1000);
    w.bytes(megaII_->getLCBank2(aux), 0x1000);
    w.bytes(megaII_->getLCHighRAM(aux), 0x2000);
  }
  w.u32(megaII_->packSwitchesForState());

  w.u64(slowCycles_);
  w.u64(slowAccesses_);
  w.f64(remainder_);
  w.u8(shadow_);
  w.u8(speed_);
  w.u8(newVideo_);
  w.u8(slotSelect_);
  w.u8(diskSelect_);
  w.u8(textColour_);
  w.u8(border_);
  w.u8(interruptEnable_);
  w.u8(vgcInterrupt_);
  w.boolean(vblPending_);
  w.boolean(scanLinePending_);
  w.boolean(quarterSecondPending_);
  w.boolean(oneSecondPending_);
  w.u64(lastQuarterSecond_);
  w.u64(lastSecond_);

  adb_.serialize(w);
  clock_.serialize(w);
  scc_.serialize(w);
  sound_.serialize(w);
}

bool IIgsMemory::deserialize(StateReader &r) {
  size_t fastSize = 0;
  const uint8_t *fast = r.blob(fastSize);
  if (!fast || fastSize != fastRam_.size()) return false;
  std::copy(fast, fast + fastSize, fastRam_.begin());

  if (const uint8_t *main = r.bytes(MAIN_RAM_SIZE)) {
    for (uint32_t a = 0; a < MAIN_RAM_SIZE; a++)
      megaII_->writeRAM(static_cast<uint16_t>(a), main[a], false);
  }
  if (const uint8_t *aux = r.bytes(AUX_RAM_SIZE)) {
    for (uint32_t a = 0; a < AUX_RAM_SIZE; a++)
      megaII_->writeRAM(static_cast<uint16_t>(a), aux[a], true);
  }
  for (bool aux : {false, true}) {
    const uint8_t *b1 = r.bytes(0x1000);
    const uint8_t *b2 = r.bytes(0x1000);
    const uint8_t *hi = r.bytes(0x2000);
    if (!hi) return false;
    megaII_->setLCBank1(b1, aux);
    megaII_->setLCBank2(b2, aux);
    megaII_->setLCHighRAM(hi, aux);
  }
  // The Mega II's switches go through the Mega II, not through this class's
  // I/O decode: they are //e switches and the MMU knows what each one moves.
  megaII_->restoreSwitchesFromState(r.u32());

  slowCycles_ = r.u64();
  slowAccesses_ = r.u64();
  remainder_ = r.f64();
  shadow_ = r.u8();
  speed_ = r.u8();
  setNewVideoRegister(r.u8());
  slotSelect_ = r.u8();
  diskSelect_ = r.u8();
  setTextColourRegister(r.u8()); // through the setter, so the video hears it
  border_ = r.u8();
  interruptEnable_ = r.u8();
  vgcInterrupt_ = r.u8();
  vblPending_ = r.boolean();
  scanLinePending_ = r.boolean();
  quarterSecondPending_ = r.boolean();
  oneSecondPending_ = r.boolean();
  lastQuarterSecond_ = r.u64();
  lastSecond_ = r.u64();

  adb_.deserialize(r);
  clock_.deserialize(r);
  scc_.deserialize(r);
  sound_.deserialize(r);
  return r.ok();
}

} // namespace a2e::iigs
