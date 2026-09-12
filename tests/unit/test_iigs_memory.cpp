/*
 * test_iigs_memory.cpp - Unit tests for a IIgs's address space
 *
 * No CPU here, and no machine: an address space is a thing you can write to
 * and read back from, and every question worth asking of a IIgs's — which bank
 * answers, what the Mega II sees, what shadowing copies where — can be asked
 * by hand.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "iigs_memory.hpp"
#include "mmu/mmu.hpp"

#include <vector>

using namespace a2e;
using namespace a2e::iigs;

namespace {
constexpr uint32_t bankAddress(uint8_t bank, uint16_t offset) {
  return (static_cast<uint32_t>(bank) << 16) | offset;
}

// A stand-in ROM: 128KB where every byte is its own low address, so a read can
// say where in the image it came from.
std::vector<uint8_t> makeRom() {
  std::vector<uint8_t> rom(ROM_SIZE_ROM01);
  for (size_t i = 0; i < rom.size(); i++) rom[i] = static_cast<uint8_t>(i);
  return rom;
}
} // namespace

TEST_CASE("A IIgs has fast RAM, slow RAM and ROM, in that order", "[iigs]") {
  IIgsMemory memory;

  SECTION("fast RAM is as many banks as are fitted") {
    // A ROM 01 came with 256KB, which is banks $00 to $03.
    REQUIRE(memory.fastRamSize() == FAST_RAM_SIZE_ROM01);
    REQUIRE(memory.bankIsPopulated(0x00));
    REQUIRE(memory.bankIsPopulated(0x03));
    REQUIRE_FALSE(memory.bankIsPopulated(0x04));

    memory.write(bankAddress(0x03, 0x1234), 0x5A);
    REQUIRE(memory.read(bankAddress(0x03, 0x1234)) == 0x5A);
  }

  SECTION("and a bank with nothing in it reads as nothing") {
    memory.write(bankAddress(0x40, 0x1234), 0x5A);
    REQUIRE(memory.read(bankAddress(0x40, 0x1234)) == 0x00);
  }

  SECTION("a bigger machine has more of it") {
    IIgsMemory expanded(2 * 1024 * 1024);
    REQUIRE(expanded.bankIsPopulated(0x1F));
    expanded.write(bankAddress(0x1F, 0x0100), 0x99);
    REQUIRE(expanded.read(bankAddress(0x1F, 0x0100)) == 0x99);
  }

  SECTION("banks $E0 and $E1 are the Mega II's 128KB") {
    // Which is to say: they are a //e's main and auxiliary RAM, reached
    // directly rather than through RAMRD and RAMWRT.
    memory.write(bankAddress(SLOW_BANK_MAIN, 0x0300), 0x11);
    memory.write(bankAddress(SLOW_BANK_AUX, 0x0300), 0x22);
    REQUIRE(memory.read(bankAddress(SLOW_BANK_MAIN, 0x0300)) == 0x11);
    REQUIRE(memory.read(bankAddress(SLOW_BANK_AUX, 0x0300)) == 0x22);

    // And the same memory the //e's own MMU sees, because it is the same MMU.
    REQUIRE(memory.megaII().readRAM(0x0300, false) == 0x11);
    REQUIRE(memory.megaII().readRAM(0x0300, true) == 0x22);
  }
}

TEST_CASE("The ROM sits at the top of the address space", "[iigs]") {
  const auto rom = makeRom();
  IIgsMemory memory;
  memory.loadROM(rom.data(), rom.size());

  REQUIRE(memory.hasROM());

  SECTION("a 128KB ROM 01 fills banks $FE and $FF") {
    REQUIRE(memory.read(bankAddress(0xFE, 0x0000)) == rom[0]);
    REQUIRE(memory.read(bankAddress(0xFF, 0x0000)) == rom[0x10000]);
    REQUIRE(memory.read(bankAddress(0xFF, 0xFFFF)) == rom[rom.size() - 1]);
  }

  SECTION("and the banks below it are empty on that machine") {
    REQUIRE(memory.read(bankAddress(0xFC, 0x0000)) == 0x00);
  }

  SECTION("writing to it does nothing, quietly") {
    memory.write(bankAddress(0xFF, 0x1000), 0x5A);
    REQUIRE(memory.read(bankAddress(0xFF, 0x1000)) == rom[0x11000]);
  }
}

TEST_CASE("I/O is visible in four banks and nowhere else", "[iigs]") {
  IIgsMemory memory;

  // $C0xx in banks $00, $01, $E0 and $E1 is the machine's I/O. In every other
  // bank it is memory, which is the point of having sixteen megabytes: a
  // program does not have to route around a hole in each one.
  memory.write(bankAddress(0x02, 0xC035), 0x5A);
  REQUIRE(memory.read(bankAddress(0x02, 0xC035)) == 0x5A);
  REQUIRE(memory.read(bankAddress(0x00, 0xC035)) != 0x5A);

  SECTION("and the shadow register can take it away from banks $00 and $01") {
    // Setting the I/O and language card inhibit makes those banks plain RAM
    // all the way up, which is how a program gets a contiguous 128KB.
    memory.setShadowRegister(IIgsMemory::SHADOW_IO_LANGUAGE_CARD);
    memory.write(bankAddress(0x00, 0xC100), 0x37);
    REQUIRE(memory.read(bankAddress(0x00, 0xC100)) == 0x37);

    memory.setShadowRegister(0);
    REQUIRE(memory.read(bankAddress(0x00, 0xC100)) != 0x37);
  }
}

TEST_CASE("Shadowing copies a write to the side the video is looking at",
          "[iigs][shadow]") {
  // The reason shadowing exists: a program runs in fast RAM and writes to bank
  // $00, and the video only ever reads banks $E0 and $E1. Without the copy, a
  // IIgs drawing at 2.8MHz would be drawing into memory nothing looks at.
  IIgsMemory memory;

  SECTION("text page one, which is on by default") {
    memory.write(bankAddress(0x00, 0x0400), 0xC1);
    REQUIRE(memory.read(bankAddress(0x00, 0x0400)) == 0xC1); // still in fast RAM
    REQUIRE(memory.megaII().readRAM(0x0400, false) == 0xC1); // and now over there
  }

  SECTION("and a set bit in the shadow register turns it off") {
    // The register reads backwards from how it sounds: a bit *disables*.
    memory.setShadowRegister(IIgsMemory::SHADOW_TEXT_PAGE1);
    memory.write(bankAddress(0x00, 0x0401), 0xC2);
    REQUIRE(memory.read(bankAddress(0x00, 0x0401)) == 0xC2);
    REQUIRE(memory.megaII().readRAM(0x0401, false) == 0x00);
  }

  SECTION("hi-res pages one and two, each with its own bit") {
    memory.write(bankAddress(0x00, 0x2000), 0x11);
    memory.write(bankAddress(0x00, 0x4000), 0x22);
    REQUIRE(memory.megaII().readRAM(0x2000, false) == 0x11);
    REQUIRE(memory.megaII().readRAM(0x4000, false) == 0x22);

    memory.setShadowRegister(IIgsMemory::SHADOW_HIRES_PAGE1);
    memory.write(bankAddress(0x00, 0x2001), 0x33);
    memory.write(bankAddress(0x00, 0x4001), 0x44);
    REQUIRE(memory.megaII().readRAM(0x2001, false) == 0x00); // off
    REQUIRE(memory.megaII().readRAM(0x4001, false) == 0x44); // still on
  }

  SECTION("bank $01 shadows into the auxiliary side, not the main one") {
    memory.write(bankAddress(0x01, 0x0400), 0x7E);
    REQUIRE(memory.megaII().readRAM(0x0400, true) == 0x7E);
    REQUIRE(memory.megaII().readRAM(0x0400, false) == 0x00);
  }

  SECTION("Super Hi-Res shadows all of bank $01's $2000-$9FFF") {
    // Which is the pixels, the scanline control bytes and the palettes: a IIgs
    // program draws into fast RAM and the video reads $E1.
    memory.write(bankAddress(0x01, SHR_PIXEL_BASE), 0xAA);
    memory.write(bankAddress(0x01, SHR_SCB_BASE), 0xBB);
    memory.write(bankAddress(0x01, SHR_PALETTE_BASE), 0xCC);
    REQUIRE(memory.megaII().readRAM(SHR_PIXEL_BASE, true) == 0xAA);
    REQUIRE(memory.megaII().readRAM(SHR_SCB_BASE, true) == 0xBB);
    REQUIRE(memory.megaII().readRAM(SHR_PALETTE_BASE, true) == 0xCC);

    memory.setShadowRegister(IIgsMemory::SHADOW_SUPER_HIRES);
    memory.write(bankAddress(0x01, 0x9000), 0xDD);
    REQUIRE(memory.megaII().readRAM(0x9000, true) == 0x00);
  }

  SECTION("nothing outside a display region is copied at all") {
    memory.write(bankAddress(0x00, 0x8000), 0x5A);
    REQUIRE(memory.megaII().readRAM(0x8000, false) == 0x00);
  }

  SECTION("and a write straight to the Mega II is not shadowed anywhere") {
    memory.write(bankAddress(SLOW_BANK_MAIN, 0x0400), 0x3C);
    REQUIRE(memory.read(bankAddress(0x00, 0x0400)) == 0x00); // fast RAM untouched
  }
}

TEST_CASE("Bank $00 follows the //e's memory switches into bank $01",
          "[iigs][memory]") {
  // A IIgs is a //e whose main RAM is bank $00 and whose auxiliary RAM is
  // bank $01, and RAMRD, RAMWRT, ALTZP, 80STORE and PAGE2 still decide which
  // of them an address in bank $00 reaches. The 80-column firmware depends on
  // it: a line's even columns are written to the text page with 80STORE and
  // PAGE2 on, meant for auxiliary memory. Left in bank $00 they shadowed into
  // $E0 on top of the odd columns, and every other column drew blank.
  IIgsMemory memory;
  MMU &megaII = memory.megaII();

  SECTION("RAMWRT and RAMRD move $0200-$BFFF") {
    memory.write(0x00C005, 0); // RAMWRT on
    memory.write(0x003000, 0x5A);
    REQUIRE(memory.peek(0x013000) == 0x5A);
    REQUIRE(memory.fastRamByte(0x003000) == 0x00); // bank $00 untouched
    memory.write(0x00C004, 0); // RAMWRT off
    memory.write(0x003000, 0xA5);
    REQUIRE(memory.fastRamByte(0x003000) == 0xA5);
    REQUIRE(memory.peek(0x013000) == 0x5A);

    REQUIRE(memory.read(0x003000) == 0xA5); // RAMRD off: bank $00's
    memory.write(0x00C003, 0); // RAMRD on
    REQUIRE(memory.read(0x003000) == 0x5A); // bank $01's
    memory.write(0x00C002, 0);

    // Bank $01 itself is never redirected.
    memory.write(0x00C005, 0);
    memory.write(0x013000, 0x33);
    REQUIRE(memory.peek(0x013000) == 0x33);
  }

  SECTION("80STORE and PAGE2 hand the text page to bank $01, and shadow it into $E1") {
    memory.write(0x00C001, 0); // 80STORE on
    memory.write(0x00C055, 0); // PAGE2 on
    memory.write(0x000400, 0xC1); // an 'A' in an even column
    REQUIRE(memory.peek(0x010400) == 0xC1);
    REQUIRE(memory.fastRamByte(0x000400) == 0x00);
    REQUIRE(megaII.readRAM(0x0400, true) == 0xC1);  // the aux text page
    REQUIRE(megaII.readRAM(0x0400, false) == 0x00); // not the main one

    memory.write(0x00C054, 0); // PAGE2 off: the main text page, whatever RAMWRT says
    memory.write(0x00C005, 0); // RAMWRT on
    memory.write(0x000401, 0xC2);
    REQUIRE(memory.fastRamByte(0x000401) == 0xC2);
    REQUIRE(memory.peek(0x010401) == 0x00);
    REQUIRE(megaII.readRAM(0x0401, false) == 0xC2);
    // ...while an address outside the text page still follows RAMWRT.
    memory.write(0x000800, 0x77);
    REQUIRE(memory.peek(0x010800) == 0x77);
  }

  SECTION("80STORE with HIRES hands the first hi-res page to PAGE2 too") {
    memory.write(0x00C001, 0); // 80STORE
    memory.write(0x00C057, 0); // HIRES
    memory.write(0x00C055, 0); // PAGE2
    memory.write(0x002000, 0x11);
    REQUIRE(memory.peek(0x012000) == 0x11);
    REQUIRE(memory.fastRamByte(0x002000) == 0x00);
    memory.write(0x00C056, 0); // LORES: the hi-res page is RAMWRT's again
    memory.write(0x002001, 0x22);
    REQUIRE(memory.fastRamByte(0x002001) == 0x22);
  }

  SECTION("ALTZP moves the zero page and the stack") {
    memory.write(0x00C009, 0); // ALTZP on
    memory.write(0x000080, 0x42);
    memory.write(0x0001FF, 0x43);
    REQUIRE(memory.peek(0x010080) == 0x42);
    REQUIRE(memory.peek(0x0101FF) == 0x43);
    REQUIRE(memory.fastRamByte(0x000080) == 0x00);
    REQUIRE(memory.read(0x000080) == 0x42);
    memory.write(0x00C008, 0);
    REQUIRE(memory.read(0x000080) == 0x00);
  }
}

TEST_CASE("A slot given to Your Card with nothing in it reads the bus, not the firmware",
          "[iigs][memory][slots]") {
  // $C02D says which slots the internal firmware answers for. A slot the
  // Control Panel has handed to a card that is not there answers as an empty
  // slot does on any Apple II — with the bus — rather than with the firmware
  // the setting was meant to hide. ProDOS 8 2.4.1 finds the AppleTalk
  // firmware's "ATLK" in slot 7 and calls into it, which ends in a BRK on a
  // ROM 01; the known way round is to set slot 7 to Your Card, and that only
  // works if the firmware then goes away.
  const std::vector<uint8_t> rom = makeRom();
  IIgsMemory memory;
  memory.loadROM(rom.data(), rom.size());
  const uint8_t firmware = memory.peek(0x00C7F9);

  memory.write(0x00C02D, 0x00); // all internal
  REQUIRE(memory.read(0x00C7F9) == firmware);
  memory.write(0x00C02D, 0x80); // slot 7: Your Card
  REQUIRE(memory.peek(0x00C7F9) == 0xFF);
  // Slot 5 is the machine's own SmartPort and is not switched by $C02D; with
  // no image in it its firmware still shows through (see setInternalCardSlot).
  memory.write(0x00C02D, 0xFE);
  REQUIRE(memory.read(0x00C3F9) == 0xFF);
  REQUIRE(memory.read(0x00C7F9) == 0xFF);
}

TEST_CASE("The state register is eight soft switches in one byte",
          "[iigs][state]") {
  // A //e sets its memory map with eight separate addresses. A IIgs program
  // can save the lot, change it, and put it back in two instructions — and the
  // switches it is writing are the Mega II's own, so everything that watches
  // them sees the change the usual way.
  IIgsMemory memory;

  memory.write(bankAddress(0x00, 0xC068),
               IIgsMemory::STATE_ALTZP | IIgsMemory::STATE_RAMRD);
  const SoftSwitches &switches = memory.megaII().getSoftSwitches();
  REQUIRE(switches.altzp);
  REQUIRE(switches.ramrd);
  REQUIRE_FALSE(switches.ramwrt);
  REQUIRE_FALSE(switches.page2);

  SECTION("and reading it back gives what was written") {
    const uint8_t state = memory.read(bankAddress(0x00, 0xC068));
    REQUIRE((state & IIgsMemory::STATE_ALTZP) != 0);
    REQUIRE((state & IIgsMemory::STATE_RAMRD) != 0);
    REQUIRE((state & IIgsMemory::STATE_RAMWRT) == 0);
  }

  SECTION("the //e's own addresses still work, and agree with it") {
    memory.write(bankAddress(0x00, 0xC055), 0); // PAGE2 on, the //e way
    REQUIRE((memory.read(bankAddress(0x00, 0xC068)) & IIgsMemory::STATE_PAGE2) !=
            0);
  }

  SECTION("RDROM says whether $D000 is the language card or the ROM") {
    const auto rom = makeRom();
    memory.loadROM(rom.data(), rom.size());

    // Put something in the language card's RAM that the ROM does not have
    // there, so that "which one answered" is a question with an answer. The
    // first version of this test compared two addresses that both held zero
    // and passed while the mapping underneath it was inverted.
    //
    // Writing to the card needs the write latch, and the state register has no
    // bit for it — a program enables writes the //e's way, with two reads of
    // $C08B, and the state register leaves that alone afterwards.
    memory.write(bankAddress(0x00, 0xC068), 0); // bank one, reading RAM
    memory.read(bankAddress(0x00, 0xC08B));
    memory.read(bankAddress(0x00, 0xC08B));
    memory.write(bankAddress(0xE0, 0xD000), 0x5A);
    REQUIRE(memory.read(bankAddress(0xE0, 0xD000)) == 0x5A);
    REQUIRE((memory.read(bankAddress(0x00, 0xC068)) & IIgsMemory::STATE_RDROM) ==
            0);

    memory.write(bankAddress(0x00, 0xC068), IIgsMemory::STATE_RDROM);
    REQUIRE((memory.read(bankAddress(0x00, 0xC068)) & IIgsMemory::STATE_RDROM) !=
            0);
    const uint8_t expected = rom[ROM_SIZE_ROM01 - 0x10000 + 0xD000];
    REQUIRE(memory.read(bankAddress(0xE0, 0xD000)) == expected);
    REQUIRE(memory.read(bankAddress(0x00, 0xD000)) == expected);

    // ...and the card's RAM is still there underneath, waiting.
    memory.write(bankAddress(0x00, 0xC068), 0);
    REQUIRE(memory.read(bankAddress(0xE0, 0xD000)) == 0x5A);
  }

  SECTION("and which bank of the card is showing") {
    // $D000-$DFFF is two banks of RAM sharing one range of addresses, and the
    // state register chooses between them like any other part of the map.
    memory.write(bankAddress(0x00, 0xC068), IIgsMemory::STATE_LCBANK2);
    memory.read(bankAddress(0x00, 0xC083));
    memory.read(bankAddress(0x00, 0xC083));
    memory.write(bankAddress(0xE0, 0xD400), 0x22);

    memory.write(bankAddress(0x00, 0xC068), 0); // bank one
    memory.read(bankAddress(0x00, 0xC08B));
    memory.read(bankAddress(0x00, 0xC08B));
    memory.write(bankAddress(0xE0, 0xD400), 0x11);

    REQUIRE(memory.read(bankAddress(0xE0, 0xD400)) == 0x11);
    memory.write(bankAddress(0x00, 0xC068), IIgsMemory::STATE_LCBANK2);
    REQUIRE(memory.read(bankAddress(0xE0, 0xD400)) == 0x22);
  }
}

TEST_CASE("The state register leaves the write latch where it found it",
          "[iigs][state]") {
  // The language card's write enable is not one of the eight bits, and a
  // program changing the memory map should not find its card quietly
  // write-protected afterwards — or quietly writable, which is worse.
  IIgsMemory memory;

  memory.read(bankAddress(0x00, 0xC08B)); // enable writes, bank one
  memory.read(bankAddress(0x00, 0xC08B));
  REQUIRE(memory.megaII().getSoftSwitches().lcwrite);

  memory.write(bankAddress(0x00, 0xC068), IIgsMemory::STATE_RDROM);
  REQUIRE(memory.megaII().getSoftSwitches().lcwrite);

  memory.write(bankAddress(0x00, 0xC068), 0);
  REQUIRE(memory.megaII().getSoftSwitches().lcwrite);

  SECTION("and a write-protected card stays that way too") {
    memory.read(bankAddress(0x00, 0xC088)); // read RAM, write protected
    REQUIRE_FALSE(memory.megaII().getSoftSwitches().lcwrite);
    memory.write(bankAddress(0x00, 0xC068), IIgsMemory::STATE_LCBANK2);
    REQUIRE_FALSE(memory.megaII().getSoftSwitches().lcwrite);
  }
}

TEST_CASE("The speed register is the machine's other clock", "[iigs]") {
  IIgsMemory memory;
  REQUIRE_FALSE(memory.isFastSpeed()); // A IIgs comes up slow

  memory.write(bankAddress(0x00, 0xC036), IIgsMemory::SPEED_FAST);
  REQUIRE(memory.isFastSpeed());
  REQUIRE(memory.read(bankAddress(0x00, 0xC036)) == IIgsMemory::SPEED_FAST);
}

TEST_CASE("A reset puts the map back the way the firmware expects it",
          "[iigs]") {
  IIgsMemory memory;
  memory.setShadowRegister(0xFF);
  memory.setSpeedRegister(IIgsMemory::SPEED_FAST);

  memory.reset();

  // Shadowing all on and the slow clock: a IIgs comes up pretending to be a
  // //e as hard as it can.
  REQUIRE(memory.shadowRegister() == 0);
  REQUIRE_FALSE(memory.isFastSpeed());
}

TEST_CASE("The slow clock ticks on the accesses that reach the Mega II",
          "[iigs][timing]") {
  // A IIgs runs at 2.8MHz until it reaches across to the slow side, and that
  // access is stretched to a Mega II cycle. The clock therefore advances
  // *during* an instruction, at each access — which is what the drive needs,
  // because a disk read loop is a few cycles with one access in it and a clock
  // that only moved between instructions would show it to the drive in lumps.
  IIgsMemory memory;
  memory.resetClock();

  const uint64_t start = memory.slowCycles();
  memory.read(bankAddress(0x02, 0x1000)); // fast RAM: no charge
  REQUIRE(memory.slowCycles() == start);

  memory.read(bankAddress(0x00, 0xC019)); // I/O: a slow cycle
  REQUIRE(memory.slowCycles() == start + 1);

  memory.read(bankAddress(SLOW_BANK_MAIN, 0x0400)); // the Mega II's RAM: another
  REQUIRE(memory.slowCycles() == start + 2);

  memory.write(bankAddress(0x00, 0xC000), 0); // and writes cost the same
  REQUIRE(memory.slowCycles() == start + 3);

  SECTION("and the rest of an instruction is added by whoever knows the speed") {
    const uint64_t before = memory.slowCycles();
    memory.addFastCycles(2.5);
    memory.addFastCycles(2.5); // the halves add up rather than being lost
    REQUIRE(memory.slowCycles() == before + 5);
  }
}

TEST_CASE("A IIgs has a register for its slots and one for its drives",
          "[iigs]") {
  IIgsMemory memory;

  // $C02D says which slots answer from a card and which from the machine's own
  // firmware; $C031 points the one disk chip at one of four drives. The
  // firmware polls both early, and a machine that answers the floating bus
  // here never gets as far as looking for a disk.
  memory.write(bankAddress(0x00, 0xC02D), 0x80);
  REQUIRE(memory.read(bankAddress(0x00, 0xC02D)) == 0x80);

  memory.write(bankAddress(0x00, 0xC031), IIgsMemory::DISK_SELECT_35);
  REQUIRE(memory.selects35Inch());
  memory.write(bankAddress(0x00, 0xC031), 0x00);
  REQUIRE_FALSE(memory.selects35Inch());
}

// ---------------------------------------------------------------------------
// How much fast RAM the machine has, which is a choice rather than a constant
// ---------------------------------------------------------------------------

TEST_CASE("Fast RAM is fitted a bank at a time, between what a machine could have",
          "[iigs][memory]") {
  // A ROM 01 had 256K on the board and a memory expansion card took it
  // further; RAM arrives 64K at a time and the bus reaches eight megabytes.
  // Anything the host asks for outside that is rounded rather than refused,
  // because a size somebody typed should still give a machine that starts.
  REQUIRE(clampFastRamSize(0) == FAST_RAM_SIZE_ROM01);
  REQUIRE(clampFastRamSize(64 * 1024) == FAST_RAM_SIZE_ROM01);
  REQUIRE(clampFastRamSize(FAST_RAM_SIZE_MAX * 4) == FAST_RAM_SIZE_MAX);

  // A size between banks loses the remainder rather than gaining a part bank.
  REQUIRE(clampFastRamSize(1024 * 1024 + 1) == 1024 * 1024);
  REQUIRE(clampFastRamSize(1024 * 1024 + BANK_SIZE) == 1024 * 1024 + BANK_SIZE);
}

TEST_CASE("A bigger machine answers in the banks a smaller one does not",
          "[iigs][memory]") {
  // The point of the setting: banks above the built-in 256K have to be real
  // memory, and banks above what is fitted have to not be — the firmware sizes
  // RAM by writing to a bank and reading it back, so a machine whose empty
  // banks answered would report memory it has not got.
  IIgsMemory small(FAST_RAM_SIZE_ROM01);
  IIgsMemory large(1024 * 1024);

  const uint32_t inExpansion = (0x08u << 16) | 0x1234; // bank $08
  const uint32_t beyondBoth = (0x40u << 16) | 0x1234;  // bank $40

  small.write(inExpansion, 0x5A);
  large.write(inExpansion, 0x5A);
  REQUIRE(small.read(inExpansion) != 0x5A); // 256K stops at bank $03
  REQUIRE(large.read(inExpansion) == 0x5A);

  large.write(beyondBoth, 0x5A);
  REQUIRE(large.read(beyondBoth) != 0x5A);

  REQUIRE(small.fastRamSize() == FAST_RAM_SIZE_ROM01);
  REQUIRE(large.fastRamSize() == 1024 * 1024);
}

TEST_CASE("Banks $E0 and $E1 are two different 64K, language card included",
          "[iigs][memory]") {
  // The Mega II's two banks are main and auxiliary, and a IIgs reaches them by
  // bank number rather than through the //e's soft switches. Below $D000 that
  // was always so. Above it the map has the language card in it, and a //e
  // picks its half with ALTZP because that is the only way a //e can ask —
  // which makes $E0 and $E1 the same 48K if the IIgs asks the same way.
  //
  // They are not the same 48K. The toolbox's vectors, the Memory Manager's
  // tables and GS/OS all live in $E1's, and a machine where $E0 overwrites
  // them gets as far as the Memory Manager refusing to allocate anything.
  IIgsMemory memory;

  // Language card RAM readable and writable, so $D000 upward is RAM not ROM.
  memory.read(0x00C083);
  memory.read(0x00C083);

  for (uint16_t offset : {uint16_t(0xD000), uint16_t(0xE000), uint16_t(0xFFF0)}) {
    memory.write(0x00E00000u | offset, 0x11);
    memory.write(0x00E10000u | offset, 0x22);
    INFO("at $" << std::hex << offset);
    REQUIRE(memory.read(0x00E00000u | offset) == 0x11);
    REQUIRE(memory.read(0x00E10000u | offset) == 0x22);
    // A debugger must see the same two banks the processor does.
    REQUIRE(memory.peek(0x00E00000u | offset) == 0x11);
    REQUIRE(memory.peek(0x00E10000u | offset) == 0x22);
  }

  // The half the bank names is not the half ALTZP names: switching ALTZP must
  // not move what bank $E1 reads.
  memory.write(0x00C009, 0x00); // ALTZP on
  REQUIRE(memory.read(0x00E10000u | 0xE000) == 0x22);
  memory.write(0x00C008, 0x00); // ALTZP off
  REQUIRE(memory.read(0x00E10000u | 0xE000) == 0x22);
}

TEST_CASE("Banks $00 and $01 have a language card of their own, out of fast RAM",
          "[iigs][memory]") {
  // These two banks are 64K of fast RAM each. Their $D000-$FFFF is the bank's
  // own memory, given the shape of a //e's language card by the FPI, with the
  // second $D000 bank being the 4K that the I/O space hides at $C000-$CFFF.
  // None of it is the Mega II's card in $E0 and $E1 — that is a different 32K.
  //
  // GS/OS puts its kernel in $00:D000 and $01:D000 and its toolbox glue in
  // $E0:E000 and $E1:D980, and a machine that made those the same memory had
  // the second pair land on top of the first: the kernel's dispatch table sent
  // every call into the middle of whatever had overwritten the routine.
  IIgsMemory memory;
  memory.read(0x00C083);
  memory.read(0x00C083); // read/write RAM, bank 2 of $D000

  // Four banks, four different bytes, at the same offset — and every one
  // reads back its own.
  memory.write(0x00E123, 0x11);
  memory.write(0x01E123, 0x22);
  memory.write(0x00E0E123, 0xE0);
  memory.write(0x00E1E123, 0xE1);
  REQUIRE(memory.read(0x00E123) == 0x11);
  REQUIRE(memory.read(0x01E123) == 0x22);
  REQUIRE(memory.read(0x00E0E123) == 0xE0);
  REQUIRE(memory.read(0x00E1E123) == 0xE1);

  // The switched $D000 area has two banks, and they are different memory.
  memory.write(0x00D06F, 0xB2); // bank 2 selected above
  memory.read(0x00C08B);
  memory.read(0x00C08B);        // read/write RAM, bank 1
  REQUIRE(memory.read(0x00D06F) != 0xB2);
  memory.write(0x00D06F, 0xB1);
  REQUIRE(memory.read(0x00D06F) == 0xB1);
  memory.read(0x00C083);
  memory.read(0x00C083);        // back to bank 2
  REQUIRE(memory.read(0x00D06F) == 0xB2);

  // Bank 1 of $D000 is the RAM under the I/O space: take the I/O and
  // language card out of the map and it shows at $C000.
  memory.setShadowRegister(IIgsMemory::SHADOW_IO_LANGUAGE_CARD);
  REQUIRE(memory.read(0x00C06F) == 0xB1);
  memory.setShadowRegister(0);

  // The switches still mean what they mean on a //e.
  memory.read(0x00C082); // read ROM, write protected
  REQUIRE(memory.read(0x00E123) == memory.read(0x00FFE123));
  memory.write(0x00E123, 0x33);
  memory.read(0x00C083);
  memory.read(0x00C083);
  REQUIRE(memory.read(0x00E123) == 0x11); // the protected write was dropped

  // ALTZP is how a //e asks for the auxiliary card, having no bank to name it
  // with, so it moves bank $00 across to bank $01's. Bank $01 stays put, and
  // the Mega II's banks are not involved at all.
  memory.write(0x00C009, 0x00); // ALTZP on
  REQUIRE(memory.read(0x00E123) == 0x22);
  REQUIRE(memory.read(0x01E123) == 0x22);
  REQUIRE(memory.read(0x00E0E123) == 0xE0);
  memory.write(0x00C008, 0x00); // ALTZP off
  REQUIRE(memory.read(0x00E123) == 0x11);

  // A debugger sees the same memory the processor does.
  REQUIRE(memory.peek(0x00E123) == 0x11);
  REQUIRE(memory.peek(0x01E123) == 0x22);
}

TEST_CASE("The VGC's one-second interrupt is enabled by bit 2, reported in bit 6",
          "[iigs][memory][interrupt]") {
  // Four above its enable, and acknowledged by writing $C032 with bit 6 low.
  IIgsMemory memory;
  REQUIRE_FALSE(memory.interruptPending());

  memory.write(0x00C023, IIgsMemory::VGC_ONE_SECOND_ENABLE);
  REQUIRE(memory.read(0x00C023) == IIgsMemory::VGC_ONE_SECOND_ENABLE);

  // A second of the slow clock goes by.
  memory.addFastCycles(1023000.0);
  memory.tickClocks();
  REQUIRE(memory.interruptPending());
  REQUIRE((memory.read(0x00C023) & 0xC4) == 0xC4); // any, one-second, enabled
  REQUIRE((memory.read(0x00C023) & 0x20) == 0);    // not the scan line

  memory.write(0x00C032, 0xFF); // nothing acknowledged
  REQUIRE(memory.interruptPending());
  memory.write(0x00C032, 0xDF); // bit 5 low is the scan line's, not this
  REQUIRE(memory.interruptPending());
  memory.write(0x00C032, 0xBF); // bit 6 low
  REQUIRE_FALSE(memory.interruptPending());
  REQUIRE(memory.read(0x00C023) == IIgsMemory::VGC_ONE_SECOND_ENABLE);
}

TEST_CASE("The VGC's scan-line interrupt is enabled by bit 1, reported in bit 5",
          "[iigs][memory][interrupt]") {
  // The lower pair: bit 1 with bit 5, which is what the ROM's manager tests
  // with `AND #$22 / LSR / LSR` before dispatching through $E1:0028, the
  // vector QuickDraw II hangs its pointer-drawing handler on. That handler
  // acknowledges by writing $DF to $C032 — bit 5 low. With the two pairs the
  // other way round, GS/OS's scan-line enable was taken as the one-second's,
  // and the pointer was redrawn once a second.
  IIgsMemory memory;
  memory.signalScanLine();
  REQUIRE_FALSE(memory.interruptPending()); // it happened, but it is not enabled
  REQUIRE((memory.read(0x00C023) & 0xE0) == 0);

  memory.write(0x00C023, 0x02);
  REQUIRE(memory.read(0x00C023) == (IIgsMemory::VGC_SCANLINE_ENABLE | 0xA0)); // any, scan line, enabled
  REQUIRE(memory.interruptPending());
  REQUIRE((memory.read(0x00C023) & 0x40) == 0); // not the second

  memory.write(0x00C032, 0xBF); // bit 6 low acknowledges the second, not this
  REQUIRE(memory.interruptPending());
  memory.write(0x00C032, 0xDF); // what QuickDraw II writes
  REQUIRE_FALSE(memory.interruptPending());
  REQUIRE(memory.read(0x00C023) == IIgsMemory::VGC_SCANLINE_ENABLE);
}

TEST_CASE("$C02E and $C02F say where the beam is", "[iigs][memory][video]") {
  // The Mega II's counters, as the IIgs exposes them: the vertical counter
  // runs $100-$1BF over the picture and $1C0-$1FF then $FA-$FF through
  // blanking, the horizontal counter is 0 for a line's first cycle and $40-$7F
  // for the rest; $C02E is the vertical counter's bits 8-1 and $C02F its bit 0
  // above the horizontal counter. The IIgs Diagnostic measures the processor's
  // speed against these, and failed at once on a machine that answered nothing.
  IIgsMemory memory;
  IIgsMemory::Beam beam{0, 0};
  memory.setBeamQuery([&beam]() { return beam; });

  REQUIRE(memory.read(0x00C02E) == 0x80); // line 0: $100 >> 1
  REQUIRE(memory.read(0x00C02F) == 0x00);
  beam = {101, 10};                       // $165: odd, so bit 0 shows in $C02F
  REQUIRE(memory.read(0x00C02E) == 0xB2);
  REQUIRE(memory.read(0x00C02F) == (0x80 | 0x49));
  beam = {200, 64};                       // blanking: $1C8
  REQUIRE(memory.read(0x00C02E) == 0xE4);
  REQUIRE(memory.read(0x00C02F) == 0x7F);
  beam = {261, 1};                        // the last line: $FF
  REQUIRE(memory.read(0x00C02E) == 0x7F);
  REQUIRE(memory.read(0x00C02F) == (0x80 | 0x40));

  // A read clears a pending scan-line interrupt.
  memory.write(0x00C023, IIgsMemory::VGC_SCANLINE_ENABLE);
  memory.signalScanLine();
  REQUIRE(memory.interruptPending());
  memory.read(0x00C02E);
  REQUIRE_FALSE(memory.interruptPending());
}

TEST_CASE("A Mega II access from the fast side waits for the slow clock",
          "[iigs][memory][timing]") {
  // The processor stops at the slow clock's next edge and then takes a whole
  // slow cycle, so an access made part-way through a slow cycle costs the
  // rest of that cycle and then one. At slow speed nothing is part-way
  // through anything and an access costs one.
  IIgsMemory memory;
  memory.write(0x00C036, 0x80); // fast (an access of its own: one cycle)
  const uint64_t base = memory.slowCycles();
  memory.addFastCycles(2.3);    // two whole cycles and a fraction
  REQUIRE(memory.slowCycles() == base + 2);
  memory.read(0x00C019);        // a Mega II register
  REQUIRE(memory.slowCycles() == base + 4); // the rest of the third, then the access
  memory.read(0x00C019);        // now on the edge: just the access
  REQUIRE(memory.slowCycles() == base + 5);
  memory.takeSlowAccesses();

  memory.write(0x00C036, 0x00); // slow (and another whole access)
  memory.addFastCycles(3.0);
  memory.read(0x00C019);
  REQUIRE(memory.slowCycles() == base + 10);
}

TEST_CASE("Vertical blanking interrupts through $C041, $C046 and $C047",
          "[iigs][memory][interrupt]") {
  IIgsMemory memory;
  memory.signalVerticalBlank();
  REQUIRE_FALSE(memory.interruptPending()); // it happened, but it is not enabled
  // ...and the flag says it happened all the same: $C046 reports what
  // occurred, and only $C047 clears it. The diagnostic's handler switches the
  // interrupt off before it looks, and must still find the flag.
  REQUIRE(memory.read(0x00C046) == IIgsMemory::INT_VBL);

  memory.write(0x00C041, IIgsMemory::INT_VBL);
  REQUIRE(memory.read(0x00C041) == IIgsMemory::INT_VBL);
  REQUIRE(memory.interruptPending());
  REQUIRE(memory.read(0x00C046) == (IIgsMemory::INT_STATUS_ANY | IIgsMemory::INT_VBL));

  memory.write(0x00C041, 0x00); // disabled again: the line drops, the flag stays
  REQUIRE_FALSE(memory.interruptPending());
  REQUIRE(memory.read(0x00C046) == IIgsMemory::INT_VBL);

  memory.write(0x00C047, 0x08); // any write acknowledges
  REQUIRE(memory.read(0x00C046) == 0x00);
}

TEST_CASE("The serial chip is quiet", "[iigs][memory][interrupt]") {
  // The interrupt manager asks the SCC first on every interrupt: writes 3 to
  // the command register to select RR3 and reads it back, taking any set bit
  // as a serial interrupt. A machine with no chip there returned the bus,
  // which read as an interrupting SCC, and the manager serviced a port that
  // does not exist instead of the vertical blank that had fired.
  IIgsMemory memory;
  memory.write(0x00C039, 0x03);
  REQUIRE(memory.read(0x00C039) == 0x00); // RR3: nothing pending
  REQUIRE((memory.read(0x00C039) & 0x05) == 0x04); // RR0 after the pointer resets: transmit buffer empty, nothing received
  memory.write(0x00C038, 0x03);
  REQUIRE(memory.read(0x00C038) == 0x00);
  REQUIRE(memory.read(0x00C03B) == 0x00); // nothing received
}
