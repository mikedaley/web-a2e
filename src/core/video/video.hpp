/*
 * video.hpp - Video output generation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../mmu/mmu.hpp"
#include "../types.hpp"
#include "ntsc.hpp"
#include <array>
#include <cstdint>
#include <functional>

namespace a2e {

class Video {
public:
  using CycleCallback = std::function<uint64_t()>;

  // The profile comes from the MMU rather than a second parameter: the video
  // scanner and the floating bus are the same counters read two ways, so a
  // Video and an MMU that disagreed about frame timing would be a bug with no
  // way to express it.
  explicit Video(MMU &mmu);

  // The machine this video generator is modelling.
  const MachineProfile &getMachine() const { return *machine_; }

  // Whether the colour killer is letting chroma through for the current field.
  // False means the picture is being decoded as monochrome because no burst
  // was seen — an all-text screen on a machine that inhibits burst in text.
  bool isChromaEnabled() const { return chromaEnabled_; }

  // Render a complete frame to the framebuffer
  void renderFrame();

  // Force a full frame render from current memory using current video switch
  // state, independent of beam position or CPU cycle count. Useful for
  // debugger screen refresh after stepping.
  void forceRenderFrame();

  // Progressive rendering: render all scanlines up to the current CPU cycle
  void renderUpToCycle(uint64_t currentCycle);

  // Get the framebuffer (RGBA, 560x384)
  const uint8_t *getFramebuffer() const { return framebuffer_.data(); }
  uint8_t *getFramebuffer() { return framebuffer_.data(); }

  // Framebuffer size
  static constexpr size_t getFramebufferSize() { return FRAMEBUFFER_SIZE; }

  // Frame dirty flag
  bool isFrameDirty() const { return frameDirty_; }
  void clearFrameDirty() { frameDirty_ = false; }
  void setFrameDirty() { frameDirty_ = true; }

  // Display mode info
  VideoMode getCurrentMode() const;

  // Color settings
  //
  // The machine emits a 1-bit dot stream; the colour mode chooses which kind of
  // receiver decodes it. setMonochrome() is the older two-state API and is kept
  // working: it switches to MONOCHROME and back to whatever was selected before.
  void setColorMode(VideoColorMode mode);
  VideoColorMode getColorMode() const { return colorMode_; }

  void setMonochrome(bool mono);
  bool isMonochrome() const { return colorMode_ == VideoColorMode::MONOCHROME; }

  /**
   * Draw text lines in two fixed colours instead of decoding them.
   *
   * A //e has no say in what colour its text comes out: the dots go down a
   * composite lead and the receiver makes of them what it will. A IIgs does —
   * its VGC generates the picture digitally and substitutes a foreground and a
   * background colour of the Control Panel's choosing for lit and unlit text
   * dots, which is why a IIgs's text is crisp and can be white on blue.
   *
   * That is a pair of colours, not a second video generator, so it lives here
   * as one: a machine that never calls this decodes text exactly as before.
   * It applies to text lines only — the text rows of a mixed screen included —
   * because graphics dots carry a colour of their own and the VGC leaves them
   * alone.
   */
  void setTextColours(uint32_t foreground, uint32_t background) {
    textForeground_ = foreground;
    textBackground_ = background;
    textColoursSet_ = true;
  }
  void clearTextColours() { textColoursSet_ = false; }

  /**
   * Show double hi-res in black and white rather than decoding it.
   *
   * A IIgs's $C029 has a bit for this (bit 5): the VGC drops the colour from
   * a double hi-res picture and shows its 560 dots as they are, which is how
   * the early Finder and every 80-column desktop program got a crisp screen
   * out of a mode that is otherwise a colour trick. It applies to double
   * hi-res lines only — the rest of the screen decodes as before — and a
   * monochrome monitor still has the last word.
   */
  void setDoubleHiResMonochrome(bool mono) { doubleHiResMono_ = mono; }
  bool isDoubleHiResMonochrome() const { return doubleHiResMono_; }

  void setGreenPhosphor(bool green) { greenPhosphor_ = green; }
  bool isGreenPhosphor() const { return greenPhosphor_; }

  // UK character set (like the physical switch on UK Apple IIe)
  void setUKCharacterSet(bool uk) { ukCharSet_ = uk; }
  bool isUKCharacterSet() const { return ukCharSet_; }

  // Cycle callback for determining current position within frame
  void setCycleCallback(CycleCallback cb) { cycleCallback_ = std::move(cb); }

  // Called by MMU callback when a video-relevant soft switch changes
  void onVideoSwitchChanged();

  // Called at frame boundaries to reset the change log and snapshot state
  void beginNewFrame(uint64_t cycleStart);

