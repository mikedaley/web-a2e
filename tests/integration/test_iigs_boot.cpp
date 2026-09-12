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
// The 80-column screen: even columns from the auxiliary text page, odd from
// the main one, which is how the //e's 80-column firmware lays a line out.
std::string screenText80(IIgsMachine &machine) {
  static const uint16_t rowBase[24] = {
      0x400, 0x480, 0x500, 0x580, 0x600, 0x680, 0x700, 0x780,
      0x428, 0x4A8, 0x528, 0x5A8, 0x628, 0x6A8, 0x728, 0x7A8,
      0x450, 0x4D0, 0x550, 0x5D0, 0x650, 0x6D0, 0x750, 0x7D0};
  std::string text;
  for (int row = 0; row < 24; row++) {
    for (int column = 0; column < 80; column++) {
      const uint8_t cell = machine.memory().megaII().readRAM(
          rowBase[row] + column / 2, (column % 2) == 0);
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
  // The speaker and the synthesiser share an amplifier whose volume is the
  // bottom nibble of $C03C, and the firmware sets it from battery RAM on the
  // way up — this machine has not got that far, so turn it up by hand.
  machine.memory().write(0x00C03C, 0x0F);
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

  // All the way to Bitsy Bye's catalog. It used to stop at the splash with
  // "Unable to load ATInit file", because the loader keeps that file in
  // auxiliary memory and bank $00 did not yet follow the //e's switches into
  // bank $01.
  const std::string text = screenText(machine);
  INFO("screen:\n" << text);
  REQUIRE(text.find("PRODOS.2.4.3") != std::string::npos);
  REQUIRE(text.find("BITSY.BOOT") != std::string::npos);

  // The Control Panel was not touched to make that happen.
  REQUIRE(machine.memory().peek(0x00C02D) == 0x00);
}

TEST_CASE("A IIgs SmartPort traps execution, not reads",
          "[iigs][boot][smartport]") {
  // The entry point at $C50A is a trap: the card services a driver call when
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
  const uint32_t entry = 0x00C500 + 0x0A; // the machine's own ProDOS entry
  REQUIRE(machine.memory().read(0x00C501) == 0x20);
  REQUIRE(machine.memory().read(0x00C503) == 0x00);
  REQUIRE(machine.memory().read(0x00C505) == 0x03);
  REQUIRE(machine.memory().read(0x00C5FF) == 0x0A); // the machine's own layout
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

TEST_CASE("A IIgs's interrupt vectors point at firmware in the I/O page",
          "[iigs][boot][interrupt]") {
  // The ROM's IRQ and BRK vectors name $C071 and $C074, and on a real IIgs
  // those addresses read firmware — a few bytes of 8-bit code whose whole job
  // is to set V or not and JML into the 16-bit interrupt manager. A //e reads
  // the bus there. A IIgs that did the same took every BRK straight into a
  // page of zeros and sat executing BRK after BRK where its handler should be.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the vector test");
    return;
  }
  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);

  const uint16_t irqVector = static_cast<uint16_t>(
      machine.memory().read(0xFFFFFE) | (machine.memory().read(0xFFFFFF) << 8));
  REQUIRE(irqVector >= 0xC071);
  REQUIRE(irqVector <= 0xC07F);

  for (uint16_t at = 0xC071; at < 0xC080; at++) {
    const uint8_t fromROM = machine.memory().read(0xFF0000u | at);
    REQUIRE(machine.memory().read(0x000000u | at) == fromROM);
    REQUIRE(machine.memory().read(0x00E00000u | at) == fromROM);
    REQUIRE(machine.memory().peek(0x000000u | at) == fromROM);
  }
  // ...and what is there is code that reaches the interrupt manager, not
  // whatever the bus happened to hold.
  REQUIRE(machine.memory().read(0x000000u | 0xC075) == 0x5C); // JML
}

TEST_CASE("A IIgs pulls its interrupt vectors from ROM whatever the language card holds",
          "[iigs][boot][interrupt]") {
  // The 65816 says on its VPB line when it is fetching a vector, and the FPI
  // answers from ROM regardless of the map. Nothing on a IIgs writes a vector
  // into bank zero's RAM — GS/OS's kernel image ends before $FFFA and holds
  // nothing at $FFEE — and GS/OS copies that kernel over $D000-$FFFF with
  // interrupts enabled. A machine that read the vector out of the RAM took the
  // first interrupt of that copy to $0000.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the vector-pull test");
    return;
  }
  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  runToPrompt(machine);
  IIgsMemory &memory = machine.memory();

  // Language card RAM in, and garbage where the emulation-mode IRQ vector
  // would be read from if the map were consulted.
  memory.read(0x00C083);
  memory.read(0x00C083);
  memory.write(0x00FFFE, 0x00);
  memory.write(0x00FFFF, 0x00);
  REQUIRE(memory.read(0x00FFFE) == 0x00); // the RAM really is in the map

  // Now an interrupt: the mouse, with its interrupt enabled.
  memory.write(0x00C027, IIgsADB::STATUS_MOUSE_INTERRUPT);
  machine.mouseMove(3, 0);
  REQUIRE(memory.interruptPending());

  // Whatever the CPU was doing at the prompt, its next instruction after
  // taking the interrupt is the firmware's vector target in the I/O page,
  // and not page zero.
  machine.cpu().setP(static_cast<uint8_t>(machine.cpu().getP() & ~0x04)); // I clear
  machine.step();
  const uint32_t pc = (static_cast<uint32_t>(machine.cpu().getPBR()) << 16) | machine.cpu().getPC();
  const uint16_t romVector = static_cast<uint16_t>(memory.read(0xFFFFFE) | (memory.read(0xFFFFFF) << 8));
  REQUIRE(pc == (0x000000u | romVector));
  REQUIRE(pc >= 0x00C071);
  REQUIRE(pc <= 0x00C07F);
}

