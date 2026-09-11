/*
 * iigs_memory.hpp - An Apple IIgs's address space
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../machine/machine_profile.hpp"
#include "iigs_adb.hpp"
#include "iigs_clock.hpp"
#include "iigs_sound.hpp"
#include "iigs_spec.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace a2e {
class MMU;
}

namespace a2e::iigs {

/**
 * IIgsMemory - the two halves of a IIgs, and the map between them
 *
 * A IIgs is two computers sharing an address space. The **FPI** side is the
 * 65816's: sixteen megabytes of banks, of which $00 upward are fast RAM and
 * $F0 upward are ROM. The **Mega II** side is an entire //e living in banks
 * $E0 and $E1 — the same 128KB, the same soft switches, the same language card,
 * the same I/O — running at the slow clock.
 *
 * So the Mega II side is not reimplemented here. It is an `MMU`, the same class
 * a //e is built from, and this class is the map around it: which bank an
 * address lands in, what answers there, and what the IIgs's own registers do to
 * all of that. When the video comes to be written it will read the Mega II's
 * memory through that MMU exactly as a //e's video does, because it is the same
 * chip looking at the same RAM.
 *
 * **Shadowing is what keeps the two sides in step.** A program running in fast
 * RAM writes to bank $00 or $01; the video only ever looks at $E0 and $E1. So
 * writes to the display regions of $00 and $01 are copied across as they
 * happen, and which regions those are is a register ($C035) rather than a
 * constant. Without it a IIgs program drawing at full speed would draw into
 * memory nothing was looking at.
 *
 * What is not here yet: cards in slots (a IIgs's $C100-$CFFF is its own
 * firmware until a slot is switched to "Your Card"), the Ensoniq's RAM window,
 * and battery RAM. Each is named where it would go.
 */
class IIgsMemory {
public:
  /**
   * @param fastRamSize  How much RAM is fitted on the FPI side. A ROM 01
   *                     motherboard came with 256KB; a card takes it to 8MB.
   */
  explicit IIgsMemory(size_t fastRamSize = FAST_RAM_SIZE_ROM01);
  ~IIgsMemory();

  IIgsMemory(const IIgsMemory &) = delete;
  IIgsMemory &operator=(const IIgsMemory &) = delete;

  /**
   * The system ROM: 128KB of it on a ROM 01, in banks $FE-$FF.
   *
   * Which half of the image is which bank is worked out here rather than
   * assumed — see the note on `romHighBankFirst_`.
   */
  void loadROM(const uint8_t *rom, size_t size);
  bool hasROM() const { return romSize_ > 0; }

  void reset();

  // ===== The bus =====

  uint8_t read(uint32_t address);
  void write(uint32_t address, uint8_t value);

  /** As the debugger sees it: no soft switch is touched by looking. */
  uint8_t peek(uint32_t address) const;

  // ===== The two sides =====

  /** The battery-backed clock and its settings, at $C033-$C034. */
  IIgsClock &clock() { return clock_; }
  const IIgsClock &clock() const { return clock_; }

  /** The keyboard and mouse controller, at $C024-$C027. */
  IIgsADB &adb() { return adb_; }
  const IIgsADB &adb() const { return adb_; }

  /** The Ensoniq's RAM and the window onto it, at $C03C-$C03F. */
  IIgsSound &sound() { return sound_; }
  const IIgsSound &sound() const { return sound_; }

  /** The Mega II: a //e, and the machine's slow side. */
  MMU &megaII() { return *megaII_; }
  const MMU &megaII() const { return *megaII_; }

  size_t fastRamSize() const { return fastRam_.size(); }

  /** Whether a bank has fast RAM in it. Unpopulated banks read as $00. */
  bool bankIsPopulated(uint8_t bank) const {
    return (static_cast<size_t>(bank) + 1) * BANK_SIZE <= fastRam_.size();
  }

  // ===== The IIgs's own registers =====
  //
  // Three registers a //e has no equivalent of, and one it has eight of.

  /** $C035 SHADOW. A set bit turns a region's shadowing *off*. */
  uint8_t shadowRegister() const { return shadow_; }
  void setShadowRegister(uint8_t value) { shadow_ = value; }

  /**
   * $C029 NEWVIDEO: bit 7 puts Super Hi-Res on the screen.
   *
   * The other bits linearise Super Hi-Res memory and turn off the Mega II's
   * bank switching, neither of which is modelled — but they are stored, since
   * the firmware writes the register with a read-modify-write and would find
   * its own bits missing afterwards.
   */
  uint8_t newVideoRegister() const { return newVideo_; }
  void setNewVideoRegister(uint8_t value) { newVideo_ = value; }
  bool superHiResEnabled() const { return (newVideo_ & NEW_VIDEO_SHR) != 0; }

