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
  // Seven bits of signed movement with the button in the top bit — which is
  // why a IIgs mouse cannot move more than 63 units between reports, and why
  // the button comes back twice for every movement.
  IIgsADB adb;

  adb.queueMouse(-5, 10);
  const uint8_t x = adb.readMouseData();
  const uint8_t y = adb.readMouseData();
  REQUIRE((x & 0x80) != 0); // not pressed reads high, as everywhere else here
  REQUIRE(static_cast<int8_t>(x << 1) / 2 == -5);
  REQUIRE((y & 0x7F) == 10);

  SECTION("a pressed button reads low, in both bytes") {
    adb.setMouseButton(true);
    adb.queueMouse(1, 1);
    REQUIRE((adb.readMouseData() & 0x80) == 0);
    REQUIRE((adb.readMouseData() & 0x80) == 0);
  }

  SECTION("and a shove further than the report can carry is clamped") {
    adb.queueMouse(500, -500);
    REQUIRE((adb.readMouseData() & 0x7F) == 63);
    REQUIRE(static_cast<int8_t>(adb.readMouseData() << 1) / 2 == -63);
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

  // Bit 5 is the one it waits for before every exchange, and it is always set:
  // the controller is ready to be read at any time.
  REQUIRE((adb.readStatus() & IIgsADB::STATUS_DATA_AVAILABLE) != 0);

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

// ---------------------------------------------------------------------------
// The clock and its battery RAM
// ---------------------------------------------------------------------------

namespace {

// A transaction is a byte in the data register and a nudge to the control one.
void send(IIgsClock &clock, uint8_t byte) {
  clock.writeData(byte);
  clock.writeControl(IIgsClock::CONTROL_TRANSACTION);
}

// Battery RAM takes three of them: two command bytes carrying the address
// between them, and then the byte itself.
void writeBatteryRam(IIgsClock &clock, uint8_t address, uint8_t value) {
  send(clock, static_cast<uint8_t>(0x38 | (address >> 5)));
  send(clock, static_cast<uint8_t>((address & 0x1F) << 2));
  send(clock, value);
}

uint8_t readBatteryRam(IIgsClock &clock, uint8_t address) {
  send(clock, static_cast<uint8_t>(0xB8 | (address >> 5)));
  send(clock, static_cast<uint8_t>((address & 0x1F) << 2));
  send(clock, 0x00);
  return clock.readData();
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
}

TEST_CASE("The clock counts seconds since 1904", "[iigs][clock]") {
  IIgsClock clock;
  clock.setSeconds(0x12345678);

  // Four registers, least significant first.
  send(clock, 0x81);
  REQUIRE(clock.readData() == 0x78);
  send(clock, 0x85);
  REQUIRE(clock.readData() == 0x56);
  send(clock, 0x89);
  REQUIRE(clock.readData() == 0x34);
  send(clock, 0x8D);
  REQUIRE(clock.readData() == 0x12);
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
  sound.writeControl(0x00); // the window is on the registers, not the RAM
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

float loudest(const std::vector<float> &samples) {
  float peak = 0.0f;
  for (float sample : samples) peak = std::max(peak, std::abs(sample));
  return peak;
}

} // namespace

TEST_CASE("An oscillator plays what is in the sound RAM", "[iigs][sound]") {
  IIgsSound sound;
  putSquareWave(sound, 0x0100, 256);

  setDocRegister(sound, IIgsSound::DOC_WAVE_POINTER, 0x01); // $0100
  setDocRegister(sound, IIgsSound::DOC_VOLUME, 0xFF);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_LOW, 0x00);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_HIGH, 0x08);
  setDocRegister(sound, IIgsSound::DOC_CONTROL, 0x00); // running, free-run, left

  std::vector<float> samples(512 * 2, 0.0f);
  sound.generateSamples(samples.data(), 512, 48000);

  REQUIRE(loudest(samples) > 0.0f);

  SECTION("and a halted one plays nothing") {
    IIgsSound silent;
    putSquareWave(silent, 0x0100, 256);
    setDocRegister(silent, IIgsSound::DOC_WAVE_POINTER, 0x01);
    setDocRegister(silent, IIgsSound::DOC_VOLUME, 0xFF);
    setDocRegister(silent, IIgsSound::DOC_FREQUENCY_HIGH, 0x08);
    setDocRegister(silent, IIgsSound::DOC_CONTROL, IIgsSound::OSC_HALT);

    std::vector<float> quiet(512 * 2, 0.0f);
    silent.generateSamples(quiet.data(), 512, 48000);
    REQUIRE(loudest(quiet) == 0.0f);
  }

  SECTION("volume scales it") {
    setDocRegister(sound, IIgsSound::DOC_VOLUME, 0x40);
    std::vector<float> quieter(512 * 2, 0.0f);
    sound.generateSamples(quieter.data(), 512, 48000);
    REQUIRE(loudest(quieter) < loudest(samples));
    REQUIRE(loudest(quieter) > 0.0f);
  }
}