TEST_CASE("The firmware services a vertical-blanking interrupt and comes back",
          "[iigs][boot][interrupt]") {
  // Enable the Mega II's VBL interrupt the way GS/OS does, let a frame end,
  // and the ROM's interrupt manager must take it, acknowledge it through
  // $C047, and return to what it was doing — rather than get lost in the
  // serial check, the sound check, or a vector that was not there.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the VBL service test");
    return;
  }
  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  runToPrompt(machine);
  IIgsMemory &memory = machine.memory();

  memory.write(0x00C041, IIgsMemory::INT_VBL);
  machine.cpu().setP(static_cast<uint8_t>(machine.cpu().getP() & ~0x04)); // I clear
  memory.signalVerticalBlank();
  REQUIRE(memory.interruptPending());

  // Give the manager a few thousand instructions.
  for (int i = 0; i < 20000; i++) machine.step();

  REQUIRE_FALSE(memory.interruptPending());          // acknowledged
  // The flag itself may well be set again by now: several frames have gone
  // by with VBL enabled, and $C046 reports every one until $C047 clears it.
  // What matters is that nothing is still asking.
  const uint32_t pc = (static_cast<uint32_t>(machine.cpu().getPBR()) << 16) | machine.cpu().getPC();
  REQUIRE(pc >= 0x000100);                            // not in page zero
  REQUIRE(machine.cpu().getPBR() == 0xFF);            // back in the firmware's prompt loop
}