private:
  // ==========================================================================
  // Signal stage
  //
  // These emit the 14.31818 MHz dot stream for a column range of one scanline
  // rather than writing pixels. Nothing here knows what colour is: colour is
  // made later, by whichever decoder the colour mode selects.
  //
  // startCol/endCol are byte positions 0-40 (one per CPU cycle in visible area)
  // ==========================================================================
  void emitText40Scanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void emitText80Scanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void emitLoResScanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void emitHiResScanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void emitDoubleLoResScanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void emitDoubleHiResScanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);

  // Dispatch a scanline segment to the correct mode emitter, handling mixed mode
  void renderScanlineSegment(int scanline, int startCol, int endCol, const VideoSwitchState& vs);

  // Render a single scanline using the switch change log (progressive rendering)
  void renderScanlineWithChanges(int scanline);

  // Clear the dot buffer before a scanline's segments run.
  void beginScanline();

  // Decode the finished dot stream and write the scanline's two framebuffer rows.
  void endScanline(int scanline);

  // Whether the machine transmits a colour burst on this scanline. A IIe
  // inhibits burst in text mode, including the bottom four rows of a mixed
  // screen.
  //
  // Note this is not the same question as whether that scanline is *displayed*
  // in colour — see chromaEnabled_.
  bool burstForScanline(int scanline, const VideoSwitchState& vs) const;

  // Whether this line draws text — a full text screen, or the bottom rows of a
  // mixed one. Both the colour burst and the VGC's text colours turn on it.
  bool isTextScanline(int scanline, const VideoSwitchState& vs) const;

  // Character rendering — emit one ROM line's dots for a single character
  void emitCharacterDots(int dotX, int charLine, uint8_t ch, bool inverse,
                         bool flash, const VideoSwitchState& vs, bool is80col);

  // Tag a dot range with how the sharp decoders should colour it. The signal
  // alone cannot answer that: a flat LORES cell and a lit HIRES pixel can carry
  // identical dots and still mean different things.
  void setKind(int from, int to, ntsc::IdealKind k) {
    if (from < 0) from = 0;
    if (to > ntsc::VISIBLE_DOTS) to = ntsc::VISIBLE_DOTS;
    for (int x = from; x < to; x++) {
      idealKind_[static_cast<size_t>(x)] = k;
    }
  }

  // Say which palette entry a run of CELL dots carries. Only the SOLID mode
  // reads it: every other mode makes the colour from the dots. The emitters
  // know the answer for nothing — a LORES cell is its nibble, a double lo-res
  // cell or a double hi-res pixel is its value rotated one place to the left,
  // which is the one dot of delay the 80-column path adds (DOUBLE_RES_DELAY)
  // expressed as the colour it turns into.
  void setCell(int from, int to, uint8_t palette) {
    if (from < 0) from = 0;
    if (to > ntsc::VISIBLE_DOTS) to = ntsc::VISIBLE_DOTS;
    for (int x = from; x < to; x++) {
      cellColour_[static_cast<size_t>(x)] = palette;
    }
  }

  // Write a single dot of the scanline's signal
  void setDot(int x, uint8_t on) {
    if (x >= 0 && x < ntsc::VISIBLE_DOTS + ntsc::SPILL) {
      dots_[static_cast<size_t>(ntsc::DOT_ORIGIN + x)] = on;
    }
  }
  uint8_t getDot(int x) const {
    if (x < 0 || x >= ntsc::VISIBLE_DOTS + ntsc::SPILL) return 0;
    return dots_[static_cast<size_t>(ntsc::DOT_ORIGIN + x)];
  }

  // Character ROM offset helper (shared between 40-col and 80-col paths)
  struct CharROMInfo {
    uint16_t romOffset;
    bool needsXor;
    bool inverse;
  };
  CharROMInfo getCharROMInfo(uint8_t ch, bool inverse, bool flash,
                             const VideoSwitchState& vs) const;

  // Capture current video switch state from MMU
  VideoSwitchState captureVideoState() const;

  // Color helpers
  uint32_t getMonochromeColor(bool on) const;

  // Text screen address calculation
  uint16_t getTextAddress(int row, int col) const;
  uint16_t getHiResAddress(int row, int col) const;

  // Reference to MMU for memory access
  MMU &mmu_;

  // Framebuffer (RGBA, 560x384)
  std::array<uint8_t, FRAMEBUFFER_SIZE> framebuffer_{};

  // Frame state
  bool frameDirty_ = true;

  // Flash state (toggles every ~16 frames)
  int flashCounter_ = 0;
  bool flashState_ = false;
  static constexpr int FLASH_RATE = 16;

  // Per-scanline video signal: one bit per 14.31818 MHz dot, with margin either
  // side so the demodulation window never runs off the ends.
  std::array<uint8_t, ntsc::DOT_BUFFER> dots_{};
  std::array<ntsc::IdealKind, ntsc::VISIBLE_DOTS> idealKind_{};
  std::array<uint8_t, ntsc::VISIBLE_DOTS> cellColour_{}; // see setCell


  // Not owned: profiles are static constexpr objects with program lifetime.
  const MachineProfile *machine_ = &defaultMachineProfile();
  bool burst_ = false;

  // Colour killer.
  //
  // Whether a line is *displayed* in colour is not decided per line. A monitor
  // integrates burst presence over a time constant far longer than one
  // scanline, and its 3.58 MHz reference flywheels through gaps, so the chroma
  // channel is switched on or off for a whole field at a time.
  //
  // This is what makes mixed mode fringe. 160 of its 192 lines carry burst, so
  // the killer never engages and the four text rows at the bottom are decoded
  // in colour along with everything else — exactly as on real hardware. Full
  // text mode transmits no burst at all, the killer engages, and text goes
  // crisp white. It is also why a II+, which never inhibits burst, fringes its
  // text in every mode.
  bool chromaEnabled_ = false;
  bool burstSeenThisFrame_ = false;

  // Display options
  VideoColorMode colorMode_ = VideoColorMode::COMPOSITE;
  VideoColorMode preMonochromeMode_ = VideoColorMode::COMPOSITE;
  bool greenPhosphor_ = false;
  bool ukCharSet_ = false;  // UK character set switch

  // The VGC's text colours, when a machine has a VGC. See setTextColours.
  bool textColoursSet_ = false;
  uint32_t textForeground_ = 0xFFFFFFFF;
  uint32_t textBackground_ = 0xFF000000;

  // Whether the line being finished is a text line, settled at the end of
  // horizontal blanking alongside the burst.
  bool textLine_ = false;
  bool doubleHiResLine_ = false; // this line was emitted as double hi-res
  bool doubleHiResMono_ = false;

  // Cycle callback for position calculation
  CycleCallback cycleCallback_;

  // Per-scanline rendering: frame start cycle and switch change log
  uint64_t frameStartCycle_ = 0;
  VideoSwitchState frameStartState_{};

  static constexpr int MAX_SWITCH_CHANGES = 1024;
  std::array<VideoSwitchChange, MAX_SWITCH_CHANGES> switchChanges_;
  int switchChangeCount_ = 0;

  // Progressive per-scanline rendering state
  int lastRenderedScanline_ = -1;   // -1 means no scanlines rendered yet this frame
  int changeIdx_ = 0;               // Current position in switch change log
  VideoSwitchState currentRenderState_{}; // Current video state for progressive rendering

  // Lookup tables
  static constexpr std::array<int, 24> TEXT_ROW_OFFSETS = {
      {0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
       0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
       0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0}};
};

} // namespace a2e
