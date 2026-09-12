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

  SECTION("and so does the amplifier's nibble in $C03C, once it has settled") {
    // The volume control is analogue and takes some twenty milliseconds to
    // follow a step, so a program's flips around a transfer are a slope
    // rather than a chop. Give it a tenth of a second.
    sound.writeControl(0x05);
    std::vector<float> quieter(512 * 2, 0.0f);
    for (int i = 0; i < 10; i++) render(sound, quieter, 512);
    REQUIRE(quieter[0] != 0.0f);
    REQUIRE(loudest(quieter) < loudest(samples) * 0.4f);
    REQUIRE(loudest(quieter) > loudest(samples) * 0.25f);
    sound.writeControl(0x00);
    for (int i = 0; i < 20; i++) render(sound, quieter, 512);
    REQUIRE(loudest(quieter) < 0.001f);
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

TEST_CASE("Oscillators are split between the two speakers", "[iigs][sound]") {
  // Channel bit 0 picks the speaker; the stereo cards of the day put the odd
  // channels on the left, so channel 1 is left and channel 0 is right.
  IIgsSound sound;
  sound.writeControl(0x0F);
  putSquareWave(sound, 0x0100, 256);
  // Three enabled, the third halted: the last enabled oscillator is heard
  // three times over, and that would tilt the comparison.
  setDocRegister(sound, IIgsSound::DOC_OSCILLATOR_ENABLE, 4);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_CONTROL + 2), IIgsSound::OSC_HALT);

  setDocRegister(sound, IIgsSound::DOC_WAVE_POINTER, 0x01);
  setDocRegister(sound, IIgsSound::DOC_VOLUME, 0xFF);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_HIGH, 0x08);
  setDocRegister(sound, IIgsSound::DOC_CONTROL, 0x10); // channel 1
  // A second voice on channel 0, at a different volume so the two sides can
  // be told apart: with both sides in use the program is asking for stereo.
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_CONTROL + 1), 0x00);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_WAVE_POINTER + 1), 0x01);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_VOLUME + 1), 0x40);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_FREQUENCY_HIGH + 1), 0x08);

  std::vector<float> samples(2048 * 2, 0.0f);
  for (int i = 0; i < 4; i++) render(sound, samples, 2048); // let the amplifier settle

  float left = 0.0f, right = 0.0f;
  for (size_t i = 0; i < samples.size(); i += 2) {
    left = std::max(left, std::abs(samples[i]));
    right = std::max(right, std::abs(samples[i + 1]));
  }
  REQUIRE(left > 0.0f);
  REQUIRE(right > 0.0f);
  REQUIRE(left > right * 2.0f); // channel 1's full-volume voice is the left one
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
