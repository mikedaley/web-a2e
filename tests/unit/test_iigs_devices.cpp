/*
 * test_iigs_devices.cpp - The ADB controller and the Ensoniq's RAM window
 *
 * Two small devices that a IIgs will not boot without, tested away from the
 * machine that needs them.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "iigs_adb.hpp"
#include "iigs_clock.hpp"
#include "iigs_scc.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include "iigs_sound.hpp"

using namespace a2e::iigs;

// ---------------------------------------------------------------------------
// ADB
// ---------------------------------------------------------------------------

TEST_CASE("The mouse reports movement and its button together", "[iigs][adb]") {
  // Seven bits of signed movement with a button in the top bit — which is
  // why a IIgs mouse cannot move more than 63 units between reports. The X
  // byte's bit is button 1, which this mouse does not have, and the Y byte's
  // is button 0: one button per byte, or a press is heard twice.
  IIgsADB adb;

  adb.queueMouse(-5, 10);
  const uint8_t x = adb.readMouseData();
  const uint8_t y = adb.readMouseData();
  REQUIRE((x & 0x80) != 0); // not pressed reads high, as everywhere else here
  REQUIRE(static_cast<int8_t>(x << 1) / 2 == -5);
  REQUIRE((y & 0x7F) == 10);

  SECTION("a pressed button reads low in the Y byte, and only there") {
    // The Finder opened a folder on a single click when the same button was
    // in both bytes: the firmware took each bit as a button of its own.
    adb.setMouseButton(true);
    adb.queueMouse(1, 1);
    REQUIRE((adb.readMouseData() & 0x80) != 0); // X: button 1, never pressed
    REQUIRE((adb.readMouseData() & 0x80) == 0); // Y: button 0, pressed
  }

  SECTION("and a shove further than a report can carry takes several") {
    adb.queueMouse(100, -100);
    REQUIRE((adb.readMouseData() & 0x7F) == 63);
    REQUIRE(static_cast<int8_t>(adb.readMouseData() << 1) / 2 == -63);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) != 0); // the rest is still owed
    REQUIRE((adb.readMouseData() & 0x7F) == 37);
    REQUIRE(static_cast<int8_t>(adb.readMouseData() << 1) / 2 == -37);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);
  }

  SECTION("movement that arrives faster than it is read adds up into one report") {
    // The host sends every twitch, hundreds a second; queued one behind the
    // other, each cost the firmware an interrupt and the pointer fell behind
    // and then leapt. Added together, the pointer catches up in one report.
    for (int i = 0; i < 20; i++) adb.queueMouse(2, -1);
    REQUIRE((adb.readMouseData() & 0x7F) == 40);
    REQUIRE(static_cast<int8_t>(adb.readMouseData() << 1) / 2 == -20);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);
  }

  SECTION("a click without any movement is a report of its own") {
    // A Finder that only heard about the button while the pointer was
    // moving could not be clicked on anything held still.
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);
    adb.setMouseButton(true);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) != 0);
    REQUIRE(adb.readMouseData() == 0x80); // X: no movement, button 1 up
    REQUIRE(adb.readMouseData() == 0x00); // Y: no movement, button 0 pressed
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);

    adb.setMouseButton(true); // still down: nothing new to say
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);

    adb.setMouseButton(false);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) != 0);
    REQUIRE(adb.readMouseData() == 0x80); // released
    REQUIRE(adb.readMouseData() == 0x80);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);
  }

  SECTION("the button is read as it was when the report was made") {
    adb.queueMouse(3, 3);
    REQUIRE((adb.readMouseData() & 0x80) != 0); // X, button up
    adb.setMouseButton(true);                    // pressed between the bytes
    REQUIRE((adb.readMouseData() & 0x80) != 0); // Y still says up
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) != 0); // and the press is the next report
  }
}

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

  // Bit 5 says the controller has put a byte in the data register, and it
  // must say so only then: the interrupt manager reads this register first
  // on every interrupt, and a bit 5 that was always set looked like an ADB
  // interrupt to service every time — so nothing underneath it was ever
  // acknowledged, and the machine took three million interrupts going nowhere.
  REQUIRE((adb.readStatus() & IIgsADB::STATUS_DATA_AVAILABLE) == 0);
  adb.writeCommand(0x0A); // a command with an answer
  REQUIRE((adb.readStatus() & IIgsADB::STATUS_DATA_AVAILABLE) != 0);
  adb.readData();
  REQUIRE((adb.readStatus() & IIgsADB::STATUS_DATA_AVAILABLE) == 0);

  SECTION("and the input queues show in it") {
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_KEYBOARD_DATA) == 0);
    adb.queueKeyboard(0x41);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_KEYBOARD_DATA) != 0);

    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);
    adb.queueMouse(16, 32);
    REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) != 0);
    REQUIRE((adb.readMouseData() & 0x7F) == 16); // X first, then Y
    REQUIRE((adb.readMouseData() & 0x7F) == 32);
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

TEST_CASE("The controller fills in the //e's keyboard registers",
          "[iigs][adb]") {
  // This is what lets //e software read the keyboard on a IIgs without knowing
  // there is a microcontroller in the way: the controller puts the key where
  // $C000 looks for it, strobe and all.
  IIgsADB adb;
  REQUIRE(adb.keyboardLatch() == 0x00);

  adb.queueKeyboard('A');
  REQUIRE(adb.keyboardLatch() == ('A' | 0x80)); // the key, with its strobe

  adb.clearKeyboardStrobe();
  REQUIRE(adb.keyboardLatch() == 'A'); // still readable, no longer new

  SECTION("and the keys queue up in order") {
    adb.queueKeyboard('B');
    adb.queueKeyboard('C');
    REQUIRE((adb.keyboardLatch() & 0x80) != 0);
    adb.clearKeyboardStrobe();
    REQUIRE(adb.keyboardLatch() == 'C');
  }

  SECTION("any-key-down is a separate line, not the strobe") {
    // A //e reports whether a key is physically held in bit 7 of $C010, and it
    // has nothing to do with whether the last key has been read.
    REQUIRE_FALSE(adb.isAnyKeyDown());
    adb.setAnyKeyDown(true);
    adb.clearKeyboardStrobe();
    REQUIRE(adb.isAnyKeyDown());
  }
}

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

TEST_CASE("$C025 says which modifier keys are down", "[iigs][adb][keyboard]") {
  // The Event Manager reads this on every event, and it used to read zero
  // whatever was held: no shift-click, no command-key menu shortcut, and
  // nothing that could recognise Control-Open-Apple-Escape.
  IIgsADB adb;
  REQUIRE(adb.peekModifiers() == 0);

  adb.setModifiers(IIgsADB::MOD_CONTROL | IIgsADB::MOD_APPLE);
  // The latch comes up with the change, which is what it is for.
  REQUIRE((adb.peekModifiers() & IIgsADB::MOD_CONTROL) != 0);
  REQUIRE((adb.peekModifiers() & IIgsADB::MOD_APPLE) != 0);
  REQUIRE((adb.peekModifiers() & IIgsADB::MOD_LATCH) != 0);

  SECTION("and a read clears the latch, leaving the keys that are still held") {
    const uint8_t read = adb.readModifiers();
    REQUIRE((read & IIgsADB::MOD_LATCH) != 0);
    REQUIRE((adb.peekModifiers() & IIgsADB::MOD_LATCH) == 0);
    REQUIRE((adb.peekModifiers() & IIgsADB::MOD_CONTROL) != 0);
  }

  SECTION("setting the same keys again does not raise the latch") {
    adb.readModifiers();
    adb.setModifiers(IIgsADB::MOD_CONTROL | IIgsADB::MOD_APPLE);
    REQUIRE((adb.peekModifiers() & IIgsADB::MOD_LATCH) == 0);
  }

  SECTION("and letting one go is a change like any other") {
    adb.readModifiers();
    adb.setModifiers(IIgsADB::MOD_CONTROL);
    REQUIRE((adb.peekModifiers() & IIgsADB::MOD_LATCH) != 0);
    REQUIRE((adb.peekModifiers() & IIgsADB::MOD_APPLE) == 0);
  }

  SECTION("a peek does not clear it, so a debugger can look") {
    REQUIRE((adb.peekModifiers() & IIgsADB::MOD_LATCH) != 0);
    REQUIRE((adb.peekModifiers() & IIgsADB::MOD_LATCH) != 0);
  }
}

TEST_CASE("Battery RAM can be kept and put back", "[iigs][clock][battery]") {
  // A real machine's battery does this, and the settings only mean anything
  // if they survive: the firmware writes its own defaults over the lot
  // whenever it does not trust the checksum, which is what a machine with a
  // dead battery does on every start.
  IIgsClock clock;
  REQUIRE(clock.batteryRamSize() == 256);

  SECTION("a write is noticed once, and only once") {
    REQUIRE_FALSE(clock.takeBatteryRamChanged());
    clock.setBatteryRam(0x1E, 0x0F);
    REQUIRE(clock.takeBatteryRamChanged());
    REQUIRE_FALSE(clock.takeBatteryRamChanged());
  }

  SECTION("writing the same byte back is not a change") {
    clock.setBatteryRam(0x1E, 0x0F);
    clock.takeBatteryRamChanged();
    clock.setBatteryRam(0x1E, 0x0F);
    REQUIRE_FALSE(clock.takeBatteryRamChanged());
  }

  SECTION("the whole of it goes out and comes back") {
    std::vector<uint8_t> kept(clock.batteryRamSize());
    for (size_t i = 0; i < kept.size(); i++) {
      kept[i] = static_cast<uint8_t>(0xA0 + (i & 0x0F));
    }
    clock.loadBatteryRam(kept.data(), kept.size());
    for (size_t i = 0; i < kept.size(); i++) {
      REQUIRE(clock.batteryRam(static_cast<uint8_t>(i)) == kept[i]);
    }
    // Putting settings back is not a change to write out again.
    REQUIRE_FALSE(clock.takeBatteryRamChanged());
  }

  SECTION("and it outlives a reset, which is what a battery is for") {
    clock.setBatteryRam(0x1E, 0x0C);
    clock.reset();
    REQUIRE(clock.batteryRam(0x1E) == 0x0C);
  }
}

TEST_CASE("The amplifier's gain is a taper, not a ratio",
          "[iigs][sound][volume]") {
  // One amplifier carries the speaker and the Ensoniq, and the nibble in
  // $C03C sets it. Treating it as a straight amplitude ratio put the machine
  // 9.5dB below a //e for the same speaker click at the volume its own
  // firmware boots with, which is not a machine sold on its sound; the taper
  // is the assumption in its place. What must not change is what the nibble
  // is for: silence at zero, full output at fifteen, and every step between
  // in order, because the ROM's bell fades by walking down it.
  REQUIRE(amplifierGain(0) == 0.0);
  REQUIRE(amplifierGain(15) == Approx(1.0));
  for (uint8_t n = 1; n < 15; n++) {
    INFO("nibble " << int(n));
    REQUIRE(amplifierGain(n) < amplifierGain(static_cast<uint8_t>(n + 1)));
  }

  SECTION("the setting the firmware boots with is within 3dB of full") {
    // 0.693 is -3.2dB, which is where a //e's speaker sits relative to a
    // IIgs at full volume. A ratio would have put it at -9.5dB.
    REQUIRE(amplifierGain(5) == Approx(0.693).margin(0.01));
  }

  SECTION("only the low nibble counts") {
    // The bits above it are the chip's, not the amplifier's.
    REQUIRE(amplifierGain(0xF5) == amplifierGain(0x05));
  }
}

TEST_CASE("The volume nibble reads back as it was written",
          "[iigs][sound][volume]") {
  // $C03C's bottom four bits are the amplifier's volume, and reading the
  // register gives what was put there. It used to read 15 whatever was
  // written, which is not what the register does and is not harmless: the
  // Control Panel's volume setting and the toolbox's SetSoundVolume both
  // change it by reading the register, altering the nibble and writing it
  // back, so all of them saw a machine claiming to be at full volume however
  // quiet it was actually set.
  IIgsSound sound;
  sound.writeControl(0x05);
  REQUIRE(sound.volume() == 5);
  REQUIRE((sound.readControl() & IIgsSound::CONTROL_VOLUME_MASK) == 5);

  SECTION("and the bits above it still say what they said") {
    sound.writeControl(IIgsSound::CONTROL_RAM | 0x03);
    REQUIRE((sound.readControl() & IIgsSound::CONTROL_RAM) != 0);
    REQUIRE((sound.readControl() & IIgsSound::CONTROL_VOLUME_MASK) == 3);
  }

  SECTION("a read-modify-write of the volume keeps the rest of the register") {
    // What the Control Panel does, and what the forced nibble broke.
    sound.writeControl(IIgsSound::CONTROL_AUTO_INCREMENT | 0x0A);
    const uint8_t held = sound.readControl();
    sound.writeControl(static_cast<uint8_t>(
        (held & ~IIgsSound::CONTROL_VOLUME_MASK) | 0x04));
    REQUIRE(sound.volume() == 4);
    REQUIRE((sound.readControl() & IIgsSound::CONTROL_AUTO_INCREMENT) != 0);
  }

  SECTION("the busy bit is the chip's and is never set here") {
    // A transfer finishes before the processor can ask about it.
    sound.writeControl(IIgsSound::CONTROL_BUSY | 0x07);
    REQUIRE((sound.readControl() & IIgsSound::CONTROL_BUSY) == 0);
    REQUIRE(sound.volume() == 7);
  }
}

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
    // The chip hands back what it fetched last time and goes for the byte at
    // the address as set, so a program sets the address, reads once to prime
    // the window, and takes its answer from the read after that. The pointer
    // moves on after each fetch, so the third read is the next byte.
    sound.setSoundRam(0x2000, 0xAA);
    sound.setSoundRam(0x2001, 0xBB);
    sound.writeAddressLow(0x00);
    sound.writeAddressHigh(0x20);

    sound.readData();                  // whatever was there before
    REQUIRE(sound.readData() == 0xAA);
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

// ---------------------------------------------------------------------------
// The clock and its battery RAM
// ---------------------------------------------------------------------------

namespace {

// The firmware's driver, byte for byte: every transfer stores what it has in
// hand to $C033 first, and the control byte says which way that byte goes —
// bit 6 clear for one going to the chip, set for one the chip supplies — with
// bit 5 holding the chip selected until the driver drops it at the end.
void sendToChip(IIgsClock &clock, uint8_t byte) {
  clock.writeData(byte);
  clock.writeControl(IIgsClock::CONTROL_TRANSACTION | IIgsClock::CONTROL_SELECT);
}

uint8_t takeFromChip(IIgsClock &clock, uint8_t junk) {
  clock.writeData(junk);
  clock.writeControl(IIgsClock::CONTROL_TRANSACTION | IIgsClock::CONTROL_READ |
                     IIgsClock::CONTROL_SELECT);
  return clock.readData();
}

void endTransaction(IIgsClock &clock) {
  clock.writeControl(static_cast<uint8_t>(clock.readControl() &
                                          ~IIgsClock::CONTROL_SELECT));
}

// Battery RAM takes three transfers: two command bytes carrying the address
// between them, and then the byte itself.
void writeBatteryRam(IIgsClock &clock, uint8_t address, uint8_t value) {
  sendToChip(clock, static_cast<uint8_t>(0x38 | (address >> 5)));
  sendToChip(clock, static_cast<uint8_t>((address & 0x1F) << 2));
  sendToChip(clock, value);
  endTransaction(clock);
}

uint8_t readBatteryRam(IIgsClock &clock, uint8_t address) {
  sendToChip(clock, static_cast<uint8_t>(0xB8 | (address >> 5)));
  sendToChip(clock, static_cast<uint8_t>((address & 0x1F) << 2));
  const uint8_t value = takeFromChip(clock, 0x00);
  endTransaction(clock);
  return value;
}

uint8_t readClockByte(IIgsClock &clock, int which, uint8_t junk = 0x00) {
  sendToChip(clock, static_cast<uint8_t>(0x81 | (which << 2)));
  const uint8_t value = takeFromChip(clock, junk);
  endTransaction(clock);
  return value;
}

void writeClockByte(IIgsClock &clock, int which, uint8_t value) {
  sendToChip(clock, static_cast<uint8_t>(0x01 | (which << 2)));
  sendToChip(clock, value);
  endTransaction(clock);
}

} // namespace

TEST_CASE("Battery RAM is addressed across three transactions",
          "[iigs][clock]") {
  // The encoding was read off a trace of the firmware rather than taken from a
  // description: the documented ones are for the chip before it grew 256 bytes
  // of RAM, and guessing from them produced a machine that stored every byte
  // at the address of the byte before.
  IIgsClock clock;

  writeBatteryRam(clock, 0x00, 0x11);
  writeBatteryRam(clock, 0x1F, 0x22);
  writeBatteryRam(clock, 0x20, 0x33); // the first address needing the top bits
  writeBatteryRam(clock, 0xFB, 0x44);

  REQUIRE(readBatteryRam(clock, 0x00) == 0x11);
  REQUIRE(readBatteryRam(clock, 0x1F) == 0x22);
  REQUIRE(readBatteryRam(clock, 0x20) == 0x33);
  REQUIRE(readBatteryRam(clock, 0xFB) == 0x44);

  SECTION("and the top three bits really are the top three") {
    // $FB is $1B with the high bits set. A decode that dropped them would put
    // both bytes in the same place, and both reads would agree — which is why
    // this checks the one that should *not* have changed.
    REQUIRE(readBatteryRam(clock, 0x1B) != 0x44);
  }

  SECTION("the battery outlives a reset, which is what a battery is for") {
    clock.reset();
    REQUIRE(readBatteryRam(clock, 0x20) == 0x33);
  }

  SECTION("the twenty bytes the old commands reach are the first twenty") {
    sendToChip(clock, 0x41 | (0x0B << 2)); // z1aaaa01: the old RAM $0B
    sendToChip(clock, 0x55);
    endTransaction(clock);
    REQUIRE(readBatteryRam(clock, 0x0B) == 0x55);

    sendToChip(clock, 0x11 | (0x02 << 1)); // z0010aa1: the old RAM $12
    sendToChip(clock, 0x66);
    endTransaction(clock);
    REQUIRE(readBatteryRam(clock, 0x12) == 0x66);
  }
}

TEST_CASE("The clock counts seconds since 1904", "[iigs][clock]") {
  IIgsClock clock;
  clock.setSeconds(0x12345678);

  // Four registers, least significant first.
  REQUIRE(readClockByte(clock, 0) == 0x78);
  REQUIRE(readClockByte(clock, 1) == 0x56);
  REQUIRE(readClockByte(clock, 2) == 0x34);
  REQUIRE(readClockByte(clock, 3) == 0x12);

  SECTION("and a byte written is a byte read back") {
    // The IIgs Diagnostic's Clock RAM Test writes each of the four and reads
    // it straight back, up to 256 times before it gives up.
    writeClockByte(clock, 1, 0xA5);
    REQUIRE(readClockByte(clock, 1) == 0xA5);
    REQUIRE(readClockByte(clock, 0) == 0x78);
    REQUIRE(clock.seconds() == 0x1234A578);
  }
}

TEST_CASE("The chip supplies a read's data byte on the read transfer, not on "
          "the command",
          "[iigs][clock]") {
  // The firmware's driver — and the Diagnostic's, which is the same routine —
  // stores whatever it is holding to $C033 before *every* transfer, the read
  // of the data byte included. A chip that put its answer in the register when
  // it saw the command had it overwritten by that store, and the Diagnostic
  // read every clock byte back as the junk it had just stored: 256 retries,
  // then the monitor.
  IIgsClock clock;
  clock.setSeconds(0x11223344);
  REQUIRE(readClockByte(clock, 0, 0xFF) == 0x44);
  REQUIRE(readClockByte(clock, 3, 0x00) == 0x11);

  writeBatteryRam(clock, 0x42, 0x99);
  sendToChip(clock, 0xB8 | (0x42 >> 5));
  sendToChip(clock, (0x42 & 0x1F) << 2);
  REQUIRE(takeFromChip(clock, 0x00) == 0x99);
  endTransaction(clock);

  SECTION("and the junk is not taken as the next command") {
    // The read transfer's stored byte, $01, is a write-seconds command if the
    // chip looks at it. It must not.
    REQUIRE(readClockByte(clock, 0, 0x01) == 0x44);
    REQUIRE(clock.seconds() == 0x11223344);
  }
}

TEST_CASE("Dropping the select line ends a transaction", "[iigs][clock]") {
  // The driver drops bit 5 after every transaction. A chip halfway through a
  // RAM address that ignored it would take the next command as that address.
  IIgsClock clock;
  writeBatteryRam(clock, 0x07, 0x77);
  sendToChip(clock, 0x38); // the first half of a RAM address...
  endTransaction(clock);   // ...abandoned
  REQUIRE(readBatteryRam(clock, 0x07) == 0x77);
}

TEST_CASE("The clock starts at the host's time and counts the machine's",
          "[iigs][clock]") {
  // Seeded from the host so the date is right, and ticked by the machine so
  // that a program which sets the time sees it move — the Diagnostic writes
  // $FFFFFFFF and waits for the roll-over, which a clock reading the host
  // would never show a machine running faster than real time.
  IIgsClock clock;
  const uint32_t host = IIgsClock::hostSecondsSince1904();
  REQUIRE(clock.seconds() - host <= 1);

  clock.setSeconds(0xFFFFFFFF);
  clock.tick();
  REQUIRE(clock.seconds() == 0);
  REQUIRE(readClockByte(clock, 0) == 0x00);
  REQUIRE(readClockByte(clock, 3) == 0x00);
}

TEST_CASE("The border colour shares the clock's control register",
          "[iigs][clock]") {
  // For no better reason than that there was a spare nibble.
  IIgsClock clock;
  clock.writeControl(0x07);
  REQUIRE(clock.borderColour() == 0x07);
}

TEST_CASE("The transaction bit clears itself when the chip is done",
          "[iigs][clock]") {
  // The firmware starts a transaction and waits for the bit to go; a chip that
  // left it set would hang the machine before it drew anything.
  IIgsClock clock;
  clock.writeData(0x38);
  clock.writeControl(IIgsClock::CONTROL_TRANSACTION);
  REQUIRE((clock.readControl() & IIgsClock::CONTROL_TRANSACTION) == 0);
}

// ---------------------------------------------------------------------------
// The synthesiser
// ---------------------------------------------------------------------------

namespace {

// Set one of the chip's registers the way a program does: through the window.
void setDocRegister(IIgsSound &sound, uint8_t reg, uint8_t value) {
  sound.writeControl(0x0F); // registers, amplifier up // the window is on the registers, not the RAM
  sound.writeAddressLow(reg);
  sound.writeAddressHigh(0x00);
  sound.writeData(value);
}

// A sound in the RAM: a square wave of `length` bytes with a zero after it,
// which is how the chip knows where it ends.
void putSquareWave(IIgsSound &sound, uint16_t at, int length) {
  for (int i = 0; i < length; i++) {
    sound.setSoundRam(static_cast<uint16_t>(at + i), i % 2 ? 0xC0 : 0x40);
  }
  sound.setSoundRam(static_cast<uint16_t>(at + length), 0x00);
}

// Run the chip for as long as the host frames represent, then take them: the
// chip runs on the machine's clock, and a buffer is what it produced meanwhile.
void render(IIgsSound &sound, std::vector<float> &samples, int frames) {
  sound.advance(static_cast<uint32_t>(frames * 1023000.0 / 48000.0));
  sound.generateSamples(samples.data(), frames, 48000);
}

float loudest(const std::vector<float> &samples) {
  float peak = 0.0f;
  for (float sample : samples) peak = std::max(peak, std::abs(sample));
  return peak;
}

} // namespace

TEST_CASE("An oscillator plays what is in the sound RAM", "[iigs][sound]") {
  IIgsSound sound;
  sound.writeControl(0x0F); // amplifier up
  putSquareWave(sound, 0x0100, 256);

  setDocRegister(sound, IIgsSound::DOC_WAVE_POINTER, 0x01); // $0100
  setDocRegister(sound, IIgsSound::DOC_VOLUME, 0xFF);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_LOW, 0x00);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_HIGH, 0x08);
  setDocRegister(sound, IIgsSound::DOC_CONTROL, 0x10); // running, free-run, left

  // A few buffers first: the amplifier takes some twenty milliseconds to
  // come up from silence.
  std::vector<float> samples(512 * 2, 0.0f);
  for (int i = 0; i < 10; i++) render(sound, samples, 512);
  REQUIRE(loudest(samples) > 0.0f);

  SECTION("and a halted one plays nothing") {
    IIgsSound silent;
    silent.writeControl(0x0F);
    putSquareWave(silent, 0x0100, 256);
    setDocRegister(silent, IIgsSound::DOC_WAVE_POINTER, 0x01);
    setDocRegister(silent, IIgsSound::DOC_VOLUME, 0xFF);
    setDocRegister(silent, IIgsSound::DOC_FREQUENCY_HIGH, 0x08);
    setDocRegister(silent, IIgsSound::DOC_CONTROL, IIgsSound::OSC_HALT);

    std::vector<float> quiet(512 * 2, 0.0f);
    render(silent, quiet, 512);
    REQUIRE(loudest(quiet) == 0.0f);
  }

  SECTION("volume scales it") {
    setDocRegister(sound, IIgsSound::DOC_VOLUME, 0x40);
    std::vector<float> quieter(512 * 2, 0.0f);
    // Twice: the resampler carries the last frame over, so the first buffer
    // after a change opens on a frame made before it.
    render(sound, quieter, 512);
    render(sound, quieter, 512);
    REQUIRE(loudest(quieter) < loudest(samples));
    REQUIRE(loudest(quieter) > 0.0f);
  }

  SECTION("and the amplifier's nibble in $C03C does not touch it") {
    // Deliberately. On the real machine the amplifier attenuates this chip
    // too, and modelling that sounded wrong: firmware and sound tools drop
    // the nibble to about 5 and put it back to 15 around every burst of DOC
    // access, in flips lasting well under ten milliseconds, so scaling the
    // chip by it turns a steady note into one wobbling at whatever rate the
    // software happens to be transferring at. The host's volume control is
    // the amplifier instead, which is what GSSquared does and for the same
    // reason. The speaker keeps the nibble, because the ROM's bell fades by
    // walking it down — test_iigs_boot.cpp pins that.
    const float atFull = loudest(samples);
    sound.writeControl(0x05);
    std::vector<float> flipped(512 * 2, 0.0f);
    for (int i = 0; i < 10; i++) render(sound, flipped, 512);
    REQUIRE(loudest(flipped) == Approx(atFull).epsilon(0.02));

    // Even at zero, which is what a game playing through a stereo card sets
    // it to: the chip is still heard.
    sound.writeControl(0x00);
    for (int i = 0; i < 10; i++) render(sound, flipped, 512);
    REQUIRE(loudest(flipped) == Approx(atFull).epsilon(0.02));
  }

  SECTION("a program that uses one speaker is heard through both") {
    // Nothing here is a stereo card: a game that puts every voice on channel
    // 0 is a mono game, and it comes out of both speakers rather than one.
    std::vector<float> both(512 * 2, 0.0f);
    render(sound, both, 512);
    float left = 0.0f, right = 0.0f;
    for (size_t i = 0; i < both.size(); i += 2) {
      left = std::max(left, std::abs(both[i]));
      right = std::max(right, std::abs(both[i + 1]));
    }
    REQUIRE(left > 0.0f);
    REQUIRE(right == left);
  }

  SECTION("nothing comes out until the machine's clock has run") {
    IIgsSound idle;
    idle.writeControl(0x0F);
    putSquareWave(idle, 0x0100, 256);
    setDocRegister(idle, IIgsSound::DOC_WAVE_POINTER, 0x01);
    setDocRegister(idle, IIgsSound::DOC_VOLUME, 0xFF);
    setDocRegister(idle, IIgsSound::DOC_FREQUENCY_HIGH, 0x08);
    setDocRegister(idle, IIgsSound::DOC_CONTROL, 0x10);
    std::vector<float> quiet(512 * 2, 0.0f);
    idle.generateSamples(quiet.data(), 512, 48000);
    REQUIRE(loudest(quiet) == 0.0f);
  }
}

TEST_CASE("A zero byte is where a sound ends", "[iigs][sound]") {
  // The chip halts an oscillator that reads one, in every mode, and firmware
  // relies on it: a sound is a run of bytes with a zero after it, and nobody
  // counts a length.
  IIgsSound sound;
  sound.writeControl(0x0F);
  putSquareWave(sound, 0x0100, 16); // ...and a zero at $0110

  setDocRegister(sound, IIgsSound::DOC_WAVE_POINTER, 0x01);
  setDocRegister(sound, IIgsSound::DOC_VOLUME, 0xFF);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_HIGH, 0x20);
  setDocRegister(sound, IIgsSound::DOC_CONTROL, IIgsSound::OSC_MODE_ONE_SHOT);
  REQUIRE_FALSE(sound.oscillatorHalted(0));

  std::vector<float> samples(2048 * 2, 0.0f);
  render(sound, samples, 2048);
  REQUIRE(sound.oscillatorHalted(0)); // it found the end and stopped

  SECTION("a free-running oscillator stops at a zero too") {
    IIgsSound looping;
    putSquareWave(looping, 0x0100, 16);
    setDocRegister(looping, IIgsSound::DOC_WAVE_POINTER, 0x01);
    setDocRegister(looping, IIgsSound::DOC_VOLUME, 0xFF);
    setDocRegister(looping, IIgsSound::DOC_FREQUENCY_HIGH, 0x20);
    setDocRegister(looping, IIgsSound::DOC_CONTROL, IIgsSound::OSC_MODE_FREE_RUN);
    render(looping, samples, 2048);
    REQUIRE(looping.oscillatorHalted(0));
  }

  SECTION("but the end of its table is where it goes round again") {
    IIgsSound looping;
    putSquareWave(looping, 0x0100, 256); // fills the 256-byte table; the zero is outside it
    setDocRegister(looping, IIgsSound::DOC_WAVE_POINTER, 0x01);
    setDocRegister(looping, IIgsSound::DOC_VOLUME, 0xFF);
    setDocRegister(looping, IIgsSound::DOC_FREQUENCY_HIGH, 0x20);
    setDocRegister(looping, IIgsSound::DOC_CONTROL, IIgsSound::OSC_MODE_FREE_RUN);
    render(looping, samples, 2048);
    REQUIRE_FALSE(looping.oscillatorHalted(0));

    // ...where a one-shot stops.
    IIgsSound once;
    putSquareWave(once, 0x0100, 256);
    setDocRegister(once, IIgsSound::DOC_WAVE_POINTER, 0x01);
    setDocRegister(once, IIgsSound::DOC_VOLUME, 0xFF);
    setDocRegister(once, IIgsSound::DOC_FREQUENCY_HIGH, 0x20);
    setDocRegister(once, IIgsSound::DOC_CONTROL, IIgsSound::OSC_MODE_ONE_SHOT);
    render(once, samples, 2048);
    REQUIRE(once.oscillatorHalted(0));
  }
}

TEST_CASE("Clearing the halt bit starts a sound from the top", "[iigs][sound]") {
  IIgsSound sound;
  putSquareWave(sound, 0x0100, 256);
  setDocRegister(sound, IIgsSound::DOC_WAVE_POINTER, 0x01);
  setDocRegister(sound, IIgsSound::DOC_VOLUME, 0xFF);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_HIGH, 0x08);
  setDocRegister(sound, IIgsSound::DOC_CONTROL, 0x00);
  sound.advance(20000);
  REQUIRE(sound.oscillator(0).accumulator != 0);

  setDocRegister(sound, IIgsSound::DOC_CONTROL, IIgsSound::OSC_HALT);
  setDocRegister(sound, IIgsSound::DOC_CONTROL, 0x00); // key on
  REQUIRE(sound.oscillator(0).accumulator == 0);
}

TEST_CASE("The chip divides its clock between the oscillators in use",
          "[iigs][sound]") {
  // One oscillator per eight ticks of 7.16MHz, with two spare slots a scan:
  // thirty-two of them make 26,320 frames a second, eight make 55,930. The
  // enable register therefore changes the pitch of everything at once.
  IIgsSound sound;
  REQUIRE(sound.activeOscillators() >= 1);

  setDocRegister(sound, IIgsSound::DOC_OSCILLATOR_ENABLE, 14); // (14/2)+1 = 8
  REQUIRE(sound.activeOscillators() == 8);
  REQUIRE(sound.sampleRate() == Approx(894886.25 / 10));

  setDocRegister(sound, IIgsSound::DOC_OSCILLATOR_ENABLE, 62);
  REQUIRE(sound.activeOscillators() == 32);
  REQUIRE(sound.sampleRate() == Approx(26320.2).epsilon(0.001));

  // A second of the slow clock is that many frames.
  sound.advance(1023000);
  std::vector<float> samples(48000 * 2, 0.0f);
  sound.generateSamples(samples.data(), 48000, 48000);
  REQUIRE(sound.oscillator(0).accumulator == 0); // halted: nothing to hear, but the clock ran
}

TEST_CASE("A swapped pair hand over to each other, and say so", "[iigs][sound][interrupt]") {
  // The sound tools play a long sample through two oscillators in swap mode:
  // when one reaches the end of its table it halts, starts its partner from
  // the top, and — with the interrupt bit set — raises an interrupt, which is
  // when the program refills the half that just finished. A chip that did
  // none of this played the first buffer and stopped.
  IIgsSound sound;
  sound.writeControl(0x0F);
  putSquareWave(sound, 0x0100, 256);
  putSquareWave(sound, 0x0200, 256);
  setDocRegister(sound, IIgsSound::DOC_OSCILLATOR_ENABLE, 2); // two oscillators

  setDocRegister(sound, IIgsSound::DOC_WAVE_POINTER, 0x01);
  setDocRegister(sound, IIgsSound::DOC_VOLUME, 0xFF);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_HIGH, 0x02);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_WAVE_POINTER + 1), 0x02);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_VOLUME + 1), 0xFF);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_FREQUENCY_HIGH + 1), 0x02);
  // Partner halted and waiting, in swap mode with interrupts.
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_CONTROL + 1),
                 IIgsSound::OSC_MODE_SWAP | IIgsSound::OSC_INTERRUPT_ENABLE | IIgsSound::OSC_HALT);
  setDocRegister(sound, IIgsSound::DOC_CONTROL,
                 IIgsSound::OSC_MODE_SWAP | IIgsSound::OSC_INTERRUPT_ENABLE);
  REQUIRE_FALSE(sound.interruptPending());

  // Run until oscillator 0 reaches the end of its 256 bytes.
  for (int i = 0; i < 200 && !sound.oscillatorHalted(0); i++) sound.advance(1000);
  REQUIRE(sound.oscillatorHalted(0));
  REQUIRE_FALSE(sound.oscillatorHalted(1)); // the partner took over
  REQUIRE(sound.interruptPending());

  // $E0 names it, active low, and reading clears it.
  sound.writeControl(0x00); // registers
  sound.writeAddressLow(IIgsSound::DOC_INTERRUPT);
  sound.readData();
  const uint8_t status = sound.readData();
  REQUIRE((status & 0x80) == 0);
  REQUIRE(((status >> 1) & 0x1F) == 0);
  REQUIRE_FALSE(sound.interruptPending());
  sound.readData();
  REQUIRE((sound.readData() & 0x80) != 0);

  // And the partner, in its turn, hands back and interrupts.
  for (int i = 0; i < 200 && !sound.oscillatorHalted(1); i++) sound.advance(1000);
  REQUIRE(sound.oscillatorHalted(1));
  REQUIRE_FALSE(sound.oscillatorHalted(0));
  REQUIRE(sound.interruptPending());

  SECTION("a halt the program writes without the interrupt bit is not reported") {
    sound.writeAddressLow(IIgsSound::DOC_INTERRUPT);
    sound.readData(); sound.readData(); // clear
    REQUIRE_FALSE(sound.interruptPending());
    setDocRegister(sound, IIgsSound::DOC_CONTROL, IIgsSound::OSC_MODE_SWAP | IIgsSound::OSC_HALT);
    REQUIRE_FALSE(sound.interruptPending());
  }
}

TEST_CASE("Oscillators on different channels are summed, not split",
          "[iigs][sound]") {
  // The chip has one analogue output pin. It visits its channels in turn and
  // puts each one's sample on that same pin, with the channel strobes saying
  // which channel is on it; a stock machine low-pass filters the pin and
  // hears the sum. Only a stereo card in a slot uses the strobes to pull the
  // channels apart, and there is none here.
  //
  // Splitting by the channel field instead put a game's bass in one speaker
  // and its melody in the other: Spy Hunter played one or the other rather
  // than both.
  IIgsSound sound;
  sound.writeControl(0x0F);
  putSquareWave(sound, 0x0100, 256);
  // Three enabled, the third halted: the uppermost enabled oscillator is
  // heard three times over on the real silicon, and that would tilt the
  // comparison below.
  setDocRegister(sound, IIgsSound::DOC_OSCILLATOR_ENABLE, 4);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_CONTROL + 2),
                 IIgsSound::OSC_HALT);

  // One voice on channel 1 and one on channel 0, which is how a game assigns
  // a bass and a melody.
  setDocRegister(sound, IIgsSound::DOC_WAVE_POINTER, 0x01);
  setDocRegister(sound, IIgsSound::DOC_VOLUME, 0xFF);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_HIGH, 0x08);
  setDocRegister(sound, IIgsSound::DOC_CONTROL, 0x10); // channel 1
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_CONTROL + 1), 0x00);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_WAVE_POINTER + 1), 0x01);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_VOLUME + 1), 0xFF);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_FREQUENCY_HIGH + 1), 0x08);

  std::vector<float> samples(2048 * 2, 0.0f);
  for (int i = 0; i < 4; i++) render(sound, samples, 2048);

  float left = 0.0f, right = 0.0f;
  for (size_t i = 0; i < samples.size(); i += 2) {
    left = std::max(left, std::abs(samples[i]));
    right = std::max(right, std::abs(samples[i + 1]));
  }
  // Both voices, in both speakers, at the same level.
  REQUIRE(left > 0.0f);
  REQUIRE(left == Approx(right));

  SECTION("and moving one to the other channel changes nothing") {
    const float before = left;
    setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_CONTROL + 1), 0x10);
    for (int i = 0; i < 4; i++) render(sound, samples, 2048);
    float after = 0.0f;
    for (size_t i = 0; i < samples.size(); i += 2) {
      after = std::max(after, std::abs(samples[i]));
    }
    REQUIRE(after == Approx(before));
  }

  SECTION("and two voices are louder than one") {
    // The sum is the point: silencing one has to be audible as a drop.
    setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_CONTROL + 1),
                   IIgsSound::OSC_HALT);
    for (int i = 0; i < 4; i++) render(sound, samples, 2048);
    float alone = 0.0f;
    for (size_t i = 0; i < samples.size(); i += 2) {
      alone = std::max(alone, std::abs(samples[i]));
    }
    REQUIRE(alone < left * 0.75f);
    REQUIRE(alone > 0.0f);
  }
}

TEST_CASE("The ADB controller interrupts only when asked to, and only for what it has",
          "[iigs][adb][interrupt]") {
  // Bit 6 of $C027 lets the mouse interrupt and bit 3 the keyboard; the full
  // bits beside them are the controller's. GS/OS sets bit 6 and then waits
  // for the interrupt, and a machine that never raised one had a Finder that
  // drew its desktop and never noticed the mouse.
  IIgsADB adb;
  REQUIRE_FALSE(adb.interruptPending());

  adb.queueMouse(5, -3);
  REQUIRE_FALSE(adb.interruptPending()); // data, but nobody asked

  adb.writeStatus(IIgsADB::STATUS_MOUSE_INTERRUPT);
  REQUIRE(adb.interruptPending());
  REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_INTERRUPT) != 0); // the enable reads back
  REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_Y_NEXT) == 0);    // X first
  adb.readMouseData();
  REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_Y_NEXT) != 0);    // then Y
  REQUIRE(adb.interruptPending());                                     // still a byte to go
  adb.readMouseData();
  REQUIRE_FALSE(adb.interruptPending());                               // and now it is served

  // Only the enables are the processor's to write: a write cannot invent
  // data, and the full bits are not disturbed by it.
  adb.writeStatus(0xFF);
  REQUIRE((adb.readStatus() & IIgsADB::STATUS_MOUSE_DATA) == 0);
  REQUIRE_FALSE(adb.interruptPending());

  adb.writeStatus(IIgsADB::STATUS_KEYBOARD_INTERRUPT);
  adb.queueKeyboard(0x41);
  REQUIRE(adb.interruptPending());
  adb.clearKeyboardStrobe();
  REQUIRE_FALSE(adb.interruptPending());
}

TEST_CASE("The Ensoniq's interrupt register says no oscillator is asking",
          "[iigs][sound][interrupt]") {
  // Register $E0 is active low: bit 7 clear means an oscillator interrupted.
  // With none having done so it must say so rather than read back the zero
  // it was never written with — the ROM's interrupt manager asks it on every
  // interrupt it cannot otherwise place, and a zero here is "Unclaimed Sound
  // Interrupt" on a machine whose sound chip has done nothing.
  IIgsSound sound;
  sound.writeControl(0x00);        // registers, not RAM
  sound.writeAddressHigh(0x00);
  sound.writeAddressLow(0xE0);
  sound.readData();                // the window is one behind
  REQUIRE((sound.readData() & 0x80) != 0);
  REQUIRE_FALSE(sound.interruptPending());
}

// ---------------------------------------------------------------------------
// The SCC
// ---------------------------------------------------------------------------

namespace {

constexpr uint8_t SCC_CMD_B = 0, SCC_CMD_A = 1, SCC_DATA_B = 2, SCC_DATA_A = 3;

// The chip's own convention: a write to the command register with the
// pointer at zero picks a register, and the next access reaches it.
void sccWrite(IIgsSCC &scc, uint8_t command, int reg, uint8_t value) {
  scc.write(command, static_cast<uint8_t>(reg & 0x0F) | (reg >= 8 ? 0x08 : 0x00));
  scc.write(command, value);
}

uint8_t sccRead(IIgsSCC &scc, uint8_t command, int reg) {
  scc.write(command, static_cast<uint8_t>(reg & 0x0F) | (reg >= 8 ? 0x08 : 0x00));
  return scc.read(command);
}

// The Diagnostic's set-up for its loop test: 600 baud, x16 clock, eight bits,
// receiver and transmitter on, local loopback with the generator running.
void sccLoopback(IIgsSCC &scc, uint8_t command) {
  sccWrite(scc, command, 4, 0x4C);
  sccWrite(scc, command, 11, 0xD0);
  sccWrite(scc, command, 12, 0xBE);
  sccWrite(scc, command, 13, 0x00);
  sccWrite(scc, command, 14, 0x13);
  sccWrite(scc, command, 3, 0xC1);
  sccWrite(scc, command, 5, 0x6A);
}

} // namespace

TEST_CASE("The SCC's command register is a pointer into a register file",
          "[iigs][scc]") {
  IIgsSCC scc;
  // The interrupt vector reads back as written, on either channel: the
  // Diagnostic writes every value from $00 down and reads each one back.
  sccWrite(scc, SCC_CMD_A, 2, 0xFE);
  REQUIRE(sccRead(scc, SCC_CMD_A, 2) == 0xFE);
  sccWrite(scc, SCC_CMD_B, 12, 0x5A);
  REQUIRE(sccRead(scc, SCC_CMD_B, 12) == 0x5A);
  REQUIRE(sccRead(scc, SCC_CMD_A, 12) != 0x5A); // its own register per channel

  SECTION("the pointer goes back to zero after one access") {
    scc.write(SCC_CMD_A, 0x0F);
    scc.read(SCC_CMD_A); // RR15
    REQUIRE((scc.read(SCC_CMD_A) & IIgsSCC::RR0_TX_EMPTY) != 0); // RR0 again
  }

  SECTION("a hardware reset through WR9 puts the file back") {
    sccWrite(scc, SCC_CMD_A, 9, 0xC0);
    REQUIRE(sccRead(scc, SCC_CMD_A, 2) == 0x00);
    REQUIRE(sccRead(scc, SCC_CMD_A, 15) == 0xF8);
  }
}

TEST_CASE("The SCC is quiet until something is asked of it", "[iigs][scc]") {
  // The ROM's interrupt manager asks RR3 first on every interrupt and takes
  // any set bit as a serial interrupt; a fresh chip must have none.
  IIgsSCC scc;
  REQUIRE(sccRead(scc, SCC_CMD_A, 3) == 0x00);
  REQUIRE_FALSE(scc.interruptPending());
  REQUIRE((sccRead(scc, SCC_CMD_A, 0) & IIgsSCC::RR0_TX_EMPTY) != 0);
  REQUIRE(scc.read(SCC_DATA_A) == 0x00);
}

TEST_CASE("The SCC's baud rate generator counts to zero, and that interrupts",
          "[iigs][scc]") {
  // The Diagnostic's internal test, step for step: reset, MIE, the slowest
  // time constant there is, the zero count enabled in WR15 and ext/status
  // interrupts in WR1, then the generator switched on. It then measures the
  // interval between two interrupts against a window a little either side
  // of 17.8ms — (TC + 2) clocks of 3.6864MHz, not twice that: the counter's
  // output toggles at each zero, so the baud rate is half the zero count.
  IIgsSCC scc;
  sccWrite(scc, SCC_CMD_A, 9, 0xC0);
  sccWrite(scc, SCC_CMD_A, 9, 0x0A);
  sccWrite(scc, SCC_CMD_A, 12, 0xFF);
  sccWrite(scc, SCC_CMD_A, 13, 0xFF);
  sccWrite(scc, SCC_CMD_A, 15, 0x02);
  scc.write(SCC_CMD_A, 0x10); // reset ext/status
  sccWrite(scc, SCC_CMD_A, 1, 0x01);
  sccWrite(scc, SCC_CMD_A, 14, 0x03);

  auto cyclesToInterrupt = [&]() {
    uint32_t cycles = 0;
    while (!scc.interruptPending() && cycles < 100000) {
      scc.advance(10);
      cycles += 10;
    }
    return cycles;
  };
  const uint32_t first = cyclesToInterrupt();
  INFO("first zero count after " << first << " cycles");
  REQUIRE(first > 17000);
  REQUIRE(first < 19500);
  REQUIRE((sccRead(scc, SCC_CMD_A, 3) & IIgsSCC::RR3_A_EXT) != 0);
  // RR0 is frozen with the zero count up until the CPU acknowledges.
  REQUIRE((sccRead(scc, SCC_CMD_A, 0) & IIgsSCC::RR0_ZERO_COUNT) != 0);

  scc.write(SCC_CMD_A, 0x10);
  REQUIRE_FALSE(scc.interruptPending());
  REQUIRE((sccRead(scc, SCC_CMD_A, 0) & IIgsSCC::RR0_ZERO_COUNT) == 0);

  const uint32_t second = cyclesToInterrupt();
  INFO("next zero count after " << second << " cycles");
  REQUIRE(second > 17000);
  REQUIRE(second < 19500);

  SECTION("and master interrupt enable is what puts it on the line") {
    sccWrite(scc, SCC_CMD_A, 9, 0x02);
    REQUIRE_FALSE(scc.interruptPending());
    REQUIRE((sccRead(scc, SCC_CMD_A, 3) & IIgsSCC::RR3_A_EXT) != 0); // still asking
  }
}

TEST_CASE("A byte sent round the SCC's local loop comes back", "[iigs][scc]") {
  // The second half of the Diagnostic's internal test: with local loopback
  // on it writes a byte, waits for RR1's all-sent, and expects RR0 to say a
  // byte is waiting and the data register to give the same byte back.
  IIgsSCC scc;
  sccWrite(scc, SCC_CMD_A, 9, 0xC0);
  sccLoopback(scc, SCC_CMD_A);
  REQUIRE((sccRead(scc, SCC_CMD_A, 0) & IIgsSCC::RR0_RX_AVAILABLE) == 0);

  scc.write(SCC_DATA_A, 0xA5);
  REQUIRE((sccRead(scc, SCC_CMD_A, 1) & 0x01) == 0); // not all sent yet
  uint32_t cycles = 0;
  while ((sccRead(scc, SCC_CMD_A, 1) & 0x01) == 0 && cycles < 100000) {
    scc.advance(100);
    cycles += 100;
  }
  // Ten bits at 600 baud is 16.7ms.
  INFO("all sent after " << cycles << " cycles");
  REQUIRE(cycles > 15000);
  REQUIRE(cycles < 19000);
  REQUIRE((sccRead(scc, SCC_CMD_A, 0) & IIgsSCC::RR0_RX_AVAILABLE) != 0);
  REQUIRE(scc.read(SCC_DATA_A) == 0xA5);
  REQUIRE((sccRead(scc, SCC_CMD_A, 0) & IIgsSCC::RR0_RX_AVAILABLE) == 0);

  SECTION("channel B has its own loop") {
    sccLoopback(scc, SCC_CMD_B);
    scc.write(SCC_DATA_B, 0x3C);
    for (int i = 0; i < 200; i++) scc.advance(100);
    REQUIRE(scc.read(SCC_DATA_B) == 0x3C);
    REQUIRE(scc.read(SCC_DATA_A) != 0x3C);
  }

  SECTION("a receive interrupt is raised and cleared with the byte") {
    sccWrite(scc, SCC_CMD_A, 9, 0x0A);
    sccWrite(scc, SCC_CMD_A, 1, 0x10); // interrupt on every character
    scc.write(SCC_DATA_A, 0x42);
    for (int i = 0; i < 200; i++) scc.advance(100);
    REQUIRE(scc.interruptPending());
    REQUIRE((sccRead(scc, SCC_CMD_A, 3) & IIgsSCC::RR3_A_RX) != 0);
    // RR2 as channel B reads it names the source: channel A receive, 110.
    REQUIRE((sccRead(scc, SCC_CMD_B, 2) & 0x0E) == 0x0C);
    REQUIRE(scc.read(SCC_DATA_A) == 0x42);
    REQUIRE_FALSE(scc.interruptPending());
  }
}

TEST_CASE("A loopback cable joins the two ports", "[iigs][scc]") {
  // The Diagnostic's External Serial Ports Test: send on one port and expect
  // the byte on the other, both ways, through a cable that crosses transmit
  // and receive. Fitted by default, because nothing else is ever plugged in.
  IIgsSCC scc;
  REQUIRE(scc.hasLoopbackCable());
  sccWrite(scc, SCC_CMD_A, 9, 0xC0);
  for (uint8_t command : {SCC_CMD_A, SCC_CMD_B}) {
    sccWrite(scc, command, 4, 0x4C);
    sccWrite(scc, command, 11, 0xD0);
    sccWrite(scc, command, 12, 0xBE);
    sccWrite(scc, command, 13, 0x00);
    sccWrite(scc, command, 14, 0x03); // the generator, and no local loop
    sccWrite(scc, command, 3, 0xC1);
    sccWrite(scc, command, 5, 0x6A);
  }
  scc.write(SCC_DATA_A, 0x5A);
  for (int i = 0; i < 200; i++) scc.advance(100);
  REQUIRE((sccRead(scc, SCC_CMD_A, 0) & IIgsSCC::RR0_RX_AVAILABLE) == 0);
  REQUIRE((sccRead(scc, SCC_CMD_B, 0) & IIgsSCC::RR0_RX_AVAILABLE) != 0);
  REQUIRE(scc.read(SCC_DATA_B) == 0x5A);

  scc.write(SCC_DATA_B, 0xC3);
  for (int i = 0; i < 200; i++) scc.advance(100);
  REQUIRE(scc.read(SCC_DATA_A) == 0xC3);

  SECTION("and crosses the handshake lines: DTR out is CTS in") {
    REQUIRE((sccRead(scc, SCC_CMD_B, 0) & IIgsSCC::RR0_CTS) == 0); // $6A leaves DTR down
    sccWrite(scc, SCC_CMD_A, 5, 0xEA);
    scc.write(SCC_CMD_B, 0x10); // acknowledge the change, so RR0 is live again
    REQUIRE((sccRead(scc, SCC_CMD_B, 0) & IIgsSCC::RR0_CTS) != 0);
    sccWrite(scc, SCC_CMD_A, 5, 0x6A);
    scc.write(SCC_CMD_B, 0x10);
    REQUIRE((sccRead(scc, SCC_CMD_B, 0) & IIgsSCC::RR0_CTS) == 0);
  }

  SECTION("unplugged, a byte goes nowhere") {
    scc.setLoopbackCable(false);
    scc.write(SCC_DATA_A, 0x11);
    for (int i = 0; i < 200; i++) scc.advance(100);
    REQUIRE((sccRead(scc, SCC_CMD_B, 0) & IIgsSCC::RR0_RX_AVAILABLE) == 0);
    REQUIRE((sccRead(scc, SCC_CMD_B, 0) & IIgsSCC::RR0_CTS) == 0);
  }
}

TEST_CASE("The SCC's transmitter is clocked from whatever WR11 selects",
          "[iigs][scc]") {
  // The Diagnostic's Serial Crystal Test clocks a byte straight from the
  // 3.6864MHz crystal at x64 — 174 microseconds for ten bits, 178 cycles —
  // and times its all-sent. A transmitter that took the generator's rate
  // whatever WR11 said took a fifth of a second over it.
  IIgsSCC scc;
  sccWrite(scc, SCC_CMD_B, 9, 0xC0);
  sccWrite(scc, SCC_CMD_B, 4, 0xC4);
  sccWrite(scc, SCC_CMD_B, 11, 0x80);
  sccWrite(scc, SCC_CMD_B, 12, 0xFF);
  sccWrite(scc, SCC_CMD_B, 13, 0xFF);
  sccWrite(scc, SCC_CMD_B, 14, 0xB3);
  sccWrite(scc, SCC_CMD_B, 3, 0xC1);
  sccWrite(scc, SCC_CMD_B, 5, 0x62);
  scc.write(SCC_DATA_B, 0x55);
  sccWrite(scc, SCC_CMD_B, 5, 0x6A);
  uint32_t cycles = 0;
  while ((sccRead(scc, SCC_CMD_B, 1) & 0x01) == 0 && cycles < 10000) {
    scc.advance(2);
    cycles += 2;
  }
  INFO("all sent after " << cycles << " cycles");
  REQUIRE(cycles >= 170);
  REQUIRE(cycles <= 190);
}