  /** $C036 CYAREG: bit 7 chooses the fast clock. */
  uint8_t speedRegister() const { return speed_; }
  void setSpeedRegister(uint8_t value) { speed_ = value; }
  bool isFastSpeed() const { return (speed_ & SPEED_FAST) != 0; }

  /**
   * $C068 STATEREG: eight of the //e's soft switches in one byte, so that a
   * program can save and restore the whole memory map in two instructions
   * instead of eight. Reading it composes the switches; writing it sets them.
   */
  uint8_t stateRegister() const;
  void setStateRegister(uint8_t value);

  // Shadow register bits, as the hardware names them. Each *disables*.
  static constexpr uint8_t SHADOW_TEXT_PAGE1 = 0x01;
  static constexpr uint8_t SHADOW_HIRES_PAGE1 = 0x02;
  static constexpr uint8_t SHADOW_HIRES_PAGE2 = 0x04;
  static constexpr uint8_t SHADOW_SUPER_HIRES = 0x08;
  static constexpr uint8_t SHADOW_AUX_HIRES = 0x10;
  static constexpr uint8_t SHADOW_IO_LANGUAGE_CARD = 0x40;

  static constexpr uint8_t SPEED_FAST = 0x80;
  static constexpr uint8_t NEW_VIDEO_SHR = 0x80;

  // State register bits.
  static constexpr uint8_t STATE_ALTZP = 0x80;
  static constexpr uint8_t STATE_PAGE2 = 0x40;
  static constexpr uint8_t STATE_RAMRD = 0x20;
  static constexpr uint8_t STATE_RAMWRT = 0x10;
  static constexpr uint8_t STATE_RDROM = 0x08;
  static constexpr uint8_t STATE_LCBANK2 = 0x04;
  static constexpr uint8_t STATE_ROMBANK = 0x02;
  static constexpr uint8_t STATE_INTCXROM = 0x01;

private:
  // Where an address lands, decided once and then acted on.
  enum class Region {
    FastRAM,      // Banks $00-$7F, as far as the RAM fitted goes
    Unpopulated,  // Banks with nothing in them
    MegaII,       // Banks $E0-$E1: the //e
    ROM,          // Banks $F0-$FF
    IO,           // $C000-$CFFF, in whichever bank it is visible
  };

  Region regionFor(uint32_t address, uint8_t bank, uint16_t offset) const;

  uint8_t readIO(uint16_t offset);
  void writeIO(uint16_t offset, uint8_t value);
  uint8_t peekIO(uint16_t offset) const;

  uint8_t readROM(uint32_t address) const;

  // Whether banks $00/$01 show I/O and the language card at all, which is the
  // one shadow bit that changes what an address *is* rather than where a write
  // also goes.
  bool ioAndLanguageCardVisible() const {
    return (shadow_ & SHADOW_IO_LANGUAGE_CARD) == 0;
  }

  /**
   * Copy a write in bank $00 or $01 to the Mega II side, if that region is
   * being shadowed. Called after the write itself: shadowing is a copy, not a
   * redirection, and fast RAM holds the value either way.
   */
  void shadowWrite(uint8_t bank, uint16_t offset, uint8_t value);
  bool isShadowed(uint8_t bank, uint16_t offset) const;

  IIgsADB adb_;
  IIgsClock clock_;
  IIgsSound sound_;
  std::vector<uint8_t> fastRam_;
  std::unique_ptr<MMU> megaII_;

  const uint8_t *rom_ = nullptr;
  size_t romSize_ = 0;

  // Whether this image holds its banks the other way round.
  //
  // The obvious reading of a 128KB ROM 01 image is that it ends at $FF:FFFF,
  // so its first half is bank $FE. Some dumps — including the one this was
  // written against — are stored the other way, and the difference is not
  // subtle: the emulation reset vector lives at $FF:FFFC, and reading it out
  // of the wrong half gives zero and a machine that resets to $00:0000.
  //
  // So the image is asked rather than assumed. The half whose top holds a
  // usable reset vector is bank $FF, because that is the one thing every IIgs
  // ROM must have in the same place.
  bool romHighBankFirst_ = false;

  uint8_t shadow_ = 0;
  uint8_t speed_ = 0;
  uint8_t newVideo_ = 0;
};

} // namespace a2e::iigs
