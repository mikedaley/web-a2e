/*
 * iigs_video.hpp - Super Hi-Res, and the picture the machine actually shows
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "iigs_spec.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace a2e {
class Video;
}

namespace a2e::iigs {

class IIgsMemory;

/**
 * IIgsVideo - the machine's screen, whichever half of it is drawing
 *
 * A IIgs has two video systems and shows one of them. The Mega II's is a //e's,
 * drawn by the //e's own `Video` class from the //e's own memory. The other is
 * Super Hi-Res, which is this: 200 lines of pixels in bank $E1, each line
 * choosing its own width and its own sixteen colours out of four thousand and
 * ninety-six.
 *
 * $C029 bit 7 decides which is on screen, and that is the whole of the switch.
 *
 * **A IIgs screen is per-line, which is the thing to understand about it.**
 * There is no single "mode". Each of the 200 lines has a control byte of its
 * own saying whether it is 320 pixels wide or 640, which of the sixteen
 * palettes it draws from, and whether it fills. So one screen can be 320-wide
 * artwork above a 640-wide menu bar, with different colours in each, and
 * programs really did that.
 *
 * The picture is composited into the raster a monitor is sent, border and
 * all: 848x480, with the 640x200 picture (lines doubled) at (96, 38) and the
 * border colour from $C034 around it — the counts are iigs_spec.hpp's. The
 * Mega II's 560x384 goes in the same place, stretched to 640 wide, because a
 * text screen and a Super Hi-Res screen are the same width on the monitor,
 * and its last eight lines are border, because it is eight lines shorter.
 */
class IIgsVideo {
public:
  IIgsVideo(Video &megaII, IIgsMemory &memory);

  /** Draw the current screen and return it. RGBA, 848x480. */
  const uint8_t *render();

  size_t framebufferSize() const { return frame_.size(); }

  /** $C029 NEWVIDEO: bit 7 puts Super Hi-Res on screen. */
  bool superHiResEnabled() const;

  /**
   * A palette entry as a colour.
   *
   * Entries are $0RGB — four bits each, so 4096 colours — and each nibble is
   * expanded to eight bits by repeating it, which is what makes $F map to 255
   * rather than 240 and keeps white white.
   */
  static void paletteColour(uint16_t entry, uint8_t &red, uint8_t &green,
                            uint8_t &blue);

  /**
   * One of the VGC's sixteen fixed colours, as `$0RGB`.
   *
   * These are the colours the Control Panel offers for text, background and
   * border, and they are the machine's own rather than the Apple II palette
   * the NTSC decoder derives. A IIgs generates them digitally — there is no
   * subcarrier involved — so medium blue is `$22F` and looks it, where a
   * composite //e's blue is whatever a television makes of four dots.
   */
  static uint16_t vgcColour(uint8_t index);

  /** The same, packed as the 0xAARRGGBB the video pipeline passes around. */
  static uint32_t vgcColourARGB(uint8_t index);

  // Scanline control byte, per line.
  static constexpr uint8_t SCB_MODE_640 = 0x80;
  static constexpr uint8_t SCB_INTERRUPT = 0x40;
  static constexpr uint8_t SCB_FILL_MODE = 0x20;
  static constexpr uint8_t SCB_PALETTE_MASK = 0x0F;

private:
  void renderSuperHiRes();
  void renderMegaII();
  void fillFrame(uint32_t colour);
  void drawLine320(int line, const uint8_t *pixels, const uint16_t *palette,
                   bool fillMode);
  void drawLine640(int line, const uint8_t *pixels, const uint16_t *palette);
  uint8_t *pictureRow(int line); // the first picture pixel of a doubled line
  uint8_t *scanline(int y);
  void putPixel(uint8_t *row, int x, uint16_t colour);

  Video &megaII_;
  IIgsMemory &memory_;
  std::vector<uint8_t> frame_;
  int width_ = 0;
  int height_ = 0;
};

} // namespace a2e::iigs
