/*
 * test_iigs_boot.cpp - A IIgs running its own firmware
 *
 * Everything else about this machine is tested a part at a time. This runs the
 * whole of it: the real ROM, the 65816, the memory map, shadowing, the Mega
 * II's video, the ADB controller and the Ensoniq's RAM, with nothing standing
 * in for anything — and asks the machine what is on its screen.
 *
 * A IIgs will not draw a single character until it has been through its power-
 * on diagnostics, so the screen is the proof: if the map is wrong, or the
 * decimal flags, or the ADB's status register, the machine stops and says so
 * rather than starting.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "cpu65816.hpp"
#include "iigs_machine.hpp"
#include "iigs_adb.hpp"
#include "iigs_clock.hpp"
#include "iigs_memory.hpp"
#include "mmu/mmu.hpp"
#include "roms.cpp"

#include <string>

using namespace a2e;
using namespace a2e::iigs;

namespace {
bool romAvailable() {
  return roms::ROM_SYSTEM_IIGS_SIZE >= ROM_SIZE_ROM01;
}

// Long enough for the diagnostics, the splash and the boot attempt; a real
// machine takes a couple of seconds over it.
void runToPrompt(IIgsMachine &machine) {
  for (int i = 0; i < 2000000 && !machine.cpu().isStopped(); i++) {
    machine.step();
  }
}
} // namespace

TEST_CASE("A IIgs boots its own firmware", "[iigs][boot]") {
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the boot test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE, roms::ROM_CHAR,
               roms::ROM_CHAR_SIZE);

  SECTION("the reset vector comes out of the ROM, not out of nothing") {
    // $00:FFFC is the language card's space, and a machine that has just been
    // powered on reads ROM there — so this is the ROM's own vector, and if the
    // memory map were wrong it would be $0000.
    REQUIRE(machine.cpu().getPC() != 0x0000);
    REQUIRE(machine.cpu().getEmulation()); // as every 65816 starts
  }

  SECTION("and gets through its diagnostics to the startup screen") {
    runToPrompt(machine);
    const std::string screen = machine.screenText();
    INFO("screen:\n" << screen);

    // What a real IIgs with no disk in it says, in this order: its own name,
    // and then a complaint about the drive.
    REQUIRE(screen.find("Fatal") == std::string::npos);
    REQUIRE(screen.find("Check startup device") != std::string::npos);
  }

  SECTION("the text it drew is on the Mega II's side of the machine") {
    // The firmware runs in fast RAM and writes through bank $00; the video
    // only ever looks at $E0. Every character on that screen got there by
    // being shadowed across, so this is shadowing tested by the machine
    // itself rather than by a unit test's expectations.
    runToPrompt(machine);
    bool anyText = false;
    for (uint16_t at = 0x0400; at < 0x0800; at++) {
      if (machine.memory().megaII().readRAM(at, false) > 0xA0) anyText = true;
    }
    REQUIRE(anyText);
  }
}

TEST_CASE("A IIgs notices somebody typing", "[iigs][boot][adb]") {
  // The whole path in one: a browser key event, translated the //e's way,
  // handed to the ADB controller, put by the controller into the register the
  // Mega II reads, and taken from there by firmware that has no idea any of
  // that happened. What proves the last step is the strobe: only the machine
  // can clear it, by reading $C010.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the keyboard test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE, roms::ROM_CHAR,
               roms::ROM_CHAR_SIZE);
  runToPrompt(machine);

  machine.keyDown('A');
  REQUIRE(machine.memory().adb().keyboardLatch() == ('A' | 0x80));

  for (int i = 0; i < 50000; i++) machine.step();
  REQUIRE((machine.memory().adb().keyboardLatch() & 0x80) == 0);
  REQUIRE((machine.memory().adb().keyboardLatch() & 0x7F) == 'A');
}

TEST_CASE("A IIgs writes its settings into battery RAM", "[iigs][boot][clock]") {
  // The first Apple II that remembers anything. A machine whose battery RAM is
  // nonsense spends its startup putting the defaults back, and that is what
  // this sees: 256 bytes that were zero before the firmware ran and hold a
  // configuration afterwards.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the battery RAM test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE, roms::ROM_CHAR,
               roms::ROM_CHAR_SIZE);

  int before = 0;
  for (int i = 0; i < 256; i++) {
    if (machine.memory().clock().batteryRam(static_cast<uint8_t>(i))) before++;
  }
  REQUIRE(before == 0);

  runToPrompt(machine);

  int after = 0;
  for (int i = 0; i < 256; i++) {
    if (machine.memory().clock().batteryRam(static_cast<uint8_t>(i))) after++;
  }
  INFO("non-zero battery RAM bytes after boot: " << after);
  REQUIRE(after > 32);
}

TEST_CASE("A IIgs without a ROM says so rather than running", "[iigs][boot]") {
  IIgsMachine machine;
  REQUIRE_FALSE(machine.hasROM());

  // Nothing to run: the reset vector reads as zero and the CPU goes to $0000,
  // which is why the machine is offered as unavailable rather than started.
  machine.reset();
  REQUIRE(machine.cpu().getPC() == 0x0000);
}