TEST_CASE("The VGC interrupts once the beam has drawn a marked Super Hi-Res line",
          "[iigs][boot][interrupt]") {
  // Bit 6 of a line's control byte asks for an interrupt when that line has
  // been drawn, which is how QuickDraw II knows it is safe to redraw the
  // pointer. It has to come every frame, not once a second. The handler is
  // the program's to install, so the processor is kept masked here and the
  // test acknowledges each one itself, through $C032, as QuickDraw's would.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the scan-line interrupt test");
    return;
  }
  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  runToPrompt(machine);
  IIgsMemory &memory = machine.memory();
  const auto &timing = machineProfile(MachineId::AppleIIgs).timing;
  machine.cpu().setP(static_cast<uint8_t>(machine.cpu().getP() | 0x04)); // I set

  const int frames = 10;
  auto countRaises = [&]() {
    const uint64_t start = machine.slowCycles();
    int raised = 0;
    while (machine.slowCycles() - start <
           static_cast<uint64_t>(timing.cyclesPerFrame()) * frames) {
      machine.step();
      if (memory.peek(0x00C023) & IIgsMemory::VGC_SCANLINE_PENDING) {
        raised++;
        memory.write(0x00C032, 0xDF); // bit 5 low: acknowledged, as QuickDraw does
      }
    }
    return raised;
  };

  // Mark line 100 and enable the interrupt, but leave Super Hi-Res off: the
  // control bytes mean nothing to the VGC in the //e's modes.
  memory.write(0x00E19D00 + 100, IIgsVideo::SCB_INTERRUPT);
  memory.write(0x00C023, IIgsMemory::VGC_SCANLINE_ENABLE);
  REQUIRE(countRaises() == 0);

  // With the picture on, once a frame.
  memory.write(0x00C029, IIgsMemory::NEW_VIDEO_SHR);
  const int raised = countRaises();
  REQUIRE(raised >= frames - 1);
  REQUIRE(raised <= frames + 1);

  // A line that no longer asks is left alone.
  memory.write(0x00E19D00 + 100, 0x00);
  REQUIRE(countRaises() == 0);
}

TEST_CASE("A IIgs's 80-column text puts its even columns in the auxiliary bank",
          "[iigs][boot][memory]") {
  // The //e's 80-column firmware writes a line's even columns to the auxiliary
  // text page through 80STORE and PAGE2, and on a IIgs "auxiliary" is bank
  // $01, shadowed into $E1. A machine that left those writes in bank $00 drew
  // every other column blank.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the 80-column test");
    return;
  }
  const std::vector<uint8_t> image = loadSystemMaster();
  if (image.empty()) {
    WARN("DOS 3.3 System Master not found; skipping the 80-column test");
    return;
  }
  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  REQUIRE(machine.insertDisk(0, image.data(), image.size(), "dos33.dsk"));
  for (int i = 0; i < 40000000 && !machine.cpu().isStopped(); i++) machine.step();
  REQUIRE(screenText(machine).find(']') != std::string::npos);

  auto type = [&](const char *line) {
    for (const char *c = line; *c; c++) {
      machine.keyDown(*c);
      for (int i = 0; i < 60000; i++) machine.step();
    }
    machine.keyDown(0x0D);
    for (int i = 0; i < 1500000; i++) machine.step();
  };
  type("PR#3");
  type("PRINT \"ABCDEFGH\"");

  const std::string text = screenText80(machine);
  INFO("80-column screen:\n" << text);
  REQUIRE(text.find("ABCDEFGH") != std::string::npos);
}

