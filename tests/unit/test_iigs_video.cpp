/*
 * test_iigs_video.cpp - Super Hi-Res
 *
 * A IIgs screen is not a mode, it is two hundred lines each choosing its own.
 * These write pixels, control bytes and palettes into bank $E1 the way a
 * program would and then look at what came out, because that is the only way
 * to tell a palette that is off by one from a palette that is right.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "iigs_machine.hpp"
#include "iigs_memory.hpp"
#include "iigs_video.hpp"
#include "mmu/mmu.hpp"

using namespace a2e;
using namespace a2e::iigs;

namespace {

constexpr uint32_t bankAddress(uint8_t bank, uint16_t offset) {
  return (static_cast<uint32_t>(bank) << 16) | offset;
}

struct Screen {
  IIgsMachine machine;
  const uint8_t *frame = nullptr;
  int width = 640;

  Screen() {
    machine.memory().write(bankAddress(0x00, 0xC029),
                           IIgsMemory::NEW_VIDEO_SHR);
  }

  // Write straight to the Mega II's auxiliary side, which is bank $E1 and
  // where the video reads from.
  void poke(uint16_t at, uint8_t value) {
    machine.memory().write(bankAddress(SLOW_BANK_AUX, at), value);
  }

  void setControlByte(int line, uint8_t value) {
    poke(static_cast<uint16_t>(SHR_SCB_BASE + line), value);
  }

  void setPalette(int palette, int entry, uint16_t colour) {
    const uint16_t at = static_cast<uint16_t>(SHR_PALETTE_BASE + palette * 32 +
                                              entry * 2);
    poke(at, static_cast<uint8_t>(colour));
    poke(static_cast<uint16_t>(at + 1), static_cast<uint8_t>(colour >> 8));
  }

  void setPixelByte(int line, int byte, uint8_t value) {
    poke(static_cast<uint16_t>(SHR_PIXEL_BASE + line * SHR_BYTES_PER_LINE + byte),
         value);
  }

  void draw() { frame = machine.screen().render(); }

  // The colour at a point, as three channels.
  std::array<uint8_t, 3> at(int x, int y) const {
    const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
    return {frame[offset], frame[offset + 1], frame[offset + 2]};
  }
};

constexpr std::array<uint8_t, 3> RED = {0xFF, 0x00, 0x00};
constexpr std::array<uint8_t, 3> GREEN = {0x00, 0xFF, 0x00};
constexpr std::array<uint8_t, 3> BLUE = {0x00, 0x00, 0xFF};
constexpr std::array<uint8_t, 3> BLACK = {0x00, 0x00, 0x00};

} // namespace

TEST_CASE("A palette entry is four bits a channel", "[iigs][video]") {
  uint8_t red = 0, green = 0, blue = 0;

  // $0F00 is red at full, and full means 255 — the nibble is repeated rather
  // than shifted, or white would come out as $F0F0F0 and look grey next to it.
  IIgsVideo::paletteColour(0x0F00, red, green, blue);
  REQUIRE(red == 0xFF);
  REQUIRE(green == 0x00);
  REQUIRE(blue == 0x00);

  IIgsVideo::paletteColour(0x0FFF, red, green, blue);
  REQUIRE(red == 0xFF);
  REQUIRE(green == 0xFF);
  REQUIRE(blue == 0xFF);

  IIgsVideo::paletteColour(0x0888, red, green, blue);
  REQUIRE(red == 0x88);
  REQUIRE(green == 0x88);
  REQUIRE(blue == 0x88);
}

TEST_CASE("320 mode is two pixels to a byte, sixteen colours a line",
          "[iigs][video]") {
  Screen screen;
  screen.setControlByte(0, 0x00); // 320 mode, palette 0
  screen.setPalette(0, 1, 0x0F00); // red
  screen.setPalette(0, 2, 0x00F0); // green
  screen.setPixelByte(0, 0, 0x12); // one pixel of each

  screen.draw();

  // 320 pixels across a 640-wide screen, so each is two wide.
  REQUIRE(screen.at(0, 0) == RED);
  REQUIRE(screen.at(1, 0) == RED);
  REQUIRE(screen.at(2, 0) == GREEN);
  REQUIRE(screen.at(3, 0) == GREEN);

  SECTION("and 200 lines across a 400-line screen, so each is two tall") {
    REQUIRE(screen.at(0, 1) == RED);
    REQUIRE(screen.at(0, 2) == BLACK); // the next line, which nothing drew
  }
}

TEST_CASE("640 mode is four pixels to a byte, from four groups of four",
          "[iigs][video]") {
  // The trick that makes sixteen colours serve a two-bit pixel: which quarter
  // of the palette a pixel draws from depends on where it sits in the byte.
  Screen screen;
  screen.setControlByte(0, IIgsVideo::SCB_MODE_640);
  for (int entry = 0; entry < 16; entry++) screen.setPalette(0, entry, 0x0000);
  screen.setPalette(0, 9, 0x0F00);  // group 2, value 1
  screen.setPalette(0, 13, 0x00F0); // group 3, value 1
  screen.setPalette(0, 1, 0x000F);  // group 0, value 1
  screen.setPalette(0, 5, 0x0FFF);  // group 1, value 1

  screen.setPixelByte(0, 0, 0b01010101); // value 1 in all four positions
  screen.draw();

  REQUIRE(screen.at(0, 0) == RED);
  REQUIRE(screen.at(1, 0) == GREEN);
  REQUIRE(screen.at(2, 0) == BLUE);
  REQUIRE(screen.at(3, 0) == std::array<uint8_t, 3>{0xFF, 0xFF, 0xFF});
}

TEST_CASE("Every line chooses its own mode and its own palette",
          "[iigs][video]") {
  // This is the thing to understand about a IIgs screen: there is no single
  // mode. Programs really did put 320-wide artwork above a 640-wide menu bar,
  // with different colours in each.
  Screen screen;

  screen.setControlByte(0, 0x00); // line 0: 320, palette 0
  screen.setPalette(0, 1, 0x0F00);
  screen.setPixelByte(0, 0, 0x10);

  screen.setControlByte(1, 0x03); // line 1: 320, palette 3
  screen.setPalette(3, 1, 0x00F0);
  screen.setPixelByte(1, 0, 0x10);

  screen.draw();

  REQUIRE(screen.at(0, 0) == RED);   // first line, first palette
  REQUIRE(screen.at(0, 2) == GREEN); // second line, doubled, third palette
}

TEST_CASE("Fill mode makes colour zero mean the pixel before it",
          "[iigs][video]") {
  // It exists because it made horizontal runs cheap to draw: one pixel of
  // colour and then nothing repeats it to the end of the run.
  Screen screen;
  screen.setControlByte(0, IIgsVideo::SCB_FILL_MODE);
  screen.setPalette(0, 3, 0x0F00);
  screen.setPixelByte(0, 0, 0x30); // colour 3, then colour 0
  screen.setPixelByte(0, 1, 0x00); // and two more zeroes

  screen.draw();

  REQUIRE(screen.at(0, 0) == RED);
  REQUIRE(screen.at(2, 0) == RED); // filled
  REQUIRE(screen.at(4, 0) == RED); // still filled, into the next byte

  SECTION("and without it, colour zero is a colour like any other") {
    Screen plain;
    plain.setControlByte(0, 0x00);
    plain.setPalette(0, 0, 0x000F); // blue
    plain.setPalette(0, 3, 0x0F00);
    plain.setPixelByte(0, 0, 0x30);
    plain.draw();

    REQUIRE(plain.at(0, 0) == RED);
    REQUIRE(plain.at(2, 0) == BLUE);
  }
}

TEST_CASE("$C029 decides which of the machine's two screens is showing",
          "[iigs][video]") {
  Screen screen;
  screen.setPalette(0, 1, 0x0F00);
  screen.setPixelByte(0, 0, 0x10);
  screen.draw();
  REQUIRE(screen.at(0, 0) == RED);

  // Turn Super Hi-Res off and the Mega II is back, drawn into the middle of a
  // screen that is bigger than its picture — so the top-left corner, which is
  // border, goes black.
  screen.machine.memory().write(bankAddress(0x00, 0xC029), 0x00);
  screen.draw();
  REQUIRE(screen.at(0, 0) == BLACK);
}

TEST_CASE("A program draws Super Hi-Res by writing to fast RAM",
          "[iigs][video][shadow]") {
  // The arrangement the whole machine is built around: the program writes to
  // bank $01 at full speed, shadowing copies it to $E1, and the video reads
  // $E1. Nothing here touches the Mega II's side directly.
  IIgsMachine machine;
  machine.memory().write(bankAddress(0x00, 0xC029), IIgsMemory::NEW_VIDEO_SHR);

  machine.memory().write(bankAddress(0x01, SHR_SCB_BASE), 0x00);
  machine.memory().write(bankAddress(0x01, SHR_PALETTE_BASE + 2), 0x00);
  machine.memory().write(bankAddress(0x01, SHR_PALETTE_BASE + 3), 0x0F);
  machine.memory().write(bankAddress(0x01, SHR_PIXEL_BASE), 0x10);

  const uint8_t *frame = machine.screen().render();
  REQUIRE(frame[0] == 0xFF); // red, having arrived by way of the other bank
  REQUIRE(frame[1] == 0x00);
  REQUIRE(frame[2] == 0x00);
}
