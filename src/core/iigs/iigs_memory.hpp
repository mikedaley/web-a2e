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
#include "iigs_scc.hpp"
#include "iigs_sound.hpp"
#include "iigs_spec.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace a2e {
class ExpansionCard;
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

  // ===== The slow clock =====
  //
  // The Mega II's 1.023MHz, and the one the video and the drive are counted
  // in. It lives here rather than in the machine because this is where the
  // thing that advances it happens: a IIgs runs at 2.8MHz until it reaches
  // across to the Mega II, and *that access* is stretched to a slow cycle.
  //
  // Keeping it here is what lets it tick during an instruction rather than
  // between instructions. A disk read loop is a few cycles with one access in
  // it, and a drive whose clock only moves when an instruction ends sees that
  // loop in lumps: the sequencer runs past a completed byte before the
  // firmware can read it, which looks exactly like a drive returning garbage.
  // It is the difference between a machine that boots a disk and one that does
  // not.

  uint64_t slowCycles() const { return slowCycles_; }

  /**
   * The rest of an instruction — the cycles that did not reach the slow side —
   * converted to slow time by whoever knows which clock is selected.
   */
  void addFastCycles(double slowEquivalent) {
    remainder_ += slowEquivalent;
    const uint64_t whole = static_cast<uint64_t>(remainder_);
    remainder_ -= static_cast<double>(whole);
    slowCycles_ += whole;
  }

  /**
   * How many of this instruction's cycles went to the slow side, and clear the
   * count for the next one.
   *
   * A cycle that reaches the Mega II is a slow cycle *instead of* a fast one,
   * not as well as: the processor stops and waits for the 1.023MHz side rather
   * than doing something else meanwhile. So the caller subtracts these before
   * converting what is left, or every instruction that touches a soft switch
   * is charged for its slow cycles twice — and the boot ROM's read loop, which
   * runs out of bank $00's I/O space and so is *all* slow accesses, comes out
   * at thirteen cycles where the disk expects seven.
   */
  /**
   * A cycle that reaches the Mega II. From the fast side the processor stops
   * until the slow clock's next edge and then takes a whole slow cycle — so
   * the access costs the rest of the slow cycle in progress, and then one —
   * which is what makes a loop that polls a Mega II register slower than its
   * cycle count says. GSSquared charges the same. At slow speed every cycle
   * is already on that clock and there is nothing to wait for.
   */
  void slowAccess() {
    if (remainder_ > 0.0) {
      slowCycles_++;
      remainder_ = 0.0;
    }
    slowCycles_++;
    slowAccesses_++;
  }

  uint64_t takeSlowAccesses() {
    const uint64_t count = slowAccesses_;
    slowAccesses_ = 0;
    return count;
  }

  void resetClock() {
    slowCycles_ = 0;
    remainder_ = 0.0;
    slowAccesses_ = 0;
  }



  // ===== The two sides =====

  /** The battery-backed clock and its settings, at $C033-$C034. */
  IIgsClock &clock() { return clock_; }
  const IIgsClock &clock() const { return clock_; }

  /**
   * The Z8530 behind the two serial ports, at $C038-$C03B. Nothing is plugged
   * into either, but the chip is real: the ROM's interrupt manager asks it
   * first on every interrupt, and the Diagnostic sends bytes round its loop.
   */
  IIgsSCC &scc() { return scc_; }
  const IIgsSCC &scc() const { return scc_; }

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

  /** A byte of fast RAM as stored, whatever the map says. For tests. */
  uint8_t fastRamByte(uint32_t address) const {
    return address < fastRam_.size() ? fastRam_[address] : 0;
  }

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

  /**
   * $C02D SLOTROMSEL: which slots answer from a card and which from the
   * machine's own firmware. A IIgs has seven slots and a Control Panel setting
   * for each, and this is where that setting ends up.
   */
  uint8_t slotRegister() const { return slotSelect_; }
  void setSlotRegister(uint8_t value) { slotSelect_ = value; }

  /**
   * $C031 DISKREG: which drive the one IWM is talking to.
   *
   * A IIgs has a single disk chip and four drives to point it at — two 3.5"
   * and two 5.25" — so this register is the pointer. Bit 7 chooses 3.5" over
   * 5.25" and bit 6 chooses the second drive of the pair. The firmware polls
   * it early and often, and a machine that answers the floating bus here never
   * gets as far as looking for a disk.
   */
  uint8_t diskSelectRegister() const { return diskSelect_; }
  void setDiskSelectRegister(uint8_t value) { diskSelect_ = value; }
  bool selects35Inch() const { return (diskSelect_ & DISK_SELECT_35) != 0; }

  /**
   * $C022 TCOLOR and the bottom nibble of $C034: the colours the VGC draws the
   * Mega II's text in, and the colour of the border around it.
   *
   * These are the one part of a IIgs's picture that is neither the //e's nor
   * Super Hi-Res. A IIgs does not put //e text through a composite decoder at
   * all — the VGC substitutes two colours of its own for lit and unlit dots,
   * which is why its text is crisp in a way a //e's never is, and why the
   * Control Panel can offer sixteen of each. The firmware writes `$F6` at
   * startup: white on medium blue, which is the screen everyone remembers.
   *
   * `$C034` is two registers at one address. The top nibble is the clock's
   * transaction control; the bottom four bits are the border. Writing `$06`
   * here sets the border to medium blue and starts no transaction, which is
   * exactly what the firmware does.
   */
  /**
   * Name a slot whose card is part of the machine.
   *
   * $C02D says, for each slot, whether $Cn00 reads the machine's own firmware
   * or the ROM of a card fitted there — it is the Control Panel's "Your Card"
   * setting, in a register, and a IIgs comes up with every slot internal. A
   * part the machine *has* is on the internal side of that switch, and this is
   * how it says so: the SmartPort is the machine's, in the slot a IIgs keeps
   * it in, and it needs no setting changed before it answers.
   */
  void setInternalCardSlot(uint8_t slot) { internalCardSlot_ = slot; }

  /**
   * The machine's interrupt sources, and whether any is asking.
   *
   * A IIgs has three places an interrupt can come from that a //e has not:
   * the ADB controller (mouse, keyboard, a command's answer — see IIgsADB),
   * the VGC ($C023: a one-second tick and a scan-line match), and the Mega
   * II's own ($C041 enables vertical blanking and a quarter-second tick; $C046
   * reports them; a write to $C047 clears them). The processor samples the
   * OR of all of it every instruction.
   *
   * GS/OS enables the ADB mouse interrupt and the VGC's one-second tick on
   * the way up, and a machine without either had a Finder that drew its
   * desktop and then never noticed the mouse.
   */
  bool interruptPending() const;

  /** The frame has ended: what the Mega II calls vertical blanking. */
  void signalVerticalBlank() { vblPending_ = true; }

  /**
   * Where the beam is, for $C02E and $C02F: the scan line since the top of
   * the frame (0-261) and the cycle within it (0-64). The machine owns the
   * frame boundary, so it answers.
   */
  struct Beam { int line; int column; };
  using BeamQuery = std::function<Beam()>;
  void setBeamQuery(BeamQuery query) { beamQuery_ = std::move(query); }

  /**
   * $C02E VERTCNT and $C02F HORIZCNT, composed as the Mega II's scanner has
   * them: the vertical counter runs $100-$1BF over the picture and $1C0-$1FF
   * then $FA-$FF through blanking; the horizontal counter is 0 for the first
   * cycle of a line and $40-$7F for the rest. $C02E is the vertical counter's
   * bits 8-1, $C02F its bit 0 above the horizontal counter. The IIgs
   * Diagnostic times the processor against these to measure its speed, and
   * a machine that answered nothing failed that test at once.
   */
  uint8_t verticalCountRegister() const;
  uint8_t horizontalCountRegister() const;

  /**
   * The VGC has finished drawing a Super Hi-Res line whose control byte asks
   * for an interrupt (bit 6 of the SCB), and $C023 bit 2 lets it.
   *
   * QuickDraw II draws the mouse pointer from this interrupt, not from the
   * vertical blank: it marks the line the pointer sits on and redraws once
   * the beam is past it, from the handler it installs at $E1:0028.
   */
  void signalScanLine() { scanLinePending_ = true; }

  /** The slow clock has moved; raise whichever ticks it has crossed. */
  void tickClocks();


  uint8_t textColourRegister() const { return textColour_; }
  void setTextColourRegister(uint8_t value) {
    textColour_ = value;
    if (textColourChanged_) textColourChanged_(textColour_);
  }

  /**
   * Told when $C022 changes, because the VGC acts on it at once.
   *
   * The picture is decoded a scanline at a time as the machine runs, so a
   * colour delivered when the frame is handed over is a frame late — and a
   * program that changes the text colour partway down the screen, which is a
   * real thing to do, would not see it change until the next one.
   */
  /**
   * Told when the volume nibble in $C03C changes, with the slow-clock time,
   * so the speaker's amplifier can follow it as it happened rather than as
   * the buffer that carried it happened to end.
   */
  using VolumeCallback = std::function<void(uint8_t, uint64_t)>;
  void setVolumeCallback(VolumeCallback callback) { volumeChanged_ = std::move(callback); }

  using TextColourCallback = std::function<void(uint8_t)>;
  void setTextColourCallback(TextColourCallback callback) {
    textColourChanged_ = std::move(callback);
    if (textColourChanged_) textColourChanged_(textColour_);
  }
  uint8_t textForeground() const {
    return static_cast<uint8_t>((textColour_ >> 4) & 0x0F);
  }
  uint8_t textBackground() const {
    return static_cast<uint8_t>(textColour_ & 0x0F);
  }
  uint8_t borderColour() const { return border_; }

  /**
   * $C036 CYAREG: bit 7 chooses the fast clock, and bits 0-3 decide when the
   * machine is not allowed to use it.
   *
   * Those four bits are slot motor detect, one each for slots 4 to 7: with a
   * slot's bit set, a drive turning in that slot drops the whole machine to
   * the Mega II's 1.023MHz until the motor stops. That is not a nicety. A Disk
   * II's data register holds a finished byte for about two bit cells and then
   * the sequencer takes it apart again, so the boot ROM's read loop — thirteen
   * cycles of poll, on the machine it was written for — arrives once per byte.
   * At 2.8MHz the same loop comes round three times as often and reads bytes
   * twice, and every checksum on the disk fails.
   *
   * So the firmware sets bit 2 before it goes looking for a disk, and the
   * hardware does the rest. `setSlotMotorQuery` is how this side finds out
   * whether a slot has a motor running; without one the bits are inert, which
   * is the right answer for a machine with nothing fitted.
   */
  uint8_t speedRegister() const { return speed_; }
  void setSpeedRegister(uint8_t value) { speed_ = value; }

  using SlotMotorQuery = std::function<bool(int slot)>;
  void setSlotMotorQuery(SlotMotorQuery query) {
    slotMotorQuery_ = std::move(query);
  }

  bool isFastSpeed() const {
    if ((speed_ & SPEED_FAST) == 0) return false;
    if (!slotMotorQuery_) return true;
    for (int slot = 4; slot <= 7; slot++) {
      if ((speed_ & (1u << (slot - 4))) && slotMotorQuery_(slot)) return false;
    }
    return true;
  }

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
  static constexpr uint8_t BORDER_MASK = 0x0F;

  // $C041 INTEN and $C046 status share bit positions for the two ticks.
  static constexpr uint8_t INT_QUARTER_SECOND = 0x10;
  static constexpr uint8_t INT_VBL = 0x08;
  static constexpr uint8_t INT_STATUS_ANY = 0x80;

  // $C023: the VGC's register — enables in the low bits, pending above them,
  // each pending bit four places above its enable. The scan line is the
  // lower pair, bit 1 with bit 5, and the one-second tick the upper, bit 2
  // with bit 6. The ROM's interrupt manager tests exactly that: `AND #$22 /
  // LSR / LSR` takes the lower pair and dispatches through $E1:0028, which is
  // where QuickDraw II installs its scan-line handler — the one that draws
  // the mouse pointer. With the pairs the other way round, GS/OS's scan-line
  // enable was read as the one-second's, and the pointer was redrawn once a
  // second, on the tick that arrived through QuickDraw's vector instead.
  static constexpr uint8_t VGC_SCANLINE_ENABLE = 0x02;
  static constexpr uint8_t VGC_ONE_SECOND_ENABLE = 0x04;
  static constexpr uint8_t VGC_SCANLINE_PENDING = 0x20;
  static constexpr uint8_t VGC_ONE_SECOND_PENDING = 0x40;
  static constexpr uint8_t VGC_ANY_PENDING = 0x80;
  static constexpr uint8_t NEW_VIDEO_SHR = 0x80;
  static constexpr uint8_t DISK_SELECT_35 = 0x80;
  static constexpr uint8_t DISK_SELECT_DRIVE2 = 0x40;

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

  /**
   * Where a language-card access in bank $00 or $01 actually lands.
   *
   * These two banks are 64K of fast RAM each, and their $D000-$FFFF is their
   * own: the FPI gives a //e program the language card it expects out of the
   * bank's own memory, with the second $D000 bank being the 4K that the I/O
   * space otherwise hides at $C000-$CFFF. None of it is the Mega II's card —
   * that is $E0's and $E1's, a different 32K, and the two must not touch.
   *
   * They did. GS/OS loads its kernel into $00:D000-$FFF9 and $01:D000-$FB48
   * and then its toolbox glue into $E0:E000 and $E1:D980, and a machine that
   * routed the first pair through the Mega II's card had the second pair
   * overwrite them: the kernel's dispatch table sent every call into the
   * middle of whatever had landed on top of the routine it named.
   *
   * ALTZP moves a bank $00 access across to bank $01's card, because that is
   * how a //e asks for the auxiliary card and //e software on the fast side
   * has to behave as it would on a //e. Bank $01 is already there.
   */
  uint32_t fastLanguageCardAddress(uint8_t bank, uint16_t offset) const;

  uint8_t vgcInterruptRegister() const;
  uint8_t interruptStatusRegister() const;

  /** $C034 as one byte: the clock's nibble over the border's. */
  uint8_t clockControlRegister() const {
    return static_cast<uint8_t>((clock_.readControl() & ~BORDER_MASK) | border_);
  }

  /**
   * The card whose ROM answers at this address, if any.
   *
   * A IIgs slot shows either the machine's own firmware or the ROM of a card
   * fitted there, and $C02D is the switch — the Control Panel's "Your Card"
   * setting, in a register. A machine with nothing fitted behaves the same
   * either way, which is why this went unnoticed until there was a card.
   */
  ExpansionCard *cardForSlotRom(uint16_t offset) const;

  // The slot whose card is part of the machine rather than fitted to it. See
  // cardForSlotRom and setInternalCardSlot.
  uint8_t internalCardSlot_ = 0;

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

  /**
   * Which bank a bank $00 access really lands in.
   *
   * A IIgs is a //e whose main RAM is bank $00 and whose auxiliary RAM is
   * bank $01, and the //e's memory switches still say which one an address
   * in bank $00 reaches: RAMRD and RAMWRT for $0200-$BFFF, ALTZP for the
   * zero page, the stack and the language card, and 80STORE with PAGE2 (and
   * HIRES) for the text and hi-res pages, overriding RAMRD and RAMWRT there.
   * That is how the 80-column firmware writes a line's even columns "to
   * auxiliary memory": it writes bank $00's text page with 80STORE and PAGE2
   * on, and the FPI puts them in bank $01, from where they shadow into $E1.
   * A machine that left them in bank $00 drew every other column blank.
   *
   * Bank $01 is never redirected — there is nowhere further to go.
   */
  uint8_t effectiveBank(uint8_t bank, uint16_t offset, bool write) const;

  /** A slot the Control Panel gave to "Your Card", with no card in it. */
  bool slotIsExternalAndEmpty(uint16_t offset) const;

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

  uint64_t slowCycles_ = 0;
  uint64_t slowAccesses_ = 0;
  double remainder_ = 0.0;
  uint8_t shadow_ = 0;
  uint8_t speed_ = 0;
  uint8_t newVideo_ = 0;
  uint8_t slotSelect_ = 0;
  uint8_t diskSelect_ = 0;

  // The VGC's two colour registers. The firmware overwrites both at startup;
  // white on black until it does is what a machine with no settings shows.
  uint8_t textColour_ = 0xF0;
  uint8_t border_ = 0x00;

  IIgsSCC scc_;

  // Interrupt state: what is enabled, and what has happened since it was
  // last cleared.
  uint8_t interruptEnable_ = 0; // $C041
  uint8_t vgcInterrupt_ = 0;    // $C023's enable bits
  bool vblPending_ = false;
  bool scanLinePending_ = false;
  bool quarterSecondPending_ = false;
  bool oneSecondPending_ = false;
  uint64_t lastQuarterSecond_ = 0;
  uint64_t lastSecond_ = 0;

  // Which slots have a drive turning, for the motor detect bits above.
  SlotMotorQuery slotMotorQuery_;

  // Who to tell when the text colours change. See setTextColourCallback.
  TextColourCallback textColourChanged_;
  VolumeCallback volumeChanged_;
  BeamQuery beamQuery_;
};

} // namespace a2e::iigs