TEST_CASE("The firmware services an oscillator interrupt and comes back",
          "[iigs][boot][interrupt][sound]") {
  // Play a one-shot through the Ensoniq with its interrupt bit set, from the
  // firmware's prompt. When the oscillator reaches the end of its table it
  // interrupts; the ROM's interrupt manager must find the chip asking in $E0,
  // dispatch it, clear it, and get back to what it was doing — rather than
  // take it for something else or take it for ever.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the oscillator interrupt test");
    return;
  }
  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  runToPrompt(machine);
  IIgsMemory &memory = machine.memory();
  IIgsSound &sound = memory.sound();

  // A short sound in the chip's RAM, through the window as a program would.
  memory.write(0x00C03C, 0x6F); // RAM, auto-increment, full volume
  memory.write(0x00C03F, 0x01);
  memory.write(0x00C03E, 0x00);
  for (int i = 0; i < 255; i++) memory.write(0x00C03D, (i % 2) ? 0xC0 : 0x40);
  memory.write(0x00C03D, 0x00);

  // One oscillator, one-shot, interrupting, on the wave at $0100.
  auto reg = [&](uint8_t r, uint8_t v) {
    memory.write(0x00C03C, 0x0F); // registers
    memory.write(0x00C03E, r);
    memory.write(0x00C03D, v);
  };
  reg(IIgsSound::DOC_OSCILLATOR_ENABLE, 0);
  reg(IIgsSound::DOC_WAVE_POINTER, 0x01);
  reg(IIgsSound::DOC_WAVE_SIZE, 0x00);
  reg(IIgsSound::DOC_VOLUME, 0xFF);
  reg(IIgsSound::DOC_FREQUENCY_LOW, 0x00);
  reg(IIgsSound::DOC_FREQUENCY_HIGH, 0x04);
  reg(IIgsSound::DOC_CONTROL, IIgsSound::OSC_MODE_ONE_SHOT | IIgsSound::OSC_INTERRUPT_ENABLE);
  REQUIRE_FALSE(sound.oscillatorHalted(0));
  machine.cpu().setP(static_cast<uint8_t>(machine.cpu().getP() & ~0x04)); // I clear

  bool interrupted = false;
  for (int i = 0; i < 400000; i++) {
    machine.step();
    if (sound.interruptPending()) interrupted = true;
  }
  REQUIRE(sound.oscillatorHalted(0));
  REQUIRE(interrupted);                         // it asked
  REQUIRE_FALSE(sound.interruptPending());      // and was answered
  REQUIRE_FALSE(memory.interruptPending());
  REQUIRE(machine.cpu().getPBR() == 0xFF);      // back in the firmware's prompt loop
}

TEST_CASE("The bell fades out and ends quiet", "[iigs][boot][audio]") {
  // The ROM's bell toggles the speaker while ramping the $C03C volume nibble
  // down, then puts the nibble back. The gain follows the nibble as it
  // happened, after the coupling stage and through a slew, so the fade is
  // continuous and putting the volume back does not bring the speaker's
  // decaying tail back as a thump — which was heard as a note after the bell.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the bell test");
    return;
  }
  const std::vector<uint8_t> image = loadSystemMaster();
  if (image.empty()) {
    WARN("DOS 3.3 System Master not found; skipping the bell test");
    return;
  }
  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE, roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  REQUIRE(machine.insertDisk(0, image.data(), image.size(), "dos33.dsk"));
  for (int i = 0; i < 40000000 && !machine.cpu().isStopped(); i++) machine.step();
  REQUIRE(screenText(machine).find(']') != std::string::npos);

  for (const char *c = "PRINT CHR$(7)"; *c; c++) { machine.keyDown(*c); for (int i = 0; i < 60000; i++) machine.step(); }
  machine.keyDown(0x0D);

  // The envelope in ten-millisecond windows, with the nibble at each.
  std::vector<float> buffer(480 * 2);
  std::vector<float> peaks; std::vector<int> nibbles;
  for (int n = 0; n < 100; n++) {
    machine.generateStereoAudioSamples(buffer.data(), 480);
    float peak = 0; for (int i = 0; i < 480; i++) peak = std::max(peak, std::fabs(buffer[i * 2]));
    peaks.push_back(peak); nibbles.push_back(machine.memory().sound().volume());
  }
  // Find the bell: the loudest window, and the window where the nibble goes
  // back up after having been ramped to zero.
  int loudest = 0; for (int n = 0; n < 100; n++) if (peaks[n] > peaks[loudest]) loudest = n;
  int restored = -1;
  for (int n = loudest; n < 99; n++) if (nibbles[n] == 0 && nibbles[n + 1] > 0) { restored = n + 1; break; }
  REQUIRE(loudest > 2);
  REQUIRE(restored > loudest);
  INFO("tone peak " << peaks[loudest] << ", after restore " << peaks[restored] << " " << peaks[restored + 1]);
  // Rang, faded to nearly nothing, and stayed quiet once the volume came back.
  REQUIRE(peaks[loudest] > 0.1f);
  REQUIRE(peaks[restored - 1] < peaks[loudest] * 0.25f);
  REQUIRE(peaks[restored] < peaks[loudest] * 0.15f);
  REQUIRE(peaks[restored + 2] < peaks[loudest] * 0.05f);
  // And the fade was a slope, not steps: no window louder than the one before it.
  for (int n = loudest + 1; n < restored; n++) REQUIRE(peaks[n] <= peaks[n - 1] + 0.005f);
}

