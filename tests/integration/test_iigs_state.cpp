/*
 * test_iigs_state.cpp - A IIgs saved and restored
 *
 * The state has to carry what a //e's does not have: a 65816's widths and
 * banks, the fast RAM, the shadow and speed registers, the Ensoniq's RAM and
 * the clock chip's battery RAM. Each is set to something a fresh machine would
 * not have, exported, restored into a fresh machine, and read back.
 */
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "audio/audio.hpp"
#include "cards/disk_controller.hpp"
#include "cpu65816.hpp"
#include "iigs_machine.hpp"
#include "iigs_memory.hpp"
#include "mmu/mmu.hpp"

#include <string>
#include <vector>

using namespace a2e;
using namespace a2e::iigs;

namespace {
std::vector<uint8_t> exported(IIgsMachine &machine) {
  size_t size = 0;
  const uint8_t *data = machine.exportState(&size);
  REQUIRE(data != nullptr);
  REQUIRE(size > 0);
  return std::vector<uint8_t>(data, data + size);
}
} // namespace

TEST_CASE("A IIgs state carries the machine id after the magic", "[iigs][state]") {
  IIgsMachine machine(256 * 1024);
  machine.init(nullptr, 0);
  auto state = exported(machine);
  REQUIRE(state.size() > 12);
  REQUIRE(std::string(state.begin(), state.begin() + 4) == "AE2S");
  REQUIRE(state[8] == 3); // MachineId::AppleIIgs
}

TEST_CASE("A IIgs state round-trips the processor, both banks of RAM and the registers",
          "[iigs][state]") {
  IIgsMachine machine(256 * 1024);
  machine.init(nullptr, 0);

  // A native-mode processor with 16-bit registers and banks that are not zero.
  CPU65816 &cpu = machine.cpu();
  cpu.setEmulation(false);
  cpu.setP(0x00);
  cpu.setA(0x1234);
  cpu.setX(0x5678);
  cpu.setY(0x9ABC);
  cpu.setSP(0x1FF0);
  cpu.setD(0x0100);
  cpu.setPC(0x2000);
  cpu.setPBR(0x02);
  cpu.setDBR(0x03);
  cpu.setTotalCycles(123456789);

  // Fast RAM in a bank nothing shadows, and the Mega II's own RAM.
  IIgsMemory &memory = machine.memory();
  memory.write(0x020100, 0xA5);
  memory.megaII().writeRAM(0x0300, 0x5A, false);
  memory.megaII().writeRAM(0x0300, 0xC3, true);
  memory.setShadowRegister(0x3F);
  memory.setSpeedRegister(0x80);
  memory.sound().setSoundRam(0x0100, 0x42);
  memory.clock().setBatteryRam(5, 0x99);
  memory.clock().setSeconds(0x12345678);

  auto state = exported(machine);

  IIgsMachine restored(256 * 1024);
  restored.init(nullptr, 0);
  REQUIRE(restored.memory().read(0x020100) != 0xA5);
  REQUIRE(restored.importState(state.data(), state.size()));

  CPU65816 &rc = restored.cpu();
  REQUIRE_FALSE(rc.getEmulation());
  REQUIRE(rc.getP() == 0x00);
  REQUIRE(rc.getA() == 0x1234);
  REQUIRE(rc.getX() == 0x5678);
  REQUIRE(rc.getY() == 0x9ABC);
  REQUIRE(rc.getSP() == 0x1FF0);
  REQUIRE(rc.getD() == 0x0100);
  REQUIRE(rc.getPC() == 0x2000);
  REQUIRE(rc.getPBR() == 0x02);
  REQUIRE(rc.getDBR() == 0x03);
  REQUIRE(rc.getTotalCycles() == 123456789);

  IIgsMemory &rm = restored.memory();
  REQUIRE(rm.read(0x020100) == 0xA5);
  REQUIRE(rm.megaII().readRAM(0x0300, false) == 0x5A);
  REQUIRE(rm.megaII().readRAM(0x0300, true) == 0xC3);
  REQUIRE(rm.shadowRegister() == 0x3F);
  REQUIRE(rm.speedRegister() == 0x80);
  REQUIRE(rm.sound().soundRam(0x0100) == 0x42);
  REQUIRE(rm.clock().batteryRam(5) == 0x99);
  REQUIRE(rm.clock().seconds() == 0x12345678);
}

TEST_CASE("A IIgs state round-trips the Mega II's soft switches", "[iigs][state]") {
  IIgsMachine machine(256 * 1024);
  machine.init(nullptr, 0);
  machine.memory().write(0xC050, 0); // graphics
  machine.memory().write(0xC057, 0); // hires
  machine.memory().write(0xC00D, 0); // 80 columns
  REQUIRE_FALSE(machine.memory().megaII().getSoftSwitches().text);

  auto state = exported(machine);
  IIgsMachine restored(256 * 1024);
  restored.init(nullptr, 0);
  REQUIRE(restored.memory().megaII().getSoftSwitches().text);
  REQUIRE(restored.importState(state.data(), state.size()));
  const auto &sw = restored.memory().megaII().getSoftSwitches();
  REQUIRE_FALSE(sw.text);
  REQUIRE(sw.hires);
  REQUIRE(sw.col80);
}

TEST_CASE("A IIgs state carries the disk in the drive", "[iigs][state]") {
  IIgsMachine machine(256 * 1024);
  machine.init(nullptr, 0);
  std::vector<uint8_t> image(143360, 0);
  image[0x100] = 0x77;
  REQUIRE(machine.insertDisk(0, image.data(), image.size(), "saved.dsk"));

  auto state = exported(machine);
  IIgsMachine restored(256 * 1024);
  restored.init(nullptr, 0);
  REQUIRE_FALSE(restored.hasDisk(0));
  REQUIRE(restored.importState(state.data(), state.size()));
  REQUIRE(restored.hasDisk(0));
  REQUIRE(std::string(restored.getDiskFilename(0)) == "saved.dsk");
  REQUIRE_FALSE(restored.hasDisk(1));
}

TEST_CASE("A IIgs refuses a state with a different amount of fast RAM", "[iigs][state]") {
  IIgsMachine small(256 * 1024);
  small.init(nullptr, 0);
  auto state = exported(small);

  IIgsMachine large(1024 * 1024);
  large.init(nullptr, 0);
  REQUIRE_FALSE(large.importState(state.data(), state.size()));
}

TEST_CASE("A IIgs refuses another machine's state and garbage", "[iigs][state]") {
  IIgsMachine machine(256 * 1024);
  machine.init(nullptr, 0);
  machine.memory().megaII().writeRAM(0x0300, 0x77, false);

  // A //e's header: the same magic, its version, its id.
  std::vector<uint8_t> iie = {'A', 'E', '2', 'S', 9, 0, 0, 0, 0, 0, 0, 0};
  iie.resize(64, 0);
  REQUIRE_FALSE(machine.importState(iie.data(), iie.size()));
  // Refused before anything was touched
  REQUIRE(machine.memory().megaII().readRAM(0x0300, false) == 0x77);

  std::vector<uint8_t> garbage(64, 0xFF);
  REQUIRE_FALSE(machine.importState(garbage.data(), garbage.size()));
  REQUIRE_FALSE(machine.importState(garbage.data(), 3));

  // Its own state, cut short
  auto state = exported(machine);
  REQUIRE_FALSE(machine.importState(state.data(), state.size() / 2));
}