TEST_CASE("A zero byte is where a sound ends", "[iigs][sound]") {
  // The chip halts an oscillator that reads one, and firmware relies on it: a
  // sound is a run of bytes with a zero after it, and nobody counts a length.
  IIgsSound sound;
  putSquareWave(sound, 0x0100, 16); // ...and a zero at $0110

  setDocRegister(sound, IIgsSound::DOC_WAVE_POINTER, 0x01);
  setDocRegister(sound, IIgsSound::DOC_VOLUME, 0xFF);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_HIGH, 0x20);
  setDocRegister(sound, IIgsSound::DOC_CONTROL, IIgsSound::OSC_MODE_ONE_SHOT);
  REQUIRE_FALSE(sound.oscillatorHalted(0));

  std::vector<float> samples(2048 * 2, 0.0f);
  sound.generateSamples(samples.data(), 2048, 48000);

  REQUIRE(sound.oscillatorHalted(0)); // it found the end and stopped

  SECTION("a free-running oscillator starts again instead") {
    IIgsSound looping;
    putSquareWave(looping, 0x0100, 16);
    setDocRegister(looping, IIgsSound::DOC_WAVE_POINTER, 0x01);
    setDocRegister(looping, IIgsSound::DOC_VOLUME, 0xFF);
    setDocRegister(looping, IIgsSound::DOC_FREQUENCY_HIGH, 0x20);
    setDocRegister(looping, IIgsSound::DOC_CONTROL, IIgsSound::OSC_MODE_FREE_RUN);

    std::vector<float> long_(2048 * 2, 0.0f);
    looping.generateSamples(long_.data(), 2048, 48000);
    REQUIRE_FALSE(looping.oscillatorHalted(0));
  }
}

TEST_CASE("The chip divides its clock between the oscillators in use",
          "[iigs][sound]") {
  // Eight running oscillators each step twice as often as sixteen would, which
  // is why the enable register changes the pitch of everything at once.
  IIgsSound sound;
  REQUIRE(sound.activeOscillators() >= 1);

  setDocRegister(sound, IIgsSound::DOC_OSCILLATOR_ENABLE, 14); // (14/2)+1 = 8
  REQUIRE(sound.activeOscillators() == 8);

  setDocRegister(sound, IIgsSound::DOC_OSCILLATOR_ENABLE, 30);
  REQUIRE(sound.activeOscillators() == 16);
}

TEST_CASE("Oscillators are split between the two speakers", "[iigs][sound]") {
  IIgsSound sound;
  putSquareWave(sound, 0x0100, 256);
  setDocRegister(sound, IIgsSound::DOC_OSCILLATOR_ENABLE, 2); // two of them

  // Oscillator 0 on an even channel, oscillator 1 on an odd one.
  setDocRegister(sound, IIgsSound::DOC_WAVE_POINTER, 0x01);
  setDocRegister(sound, IIgsSound::DOC_VOLUME, 0xFF);
  setDocRegister(sound, IIgsSound::DOC_FREQUENCY_HIGH, 0x08);
  setDocRegister(sound, IIgsSound::DOC_CONTROL, 0x00);

  std::vector<float> samples(256 * 2, 0.0f);
  sound.generateSamples(samples.data(), 256, 48000);

  float left = 0.0f, right = 0.0f;
  for (size_t i = 0; i < samples.size(); i += 2) {
    left = std::max(left, std::abs(samples[i]));
    right = std::max(right, std::abs(samples[i + 1]));
  }
  REQUIRE(left > 0.0f);
  REQUIRE(right == 0.0f); // channel 0 is one speaker only

  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_CONTROL + 1), 0x10);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_WAVE_POINTER + 1), 0x01);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_VOLUME + 1), 0xFF);
  setDocRegister(sound, static_cast<uint8_t>(IIgsSound::DOC_FREQUENCY_HIGH + 1), 0x08);

  std::fill(samples.begin(), samples.end(), 0.0f);
  sound.generateSamples(samples.data(), 256, 48000);
  right = 0.0f;
  for (size_t i = 1; i < samples.size(); i += 2) {
    right = std::max(right, std::abs(samples[i]));
  }
  REQUIRE(right > 0.0f);
}
