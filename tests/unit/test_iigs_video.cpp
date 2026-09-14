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
#include "video/video.hpp"

using namespace a2e;
using namespace a2e::iigs;

namespace {

constexpr uint32_t bankAddress(uint8_t bank, uint16_t offset) {
  return (static_cast<uint32_t>(bank) << 16) | offset;
}

struct Screen {
  IIgsMachine machine;
  const uint8_t *frame = nullptr;
  int width = RASTER_WIDTH;

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

  // The colour at a point of the raster, as three channels.
  std::array<uint8_t, 3> rasterAt(int x, int y) const {
    const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
    return {frame[offset], frame[offset + 1], frame[offset + 2]};
  }

  // The colour at a point of the picture, which sits inside the border.
  std::array<uint8_t, 3> at(int x, int y) const {
    return rasterAt(PICTURE_LEFT + x, PICTURE_TOP + y);
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
  const size_t first = (static_cast<size_t>(PICTURE_TOP) * RASTER_WIDTH + PICTURE_LEFT) * 4;
  REQUIRE(frame[first] == 0xFF); // red, having arrived by way of the other bank
  REQUIRE(frame[first + 1] == 0x00);
  REQUIRE(frame[first + 2] == 0x00);
}

// ---------------------------------------------------------------------------
// The VGC's colours, which are neither the //e's picture nor Super Hi-Res
// ---------------------------------------------------------------------------

TEST_CASE("The border is the bottom nibble of $C034", "[iigs][video][colour]") {
  // $C034 is two registers at one address: the clock's transaction control on
  // top, the border colour underneath. The firmware writes $06 there and means
  // medium blue, without starting a transaction.
  IIgsMachine machine;
  machine.memory().write(bankAddress(0x00, 0xC034), 0x06);
  REQUIRE(machine.memory().borderColour() == 0x06);

  // The Mega II's picture sits inside the border, so the corner of the raster
  // is border and nothing else.
  const uint8_t *frame = machine.screen().render();
  uint8_t red = 0, green = 0, blue = 0;
  IIgsVideo::paletteColour(IIgsVideo::vgcColour(0x06), red, green, blue);
  REQUIRE(frame[0] == red);
  REQUIRE(frame[1] == green);
  REQUIRE(frame[2] == blue);
  REQUIRE(blue > red); // medium blue, and recognisably blue

  // The clock still gets its own nibble, and reading the address gives both.
  REQUIRE((machine.memory().peek(bankAddress(0x00, 0xC034)) & 0x0F) == 0x06);
}

TEST_CASE("$C022 colours the text rather than decoding it",
          "[iigs][video][colour]") {
  // A IIgs does not send its //e-mode text down a composite lead: the VGC
  // substitutes two colours for lit and unlit dots. So a text screen is
  // exactly two colours, whichever receiver the display settings are asking
  // for — which is the opposite of every other machine here.
  IIgsMachine machine;
  machine.memory().write(bankAddress(0x00, 0xC022), 0xF6); // white on blue

  REQUIRE(machine.memory().textForeground() == 0x0F);
  REQUIRE(machine.memory().textBackground() == 0x06);

  // Text mode, one inverse space on an otherwise blank line, so the line
  // carries both colours. $E0 is the Mega II's main bank.
  machine.memory().write(bankAddress(0x00, 0xC051), 0x00); // TEXT on
  for (uint16_t at = 0x0400; at < 0x0800; at++) {
    machine.memory().write(bankAddress(SLOW_BANK_MAIN, at), 0xA0); // space
  }
  machine.memory().write(bankAddress(SLOW_BANK_MAIN, 0x0400), 0x20); // inverse

  machine.video().forceRenderFrame();
  const uint8_t *frame = machine.screen().render();

  uint8_t fr = 0, fg = 0, fb = 0, br = 0, bg = 0, bb = 0;
  IIgsVideo::paletteColour(IIgsVideo::vgcColour(0x0F), fr, fg, fb);
  IIgsVideo::paletteColour(IIgsVideo::vgcColour(0x06), br, bg, bb);

  // The //e's picture is inside the border, stretched to 640 wide — a 14-dot
  // cell is 16 pixels here — and centred in the picture area's 200 lines.
  auto pixel = [&](int x, int y) {
    const size_t at =
        (static_cast<size_t>(MEGAII_TOP + y) * RASTER_WIDTH + (PICTURE_LEFT + x)) * 4;
    return std::array<uint8_t, 3>{frame[at], frame[at + 1], frame[at + 2]};
  };

  // The inverse cell is solid foreground; the blank cell beside it is solid
  // background. Neither is anything else.
  REQUIRE(pixel(2, 2) == std::array<uint8_t, 3>{fr, fg, fb});
  REQUIRE(pixel(44, 2) == std::array<uint8_t, 3>{br, bg, bb});

  // A monochrome monitor has one phosphor and no opinion about what the
  // machine sent it, so the display setting still wins over the VGC.
  machine.video().setColorMode(VideoColorMode::MONOCHROME);
  machine.video().forceRenderFrame();
  frame = machine.screen().render();
  REQUIRE(pixel(40, 2) == std::array<uint8_t, 3>{0x00, 0x00, 0x00});
}

TEST_CASE("The raster is the picture with the border a monitor sees around it",
          "[iigs][video][border]") {
  // What a IIgs sends is 53 cycles across by 240 lines: the picture, and 6
  // cycles of border before it, 7 after, 19 lines above and 21 below — the
  // cycles GSSquared's scanner flags as border. Super Hi-Res is 16 pixels a
  // cycle, and the border drawn is the part a monitor shows — three cycles
  // either side, twelve lines above and below — so the raster is 736x448
  // with the picture at (48, 24).
  REQUIRE(RASTER_WIDTH == 736);
  REQUIRE(RASTER_HEIGHT == 448);
  REQUIRE(PICTURE_LEFT == 48);
  REQUIRE(PICTURE_TOP == 24);

  Screen screen;
  screen.machine.memory().write(bankAddress(0x00, 0xC034), 0x06); // blue border
  screen.setPalette(0, 1, 0x0F00);
  for (int line = 0; line < SHR_LINES; line++) {
    screen.setControlByte(line, 0x00);
    for (int byte = 0; byte < SHR_BYTES_PER_LINE; byte++) screen.setPixelByte(line, byte, 0x11);
  }
  screen.draw();

  uint8_t br = 0, bg = 0, bb = 0;
  IIgsVideo::paletteColour(IIgsVideo::vgcColour(0x06), br, bg, bb);
  const std::array<uint8_t, 3> border = {br, bg, bb};

  SECTION("Super Hi-Res fills the picture and nothing else") {
    REQUIRE(screen.at(0, 0) == RED);
    REQUIRE(screen.at(639, 399) == RED);
    REQUIRE(screen.rasterAt(0, 0) == border);
    REQUIRE(screen.rasterAt(PICTURE_LEFT - 1, PICTURE_TOP) == border);
    REQUIRE(screen.rasterAt(PICTURE_LEFT + 640, PICTURE_TOP) == border);
    REQUIRE(screen.rasterAt(PICTURE_LEFT, PICTURE_TOP - 1) == border);
    REQUIRE(screen.rasterAt(PICTURE_LEFT, PICTURE_TOP + 400) == border);
    REQUIRE(screen.rasterAt(RASTER_WIDTH - 1, RASTER_HEIGHT - 1) == border);
  }

  SECTION("a //e mode draws 192 lines centred in the 200, with border above and below") {
    // Text mode, every cell inverse, so the picture is solid text foreground.
    screen.machine.memory().write(bankAddress(0x00, 0xC029), 0x00);
    screen.machine.memory().write(bankAddress(0x00, 0xC022), 0xF6);
    screen.machine.memory().write(bankAddress(0x00, 0xC051), 0x00);
    for (uint16_t at = 0x0400; at < 0x0800; at++) {
      screen.machine.memory().write(bankAddress(SLOW_BANK_MAIN, at), 0x20);
    }
    screen.machine.video().forceRenderFrame();
    screen.draw();
    uint8_t fr = 0, fg = 0, fb = 0;
    IIgsVideo::paletteColour(IIgsVideo::vgcColour(0x0F), fr, fg, fb);
    const std::array<uint8_t, 3> white = {fr, fg, fb};
    // Four picture lines (eight raster lines) of border above and below the
    // 192, so the top and bottom borders are the same height.
    constexpr int GAP = MEGAII_TOP - PICTURE_TOP;
    REQUIRE(GAP == 8);
    REQUIRE(screen.at(0, GAP - 1) == border);
    REQUIRE(screen.at(0, GAP) == white);
    REQUIRE(screen.at(639, GAP + 383) == white); // stretched to the full width
    REQUIRE(screen.at(0, GAP + 384) == border);
    REQUIRE(screen.at(0, 399) == border);
    REQUIRE(screen.rasterAt(PICTURE_LEFT - 1, MEGAII_TOP) == border);
  }
}

TEST_CASE("$C029 bit 5 shows double hi-res in black and white",
          "[iigs][video][colour]") {
  // The early Finder and the 80-column desktop programs draw a 560-dot
  // double hi-res picture and set this bit so the VGC shows the dots as they
  // are. Decoding them into colour instead fringes every letter.
  IIgsMachine machine;
  machine.video().setColorMode(VideoColorMode::COMPOSITE);

  // Double hi-res: graphics, hi-res, 80 columns, AN3 off. A lone lit dot
  // pattern, which any colour decoder turns into a hue.
  machine.memory().write(bankAddress(0x00, 0xC050), 0x00); // graphics
  machine.memory().write(bankAddress(0x00, 0xC057), 0x00); // hires
  machine.memory().write(bankAddress(0x00, 0xC00D), 0x00); // 80 columns
  machine.memory().write(bankAddress(0x00, 0xC05E), 0x00); // AN3 off: DHGR
  for (uint16_t at = 0x2000; at < 0x4000; at++) {
    machine.memory().write(bankAddress(SLOW_BANK_MAIN, at), 0x11);
    machine.memory().write(bankAddress(SLOW_BANK_AUX, at), 0x11);
  }

  auto greyOnly = [&](bool mono) {
    machine.memory().write(bankAddress(0x00, 0xC029),
                           mono ? IIgsMemory::NEW_VIDEO_MONO_DHGR : 0x00);
    machine.video().forceRenderFrame();
    const uint8_t *frame = machine.screen().render();
    bool grey = true;
    for (int x = 0; x < 640 && grey; x++) {
      const size_t at =
          (static_cast<size_t>(MEGAII_TOP + 4) * RASTER_WIDTH + PICTURE_LEFT + x) * 4;
      grey = frame[at] == frame[at + 1] && frame[at + 1] == frame[at + 2];
    }
    return grey;
  };

  REQUIRE_FALSE(greyOnly(false)); // colour, as a //e would show it
  REQUIRE(greyOnly(true));        // the VGC's monochrome double hi-res

  // A monochrome monitor is one phosphor whatever the bit says; the bit
  // must not turn a green screen white.
  machine.memory().write(bankAddress(0x00, 0xC029), IIgsMemory::NEW_VIDEO_MONO_DHGR);
  machine.video().setColorMode(VideoColorMode::MONOCHROME);
  machine.video().setGreenPhosphor(true);
  machine.video().forceRenderFrame();
  const uint8_t *frame = machine.screen().render();
  bool anyGreen = false;
  for (int x = 0; x < 640 && !anyGreen; x++) {
    const size_t at =
        (static_cast<size_t>(MEGAII_TOP + 4) * RASTER_WIDTH + PICTURE_LEFT + x) * 4;
    anyGreen = frame[at + 1] > frame[at] && frame[at + 1] > frame[at + 2];
  }
  REQUIRE(anyGreen);
}
