/*
 * iigs_video.cpp - Super Hi-Res, and the picture the machine actually shows
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_video.hpp"

#include "../machine/machine_profile.hpp"
#include "../mmu/mmu.hpp"
#include "../video/video.hpp"
#include "iigs_memory.hpp"

#include <cstring>

namespace a2e::iigs {

namespace {
// $C029, the new video register. Bit 7 is the one that matters here; bit 6
// linearises Super Hi-Res memory on a ROM 3 and bit 5 turns the Mega II's
// bank-switched memory off, neither of which is modelled.
constexpr uint16_t NEW_VIDEO = 0xC029;
constexpr uint8_t NEW_VIDEO_SUPER_HIRES = 0x80;

constexpr int PALETTE_BYTES = SHR_PALETTE_ENTRIES * 2;

// In 640 mode a byte is four two-bit pixels, and which four colours each of
// them may use depends on where it sits in the byte. That is how sixteen
// colours are made to serve a mode that can only count to four per pixel:
// the palette is read as four groups of four, and the groups rotate across the
// byte. Software draws with it by arranging the palette so neighbouring groups
// dither into each other.
constexpr int MODE_640_GROUPS[4] = {2, 3, 0, 1};

// The VGC's sixteen fixed colours, as $0RGB. Black, red, dark blue, purple,
// dark green, dark grey, medium blue, light blue, brown, orange, light grey,
// pink, light green, yellow, aqua, white — the Control Panel's list, in the
// order the register numbers them.
constexpr uint16_t VGC_COLOURS[16] = {
    0x0000, 0x0D03, 0x0009, 0x0D2D, 0x0072, 0x0555, 0x022F, 0x06AF,
    0x0852, 0x0F60, 0x0AAA, 0x0F98, 0x01D1, 0x0FF0, 0x04F9, 0x0FFF,
};
} // namespace

IIgsVideo::IIgsVideo(Video &megaII, IIgsMemory &memory)
    : megaII_(megaII), memory_(memory) {
  const auto &display = machineProfile(MachineId::AppleIIgs).display;
  width_ = display.pixelWidth;
  height_ = display.pixelHeight;
  frame_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
}

void IIgsVideo::paletteColour(uint16_t entry, uint8_t &red, uint8_t &green,
                              uint8_t &blue) {
  // $0RGB, four bits a channel. Repeating the nibble rather than shifting it
  // is what makes $F white rather than nearly-white.
  const uint8_t r = (entry >> 8) & 0x0F;
  const uint8_t g = (entry >> 4) & 0x0F;
  const uint8_t b = entry & 0x0F;
  red = static_cast<uint8_t>((r << 4) | r);
  green = static_cast<uint8_t>((g << 4) | g);
  blue = static_cast<uint8_t>((b << 4) | b);
}

uint16_t IIgsVideo::vgcColour(uint8_t index) {
  return VGC_COLOURS[index & 0x0F];
}

uint32_t IIgsVideo::vgcColourARGB(uint8_t index) {
  uint8_t red = 0, green = 0, blue = 0;
  paletteColour(vgcColour(index), red, green, blue);
  return 0xFF000000u | (static_cast<uint32_t>(red) << 16) |
         (static_cast<uint32_t>(green) << 8) | blue;
}

bool IIgsVideo::superHiResEnabled() const {
  return (memory_.peek(NEW_VIDEO) & NEW_VIDEO_SUPER_HIRES) != 0;
}

void IIgsVideo::fillFrame(uint32_t colour) {
  const uint8_t red = static_cast<uint8_t>(colour >> 16);
  const uint8_t green = static_cast<uint8_t>(colour >> 8);
  const uint8_t blue = static_cast<uint8_t>(colour);
  for (size_t at = 0; at < frame_.size(); at += 4) {
    frame_[at + 0] = red;
    frame_[at + 1] = green;
    frame_[at + 2] = blue;
    frame_[at + 3] = 0xFF;
  }
}

uint8_t *IIgsVideo::scanline(int y) {
  return frame_.data() + static_cast<size_t>(y) * width_ * 4;
}

void IIgsVideo::putPixel(uint8_t *row, int x, uint16_t colour) {
  uint8_t red = 0, green = 0, blue = 0;
  paletteColour(colour, red, green, blue);
  uint8_t *pixel = row + static_cast<size_t>(x) * 4;
  pixel[0] = red;
  pixel[1] = green;
  pixel[2] = blue;
  pixel[3] = 0xFF;
}

const uint8_t *IIgsVideo::render() {
  if (superHiResEnabled()) {
    renderSuperHiRes();
  } else {
    renderMegaII();
  }
  return frame_.data();
}

void IIgsVideo::renderMegaII() {
  // The //e's picture, centred in a screen that is bigger than it. A real IIgs
  // does much the same: the //e modes do not fill a Super Hi-Res raster, and
  // what is around them is border — the border the Control Panel sets, in the
  // bottom nibble of $C034, and not black unless that is what it says.
  const uint32_t border = vgcColourARGB(memory_.borderColour());
  fillFrame(border);

  const auto &megaIIDisplay = machineProfile(MachineId::AppleIIe).display;
  const int sourceWidth = megaIIDisplay.pixelWidth;
  const int sourceHeight = megaIIDisplay.pixelHeight;
  const int left = (width_ - sourceWidth) / 2;
  const int top = (height_ - sourceHeight) / 2;

  const uint8_t *source = megaII_.getFramebuffer();
  for (int y = 0; y < sourceHeight; y++) {
    std::memcpy(scanline(top + y) + static_cast<size_t>(left) * 4,
                source + static_cast<size_t>(y) * sourceWidth * 4,
                static_cast<size_t>(sourceWidth) * 4);
  }
}

void IIgsVideo::renderSuperHiRes() {
  // Everything Super Hi-Res draws from is in bank $E1: the pixels at $2000, a
  // control byte for each line at $9D00, and sixteen palettes at $9E00. A
  // program running in fast RAM writes to bank $01 and shadowing brings it
  // here, which is the arrangement the whole machine is built around.
  MMU &megaIIMemory = memory_.megaII();
  auto slowRead = [&megaIIMemory](uint16_t address) {
    return megaIIMemory.readRAM(address, true); // bank $E1 is the aux side
  };

  for (int line = 0; line < SHR_LINES; line++) {
    const uint8_t control =
        slowRead(static_cast<uint16_t>(SHR_SCB_BASE + line));

    // The line's own palette, read once rather than per pixel: sixteen entries
    // of two bytes, low byte first.
    uint16_t palette[SHR_PALETTE_ENTRIES];
    const uint16_t paletteBase = static_cast<uint16_t>(
        SHR_PALETTE_BASE + (control & SCB_PALETTE_MASK) * PALETTE_BYTES);
    for (int entry = 0; entry < SHR_PALETTE_ENTRIES; entry++) {
      const uint16_t at = static_cast<uint16_t>(paletteBase + entry * 2);
      palette[entry] =
          static_cast<uint16_t>(slowRead(at) | (slowRead(at + 1) << 8));
    }

    uint8_t pixels[SHR_BYTES_PER_LINE];
    const uint16_t pixelBase =
        static_cast<uint16_t>(SHR_PIXEL_BASE + line * SHR_BYTES_PER_LINE);
    for (int byte = 0; byte < SHR_BYTES_PER_LINE; byte++) {
      pixels[byte] = slowRead(static_cast<uint16_t>(pixelBase + byte));
    }

    if (control & SCB_MODE_640) {
      drawLine640(line, pixels, palette);
    } else {
      drawLine320(line, pixels, palette, (control & SCB_FILL_MODE) != 0);
    }

    // 200 lines over a 400-line screen: every line is drawn twice, which is
    // what the machine does and why a IIgs picture is 200 lines tall.
    std::memcpy(scanline(line * 2 + 1), scanline(line * 2),
                static_cast<size_t>(width_) * 4);
  }
}

void IIgsVideo::drawLine320(int line, const uint8_t *pixels,
                            const uint16_t *palette, bool fillMode) {
  uint8_t *row = scanline(line * 2);
  uint8_t previous = 0;

  for (int byte = 0; byte < SHR_BYTES_PER_LINE; byte++) {
    // Two pixels to a byte, high nibble first.
    for (int half = 0; half < 2; half++) {
      uint8_t index = half == 0 ? (pixels[byte] >> 4) : (pixels[byte] & 0x0F);

      // Fill mode: colour zero is not a colour, it is "the same as the pixel
      // before". It exists because it made horizontal runs cheap to draw, and
      // it means a line's leftmost pixel can never be transparent — there is
      // nothing to its left to copy.
      if (fillMode && index == 0) index = previous;
      previous = index;

      const uint16_t colour = palette[index];
      const int x = (byte * 2 + half) * 2; // 320 pixels across a 640 screen
      putPixel(row, x, colour);
      putPixel(row, x + 1, colour);
    }
  }
}

void IIgsVideo::drawLine640(int line, const uint8_t *pixels,
                            const uint16_t *palette) {
  uint8_t *row = scanline(line * 2);

  for (int byte = 0; byte < SHR_BYTES_PER_LINE; byte++) {
    for (int position = 0; position < 4; position++) {
      const uint8_t value =
          (pixels[byte] >> ((3 - position) * 2)) & 0x03;
      // Which quarter of the palette this pixel draws from depends on where it
      // sits in the byte. See MODE_640_GROUPS.
      const int group = MODE_640_GROUPS[position];
      const uint16_t colour = palette[group * 4 + value];
      putPixel(row, byte * 4 + position, colour);
    }
  }
}

} // namespace a2e::iigs
