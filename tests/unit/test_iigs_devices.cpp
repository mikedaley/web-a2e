/*
 * test_iigs_devices.cpp - The ADB controller and the Ensoniq's RAM window
 *
 * Two small devices that a IIgs will not boot without, tested away from the
 * machine that needs them.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "iigs_adb.hpp"
#include "iigs_sound.hpp"

using namespace a2e::iigs;

// ---------------------------------------------------------------------------
// ADB
// ---------------------------------------------------------------------------

TEST_CASE("The ADB controller answers the commands the firmware asks",
          "[iigs][adb]") {
  IIgsADB adb;

  SECTION("a command with no arguments is finished the moment it arrives") {
    adb.writeCommand(0x0D); // Version
    REQUIRE(adb.lastCommand() == 0x0D);
    REQUIRE(adb.hasResponse());
    REQUIRE(adb.readData() != 0x00); // It has a version, whatever it is
    REQUIRE_FALSE(adb.hasResponse());
  }

  SECTION("a command with arguments collects them before it acts") {
    // SYNC is four bytes of modes and configuration on a ROM 01, and the
    // controller is busy until it has all of them.
    adb.writeCommand(0x07);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_COMMAND_FULL) != 0);
    adb.writeCommand(0x00);
    adb.writeCommand(0x32);
    adb.writeCommand(0x00);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_COMMAND_FULL) != 0);
    adb.writeCommand(0x24);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_COMMAND_FULL) == 0);

    // ...and then it knows what it was told.
    adb.writeCommand(0x0A); // Read modes
    REQUIRE(adb.readData() == 0x00);
    adb.writeCommand(0x0B); // Read configuration
    REQUIRE(adb.readData() == 0x32);
    REQUIRE(adb.readData() == 0x00);
    REQUIRE(adb.readData() == 0x24);
  }

  SECTION("its memory can be written and read back") {
    adb.writeCommand(0x08); // Write memory
    adb.writeCommand(0x40); // ...at $40
    adb.writeCommand(0x5A);

    adb.writeCommand(0x09); // Read memory
    adb.writeCommand(0x40);
    REQUIRE(adb.readData() == 0x5A);
  }

  SECTION("modes are set and cleared a bit at a time") {
    adb.writeCommand(0x04);
    adb.writeCommand(0x03); // set two
    adb.writeCommand(0x05);
    adb.writeCommand(0x01); // clear one
    adb.writeCommand(0x0A);
    REQUIRE(adb.readData() == 0x02);
  }

  SECTION("an unknown command is taken and ignored, not refused") {
    // Most of the command space addresses devices on the bus rather than the
    // controller. Nothing is attached yet, and a controller that stopped
    // answering would hang the machine.
    adb.writeCommand(0x73);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_COMMAND_FULL) == 0);
    REQUIRE(adb.readData() == 0x00);
  }
}

TEST_CASE("The ADB status register is what the firmware polls", "[iigs][adb]") {
  IIgsADB adb;

  // Bit 5 is the one it waits for before every exchange, and it is always set:
  // the controller is ready to be read at any time.
  REQUIRE((adb.readStatus() & IIgsADB::STATUS_DATA_AVAILABLE) != 0);

  SECTION("and the input queues show in it") {
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_KEYBOARD_DATA) == 0);
    adb.queueKeyboard(0x41);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_KEYBOARD_DATA) != 0);

    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);
    adb.queueMouse(0x10, 0x20);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) != 0);
    REQUIRE(adb.readMouseData() == 0x10); // X first, then Y
    REQUIRE(adb.readMouseData() == 0x20);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);
  }

  SECTION("a reset empties everything") {
    adb.queueKeyboard(0x41);
    adb.writeCommand(0x0D);
    adb.reset();
    REQUIRE_FALSE(adb.hasResponse());
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_KEYBOARD_DATA) == 0);
  }
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

TEST_CASE("The Ensoniq's RAM is reached a byte at a time", "[iigs][sound]") {
  IIgsSound sound;
  sound.writeControl(IIgsSound::CONTROL_RAM | IIgsSound::CONTROL_AUTO_INCREMENT);

  SECTION("writing walks the pointer along") {
    sound.writeAddressLow(0x00);
    sound.writeAddressHigh(0x10);
    sound.writeData(0x11);
    sound.writeData(0x22);
    sound.writeData(0x33);

    REQUIRE(sound.soundRam(0x1000) == 0x11);
    REQUIRE(sound.soundRam(0x1001) == 0x22);
    REQUIRE(sound.soundRam(0x1002) == 0x33);
    REQUIRE(sound.address() == 0x1003);
  }

  SECTION("and reading is one behind, which is what firmware expects") {
    // The chip hands back what it fetched last time and goes for the next, so
    // a program sets the address, reads once to prime the window, and takes
    // its answer from the read after that.
    sound.setSoundRam(0x2000, 0xAA);
    sound.setSoundRam(0x2001, 0xBB);
    sound.writeAddressLow(0x00);
    sound.writeAddressHigh(0x20);

    REQUIRE(sound.readData() == 0xAA); // primed by setting the address
    REQUIRE(sound.readData() == 0xBB);
  }

  SECTION("without auto-increment the pointer stays put") {
    sound.writeControl(IIgsSound::CONTROL_RAM);
    sound.writeAddressLow(0x34);
    sound.writeAddressHigh(0x12);
    sound.writeData(0x77);
    sound.writeData(0x88);
    REQUIRE(sound.soundRam(0x1234) == 0x88);
    REQUIRE(sound.address() == 0x1234);
  }

  SECTION("with the RAM bit clear the window is on the chip's registers") {
    sound.writeControl(IIgsSound::CONTROL_AUTO_INCREMENT);
    sound.writeAddressLow(0x00);
    sound.writeAddressHigh(0x00);
    sound.writeData(0x5A);
    REQUIRE(sound.docRegister(0x00) == 0x5A);
    REQUIRE(sound.soundRam(0x0000) == 0x00); // and not in the RAM
  }

  SECTION("volume is the bottom four bits of the control byte") {
    sound.writeControl(0x0B);
    REQUIRE(sound.volume() == 0x0B);
  }
}
