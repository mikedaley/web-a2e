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
#include "audio/audio.hpp"
#include "cards/disk_controller.hpp"
#include "iigs_video.hpp"
#include "machine/machine_profile.hpp"
#include "video/video.hpp"
#include "roms.cpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace a2e;
using namespace a2e::iigs;

namespace {
bool romAvailable() {
  return roms::ROM_SYSTEM_IIGS_SIZE >= ROM_SIZE_ROM01;
}

// Long enough for the diagnostics, the splash and the boot attempt: about
// seven seconds of the machine's own time, which is what a IIgs takes over its
// power-on tests. Counted in instructions rather than cycles because that is
// what the test can drive directly.
void runToPrompt(IIgsMachine &machine) {
  for (int i = 0; i < 5000000 && !machine.cpu().isStopped(); i++) {
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

// ---------------------------------------------------------------------------
// Booting a disk, which is the drive, the two clocks and the firmware at once
// ---------------------------------------------------------------------------

namespace {
// The DOS 3.3 System Master that ships in public/disks, or an empty vector if
// it is not where the test expects it. Tests run from the source directory.
std::vector<uint8_t> loadSystemMaster() {
  FILE *file = fopen("public/disks/Apple DOS 3.3 January 1983.dsk", "rb");
  if (!file) return {};
  std::vector<uint8_t> image(143360);
  const size_t read = fread(image.data(), 1, image.size(), file);
  fclose(file);
  if (read != image.size()) return {};
  return image;
}

// What the Mega II's text page says, row by row, as one string.
std::string screenText(IIgsMachine &machine) {
  static const uint16_t rowBase[24] = {
      0x400, 0x480, 0x500, 0x580, 0x600, 0x680, 0x700, 0x780,
      0x428, 0x4A8, 0x528, 0x5A8, 0x628, 0x6A8, 0x728, 0x7A8,
      0x450, 0x4D0, 0x550, 0x5D0, 0x650, 0x6D0, 0x750, 0x7D0};
  std::string text;
  for (int row = 0; row < 24; row++) {
    for (int column = 0; column < 40; column++) {
      const uint8_t cell =
          machine.memory().megaII().readRAM(rowBase[row] + column, false);
      const char ch = static_cast<char>(cell & 0x7F);
      text += (ch >= 0x20 && ch <= 0x7E) ? ch : ' ';
    }
    text += '\n';
  }
  return text;
}
} // namespace

TEST_CASE("A IIgs boots DOS 3.3 through its own IWM", "[iigs][boot][disk]") {
  // The other machines each boot this image in test_emulator_disk; this is the
  // same claim for the one that reads it with a 65816, out of its own firmware
  // rather than a card's, on a clock that changes speed while it does it.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the disk boot test");
    return;
  }
  const std::vector<uint8_t> image = loadSystemMaster();
  if (image.empty()) {
    WARN("DOS 3.3 System Master not found; skipping the disk boot test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  REQUIRE(machine.insertDisk(0, image.data(), image.size(), "dos33.dsk"));

  // The diagnostics take about ten seconds of the machine's own time before it
  // so much as looks at a drive, and DOS and Applesoft load behind that.
  for (int i = 0; i < 40000000 && !machine.cpu().isStopped(); i++) {
    machine.step();
  }

  const std::string text = screenText(machine);
  INFO("screen:\n" << text);
  REQUIRE(text.find("DOS VERSION 3.3") != std::string::npos);
  REQUIRE(text.find(']') != std::string::npos);
}

TEST_CASE("A IIgs leaves the disk it boots from alone", "[iigs][boot][disk]") {
  // The firmware writes the IWM's mode register the instruction after it
  // switches the drive off, and $C0EF is both that register and Q7 — so a
  // machine that lets a coasting drive write erases track zero and then reads
  // nothing. The image is the witness: boot it, and every byte must still be
  // where it started.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the disk integrity test");
    return;
  }
  const std::vector<uint8_t> image = loadSystemMaster();
  if (image.empty()) {
    WARN("DOS 3.3 System Master not found; skipping the disk integrity test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  REQUIRE(machine.insertDisk(0, image.data(), image.size(), "dos33.dsk"));

  for (int i = 0; i < 40000000 && !machine.cpu().isStopped(); i++) {
    machine.step();
  }

  size_t size = 0;
  const uint8_t *after = machine.disk().getDiskData(0, &size);
  REQUIRE(after != nullptr);
  REQUIRE(size == image.size());
  REQUIRE(std::equal(image.begin(), image.end(), after));
}

TEST_CASE("A IIgs has a speaker as well as an Ensoniq", "[iigs][boot][audio]") {
  // $C030 is a Mega II address and behind it is the one-bit speaker every
  // Apple II has. The synthesiser is a different chip on different addresses,
  // and a machine given only that one is silent through every beep, every
  // click and every game written before 1986 — which is most of what it runs.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the speaker test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);

  auto peakOf = [&machine](std::vector<float> &buffer) {
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    machine.generateStereoAudioSamples(buffer.data(),
                                       static_cast<int>(buffer.size() / 2));
    float peak = 0.0f;
    for (float sample : buffer) peak = std::max(peak, std::fabs(sample));
    return peak;
  };

  // The firmware beeps on the way up, which is itself the speaker working —
  // so let that finish and take the quiet afterwards as the control.
  std::vector<float> buffer(1024, 0.0f);
  float quietPeak = 1.0f;
  for (int attempt = 0; attempt < 200 && quietPeak > 0.01f; attempt++) {
    quietPeak = peakOf(buffer);
  }
  INFO("the machine never went quiet; peak " << quietPeak);
  REQUIRE(quietPeak <= 0.01f);

  // Then toggle it at something like a musical rate. The machine is run by
  // asking it for audio, so the toggles go between short buffers rather than
  // in a loop of their own: that is how a program on it makes a sound, and it
  // keeps each buffer's span of time the length the mixer expects.
  std::vector<float> chunk(64, 0.0f);
  float tonePeak = 0.0f;
  for (int round = 0; round < 40; round++) {
    machine.memory().read(0x00C030); // the speaker, at the Mega II's address
    tonePeak = std::max(tonePeak, peakOf(chunk));
  }

  INFO("quiet peak " << quietPeak << ", tone peak " << tonePeak);
  REQUIRE(tonePeak > 0.1f);
}

TEST_CASE("A IIgs comes up white on blue, with a blue border",
          "[iigs][boot][video][colour]") {
  // The firmware writes $F6 to $C022 and $06 to $C034 on the way up: white
  // text on medium blue, in a medium blue border. That is the screen everyone
  // remembers, and it is not something the //e's video could produce — its
  // text is whatever a receiver makes of the dots.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the colour test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  runToPrompt(machine);

  REQUIRE(machine.memory().textColourRegister() == 0xF6);
  REQUIRE(machine.memory().borderColour() == 0x06);

  uint8_t red = 0, green = 0, blue = 0;
  IIgsVideo::paletteColour(IIgsVideo::vgcColour(0x06), red, green, blue);

  const uint8_t *frame = machine.screen().render();
  const auto &display = machineProfile(MachineId::AppleIIgs).display;
  const int width = display.pixelWidth;

  auto isBorderBlue = [&](int x, int y) {
    const size_t at = (static_cast<size_t>(y) * width + x) * 4;
    return frame[at] == red && frame[at + 1] == green && frame[at + 2] == blue;
  };

  // The corner is border, and the middle of the picture is a blank text cell,
  // which is background — the same blue in both places.
  REQUIRE(isBorderBlue(1, 1));
  REQUIRE(isBorderBlue(width - 2, display.pixelHeight - 2));

  // And the screen is only ever those two colours: no decoder ran on it.
  size_t other = 0;
  for (int y = 0; y < display.pixelHeight; y++) {
    for (int x = 0; x < width; x++) {
      const size_t at = (static_cast<size_t>(y) * width + x) * 4;
      const bool isBlue = frame[at] == red && frame[at + 1] == green &&
                          frame[at + 2] == blue;
      const bool isWhite =
          frame[at] == 0xFF && frame[at + 1] == 0xFF && frame[at + 2] == 0xFF;
      if (!isBlue && !isWhite) other++;
    }
  }
  INFO("pixels that are neither white nor the border blue: " << other);
  REQUIRE(other == 0);
}

// ---------------------------------------------------------------------------
// The SmartPort, which is the machine's own rather than a card in a slot
// ---------------------------------------------------------------------------

namespace {
std::vector<uint8_t> loadFile(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return {};
  fseek(file, 0, SEEK_END);
  const long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  std::vector<uint8_t> image(static_cast<size_t>(size));
  const size_t read = fread(image.data(), 1, image.size(), file);
  fclose(file);
  if (read != image.size()) return {};
  return image;
}
} // namespace

TEST_CASE("A IIgs boots ProDOS from its own SmartPort", "[iigs][boot][smartport]") {
  // Slot 5 is where a IIgs keeps its SmartPort, and it is part of the machine:
  // no card to fit, no Control Panel setting to change. $C02D stays exactly as
  // the firmware wrote it — every slot internal — because a part the machine
  // has is on the internal side of that switch.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the SmartPort boot test");
    return;
  }
  const std::vector<uint8_t> image = loadFile("public/disks/ProDOS 2.4.3.po");
  if (image.empty()) {
    WARN("ProDOS image not found; skipping the SmartPort boot test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  REQUIRE(machine.insertBlockImage(0, image.data(), image.size(), "hd.po"));

  for (int i = 0; i < 60000000 && !machine.cpu().isStopped(); i++) {
    machine.step();
  }

  const std::string text = screenText(machine);
  INFO("screen:\n" << text);
  REQUIRE(text.find("ProDOS 8") != std::string::npos);

  // The Control Panel was not touched to make that happen.
  REQUIRE(machine.memory().peek(0x00C02D) == 0x00);
}

TEST_CASE("A IIgs SmartPort traps execution, not reads",
          "[iigs][boot][smartport]") {
  // The entry point at $C510 is a trap: the card services a driver call when
  // the CPU *executes* there, and hands back a ROM byte when something reads
  // it as data. Which of those is happening depends on what the processor has
  // done to the program counter by the time the read arrives, and a 65816 has
  // not done what a 6502 has — it reads the opcode and then advances. Guessing
  // the 6502's answer here boots the volume by luck and then fails every
  // driver call after it, which is exactly what "UNABLE TO LOAD PRODOS" is.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the SmartPort trap test");
    return;
  }
  const std::vector<uint8_t> image = loadFile("public/disks/ProDOS 2.4.3.po");
  if (image.empty()) {
    WARN("ProDOS image not found; skipping the SmartPort trap test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  REQUIRE(machine.insertBlockImage(0, image.data(), image.size(), "hd.po"));

  // Reading the slot's bytes is what a ProDOS driver scan does, and it must
  // see the ROM: the signature that says "a block device lives here", and the
  // SEC that a call would have been answered with.
  const uint32_t entry = 0x00C500 + 0x10;
  REQUIRE(machine.memory().read(0x00C501) == 0x20);
  REQUIRE(machine.memory().read(0x00C503) == 0x00);
  REQUIRE(machine.memory().read(0x00C505) == 0x03);
  REQUIRE(machine.memory().read(0x00C5FF) == 0x10);
  REQUIRE(machine.memory().read(entry) == 0x38); // SEC, untouched
}

TEST_CASE("A IIgs with no SmartPort image keeps its own slot 5 firmware",
          "[iigs][boot][smartport]") {
  // With nothing inserted the SmartPort has no ROM, so $C500 is the machine's
  // own SmartPort firmware — the real one, which drives the IWM looking for a
  // 3.5" drive. Its signature says SmartPort rather than Disk II: $Cn07 is $00
  // where slot 6's is $3C.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the slot 5 firmware test");
    return;
  }

  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);

  REQUIRE(machine.memory().read(0x00C501) == 0x20);
  REQUIRE(machine.memory().read(0x00C503) == 0x00);
  REQUIRE(machine.memory().read(0x00C505) == 0x03);
  REQUIRE(machine.memory().read(0x00C507) == 0x00); // SmartPort, not a Disk II
  REQUIRE(machine.memory().read(0x00C500) == 0xA2); // the firmware's own code
}