TEST_CASE("A IIgs's SmartPort answers where its own firmware does",
          "[iigs][boot][smartport]") {
  // The machine's slot 5 firmware has its ProDOS entry at $C50A and its
  // SmartPort entry at $C50D, and software written for a IIgs hard-codes those
  // rather than reading $C5FF. A boot loader that did `JSR $C50D` into a card
  // laid out like a card found an RTS there, came back without its inline
  // parameters skipped, and executed them into a BRK.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the SmartPort layout test");
    return;
  }
  const std::vector<uint8_t> image = loadFile("public/disks/ProDOS 2.4.3.po");
  if (image.empty()) {
    WARN("ProDOS image not found; skipping the SmartPort layout test");
    return;
  }
  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  REQUIRE(machine.insertBlockImage(0, image.data(), image.size(), "hd.po"));
  IIgsMemory &memory = machine.memory();
  REQUIRE(memory.peek(0x00C5FF) == 0x0A);
  REQUIRE(memory.peek(0x00C50A) == 0x38);
  REQUIRE(memory.peek(0x00C50D) == 0x38);
  REQUIRE(memory.peek(0x00C501) == 0x20);
}

TEST_CASE("The IIgs Diagnostic's speed loop counts what the disk expects",
          "[iigs][boot][timing]") {
  // The Apple IIgs Diagnostic measures the processor's speed by counting
  // iterations of a nine-cycle loop between two changes of $C02E, the
  // vertical counter, which moves every two scan lines. It accepts 25 or 26
  // at fast speed and 14 or 15 at slow, and those numbers are the sum of
  // three things the machine models: 2.8MHz with one refresh cycle in every
  // ten for fast RAM, a Mega II access that waits for the slow clock's edge
  // and then takes a whole slow cycle, and a slow speed that is exactly the
  // Mega II's. The same loop, run here, has to count the same.
  if (!romAvailable()) {
    WARN("IIgs ROM not built in; skipping the speed loop test");
    return;
  }
  IIgsMachine machine;
  machine.init(roms::ROM_SYSTEM_IIGS, roms::ROM_SYSTEM_IIGS_SIZE,
               roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
  runToPrompt(machine);
  IIgsMemory &memory = machine.memory();

  // The diagnostic's loop, verbatim, with a STP after it:
  //   LDX #0 / LDA $C02E / CMP $C02E / BEQ -3
  //   LDA $C02E / INX / CMP $C02E / BEQ -3 / NOP  (run until the NOP is reached)
  static const uint8_t loop[] = {0xA2, 0x00, 0xAD, 0x2E, 0xC0, 0xCD, 0x2E, 0xC0, 0xF0, 0xFB,
                                 0xAD, 0x2E, 0xC0, 0xE8, 0xCD, 0x2E, 0xC0, 0xF0, 0xFA, 0xEA};
  auto count = [&](uint8_t speed) {
    memory.write(0x00C036, speed);
    for (size_t i = 0; i < sizeof loop; i++) memory.write(0x000300 + i, loop[i]);
    machine.cpu().setPBR(0x00);
    machine.cpu().setPC(0x0300);
    machine.cpu().setP(static_cast<uint8_t>(machine.cpu().getP() | 0x04)); // no interrupts in the way
    int steps = 0;
    while (steps++ < 100000 && !(machine.cpu().getPBR() == 0 && machine.cpu().getPC() == 0x0313)) machine.step();
    REQUIRE(machine.cpu().getPC() == 0x0313);
    return machine.cpu().getX() & 0xFF;
  };
  const int fast = count(0x80);
  const int slow = count(0x00);
  INFO("fast " << fast << ", slow " << slow);
  REQUIRE((fast == 25 || fast == 26));
  REQUIRE((slow == 14 || slow == 15));
}
