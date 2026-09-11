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
