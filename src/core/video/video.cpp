/*
 * video.cpp - Video output generation implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "video.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

Video::Video(MMU &mmu) : mmu_(mmu), machine_(&mmu.getMachine()) {
  // Initialize framebuffer to black
  std::memset(framebuffer_.data(), 0, framebuffer_.size());
}

VideoMode Video::getCurrentMode() const {
  const auto &sw = mmu_.getSoftSwitches();

  if (sw.text) {
    return sw.col80 ? VideoMode::TEXT_80 : VideoMode::TEXT_40;
  }

  if (sw.hires) {
    // DHR requires: AN3 OFF (!an3), 80COL on, HIRES on
    if (sw.col80 && !sw.an3) {
      return VideoMode::DOUBLE_HIRES;
    }
    return VideoMode::HIRES;
  }

  // Double LoRes: AN3 OFF (!an3), 80COL on
  if (sw.col80 && !sw.an3) {
    return VideoMode::DOUBLE_LORES;
  }
  return VideoMode::LORES;
}

// ============================================================================
// Raster rendering infrastructure
// ============================================================================

VideoSwitchState Video::captureVideoState() const {
  const auto &sw = mmu_.getSoftSwitches();
  return {sw.text, sw.mixed, sw.page2, sw.hires,
          sw.col80, sw.altCharSet, sw.store80, sw.an3};
}

void Video::onVideoSwitchChanged() {
  VideoSwitchState newState = captureVideoState();

  // Compare against last logged state to avoid redundant entries
  const VideoSwitchState &lastState =
      (switchChangeCount_ > 0) ? switchChanges_[switchChangeCount_ - 1].state
                                : frameStartState_;

  if (std::memcmp(&newState, &lastState, sizeof(VideoSwitchState)) == 0) {
    return; // No actual change
  }

  if (switchChangeCount_ >= MAX_SWITCH_CHANGES) {
    return; // Log full, drop this change
  }

  uint32_t cycleOffset = 0;
  if (cycleCallback_) {
    uint64_t currentCycle = cycleCallback_();
    // +2 models the Apple IIe two-stage video pipeline delay:
    // 1) Phi-0/Phi-1 bus phasing: the video fetches memory on Phi-0 (first half
    //    of each clock cycle) before the CPU writes on Phi-1 (second half).
    //    A soft switch change on cycle N misses that cycle's video fetch.
    // 2) Shift register latching: the byte fetched on Phi-0 of cycle N+1 is
    //    loaded into the shift register and doesn't produce visible dots until
    //    approximately cycle N+2.
    // Combined: a CPU write on cycle N affects display output at cycle N+2.
    cycleOffset = static_cast<uint32_t>(currentCycle - frameStartCycle_ + 2);
  }

  switchChanges_[switchChangeCount_] = {cycleOffset, newState};
  switchChangeCount_++;
}

void Video::beginNewFrame(uint64_t cycleStart) {
  frameStartCycle_ = cycleStart;
  frameStartState_ = captureVideoState();
  switchChangeCount_ = 0;

  // Carry the killer's decision over from the field just finished. A real one
  // is an integrator, so it necessarily lags by more than a line; a field of
  // lag is both the simplest faithful model and roughly what the hardware did.
  // The visible consequence is that switching to full text takes a frame for
  // the colour to die away, which is also what a monitor does.
  chromaEnabled_ = burstSeenThisFrame_;
  burstSeenThisFrame_ = false;

  // Reset progressive rendering state
  lastRenderedScanline_ = -1;
  changeIdx_ = 0;
  currentRenderState_ = frameStartState_;
}

// ============================================================================
// Character ROM offset helper (deduplicates 40-col and 80-col logic)
// ============================================================================

Video::CharROMInfo Video::getCharROMInfo(uint8_t ch, bool inverse, bool flash,
                                          const VideoSwitchState &vs) const {
  uint16_t romOffset;
  bool needsXor = false;

  if (vs.altCharSet) {
    uint8_t charIndex;
    if (ch >= 0x40 && ch < 0x60) {
      charIndex = ch;
      needsXor = true;
      inverse = false;
    } else if (ch >= 0x60 && ch < 0x80) {
      charIndex = ch;
      needsXor = true;
    } else if (ch < 0x40) {
      charIndex = ch;
      needsXor = false;
    } else {
      if (ch < 0xA0) {
        charIndex = ch & 0x1F;
      } else if (ch < 0xC0) {
        charIndex = (ch & 0x1F) + 32;
      } else if (ch < 0xE0) {
        charIndex = ch & 0x1F;
      } else {
        charIndex = (ch & 0x1F) + 96;
      }
      needsXor = false;
      inverse = false;
    }
    romOffset = charIndex * 8;
  } else {
    uint8_t charIndex;
    if (ch < 0x20) {
      charIndex = ch;
    } else if (ch < 0x40) {
      charIndex = ch;
    } else if (ch < 0x60) {
      charIndex = ch & 0x1F;
    } else if (ch < 0x80) {
      charIndex = (ch & 0x1F) + 32;
    } else if (ch < 0xA0) {
      charIndex = ch & 0x1F;
      inverse = false;
    } else if (ch < 0xC0) {
      charIndex = (ch & 0x1F) + 32;
      inverse = false;
    } else if (ch < 0xE0) {
      charIndex = ch & 0x1F;
      inverse = false;
    } else {
      charIndex = (ch & 0x1F) + 96;
      inverse = false;
    }
    romOffset = charIndex * 8;
    needsXor = false;
  }

  // Apply UK character set offset if enabled
  if (ukCharSet_) {
    romOffset += 0x1000;
  }

  // Handle flash - toggle inverse state when flash is active
  if (flash && flashState_ && !vs.altCharSet) {
    inverse = !inverse;
  }

  return {romOffset, needsXor, inverse};
}

// ============================================================================
// Per-character-line signal emission
// ============================================================================

void Video::emitCharacterDots(int dotX, int charLine, uint8_t ch, bool inverse,
                              bool flash, const VideoSwitchState &vs,
                              bool is80col) {
  CharROMInfo info = getCharROMInfo(ch, inverse, flash, vs);

  uint8_t rowData = mmu_.readCharROM(info.romOffset + charLine);
  if (info.needsXor) {
    rowData ^= 0xFF;
  }
  if (info.inverse) {
    rowData ^= 0xFF;
  }

  // Text is dot-gated like HIRES. Its strokes are drawn shapes rather than an
  // encoded colour value, and on real hardware they pick up artifact colour
  // from their dot pattern exactly as HIRES pixels do — so they do here too,
  // whenever the colour burst is live. Full text mode kills the burst, which is
  // what keeps an all-text screen crisp and white.
  setKind(dotX, dotX + (is80col ? 7 : 14), ntsc::IdealKind::DOT_GATED);

  if (is80col) {
    // 80-col: one dot per character bit, 7 dots per cell.
    for (int charCol = 0; charCol < 7; charCol++) {
      setDot(dotX + charCol, (rowData >> charCol) & 1);
    }
  } else {
    // 40-col: the shift register runs at half rate, so each bit is two dots.
    for (int charCol = 0; charCol < 7; charCol++) {
      const uint8_t on = (rowData >> charCol) & 1;
      setDot(dotX + charCol * 2, on);
      setDot(dotX + charCol * 2 + 1, on);
    }
  }
}

// ============================================================================
// Per-scanline segment emitters
//
// Each emits the dot stream for a column range [startCol, endCol) of one
// scanline. Columns are byte positions 0-40, matching the hardware's per-cycle
// memory reads. Dot positions are absolute across the line, which matters: the
// colour subcarrier is a free-running reference, so what a pattern decodes to
// depends on where it sits relative to that reference, not on where its byte
// cell begins.
//
// One extra dot of delay applies to the double-resolution modes. The 80-column
// shift path is clocked a dot later than the 40-column one, so a given bit
// pattern lands on the next subcarrier phase along and comes out a quarter turn
// round the colour wheel. This is the same fact the old DLGR_COLORS table in
// types.hpp encoded, as a copy of LORES_COLORS with the nibble rotated left by
// one; expressing it as a delay in the signal puts it where it belongs and lets
// one palette serve every mode.
// ============================================================================

namespace {
constexpr int DOUBLE_RES_DELAY = 1;
} // namespace

void Video::emitText40Scanline(int scanline, int startCol, int endCol,
                               const VideoSwitchState &vs) {
  int textRow = scanline / 8;
  int charLine = scanline % 8;
  if (textRow >= 24) return;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getTextAddress(textRow, col);

    uint8_t ch;
    if (vs.page2 && !vs.store80) {
      ch = mmu_.readRAM(addr + 0x0400, false);
    } else {
      ch = mmu_.readRAM(addr, false);
    }

    bool inverse = (ch & 0xC0) == 0x00;
    bool flash = (ch & 0xC0) == 0x40;

    emitCharacterDots(col * 14, charLine, ch, inverse, flash, vs, false);
  }
}

void Video::emitText80Scanline(int scanline, int startCol, int endCol,
                               const VideoSwitchState &vs) {
  int textRow = scanline / 8;
  int charLine = scanline % 8;
  if (textRow >= 24) return;

  uint16_t pageOffset = (vs.page2 && !vs.store80) ? 0x0400 : 0x0000;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getTextAddress(textRow, col);

    // Aux memory character (even columns in display)
    uint8_t auxCh = mmu_.readRAM(addr + pageOffset, true);
    bool auxInverse = (auxCh & 0xC0) == 0x00;
    bool auxFlash = (auxCh & 0xC0) == 0x40;
    emitCharacterDots(col * 14, charLine, auxCh, auxInverse, auxFlash, vs, true);

    // Main memory character (odd columns in display)
    uint8_t mainCh = mmu_.readRAM(addr + pageOffset, false);
    bool mainInverse = (mainCh & 0xC0) == 0x00;
    bool mainFlash = (mainCh & 0xC0) == 0x40;
    emitCharacterDots(col * 14 + 7, charLine, mainCh, mainInverse, mainFlash, vs,
                      true);
  }
}

void Video::emitLoResScanline(int scanline, int startCol, int endCol,
                              const VideoSwitchState &vs) {
  int textRow = scanline / 8;
  int lineInRow = scanline % 8;
  if (textRow >= 24) return;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getTextAddress(textRow, col);

    uint8_t colorByte;
    if (vs.page2 && !vs.store80) {
      colorByte = mmu_.readRAM(addr + 0x0400, false);
    } else {
      colorByte = mmu_.readRAM(addr, false);
    }

    uint8_t nibble = (lineInRow < 4) ? (colorByte & 0x0F)
                                     : ((colorByte >> 4) & 0x0F);

    // LORES is the one mode that does not shift bits out serially: the hardware
    // gates the nibble with the four-phase colour clock directly, so dot x
    // carries nibble bit (x mod 4). Indexing by absolute dot position is what
    // makes a colour look the same in every column even though a 14-dot cell is
    // three and a half subcarrier cycles long.
    int base = col * 14;
    setKind(base, base + 14, ntsc::IdealKind::CELL);
    for (int px = 0; px < 14; px++) {
      int x = base + px;
      setDot(x, (nibble >> (x & 3)) & 1);
    }
  }
}

void Video::emitHiResScanline(int scanline, int startCol, int endCol,
                              const VideoSwitchState &vs) {
  if (scanline >= machine_->timing.visibleScanlines) return;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getHiResAddress(scanline, col);

    uint8_t dataByte;
    if (vs.page2 && !vs.store80) {
      dataByte = mmu_.readRAM(addr + 0x2000, false);
    } else {
      dataByte = mmu_.readRAM(addr, false);
    }

    const int base = col * 14;
    setKind(base, base + 14, ntsc::IdealKind::DOT_GATED);

    // The high bit is not data. It delays the whole byte through an extra flop,
    // pushing its seven pixels one 14 MHz dot to the right — half a HIRES pixel.
    // That shift is the entire mechanism behind orange and blue: the same dot
    // pattern lands on the opposite subcarrier phase. The vacated dot is not
    // blank, it holds whatever the shift register was already outputting.
    const int delay = (dataByte & 0x80) ? 1 : 0;
    if (delay) {
      setDot(base, getDot(base - 1));
    }

    for (int bit = 0; bit < 7; bit++) {
      const uint8_t on = (dataByte >> bit) & 1;
      const int x = base + delay + bit * 2;
      setDot(x, on);
      setDot(x + 1, on);
    }
  }
}

void Video::emitDoubleLoResScanline(int scanline, int startCol, int endCol,
                                    const VideoSwitchState &vs) {
  int textRow = scanline / 8;
  int lineInRow = scanline % 8;
  if (textRow >= 24) return;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getTextAddress(textRow, col);

    uint8_t auxByte = mmu_.readRAM(addr, true);
    uint8_t mainByte = mmu_.readRAM(addr, false);

    uint8_t auxNibble = (lineInRow < 4) ? (auxByte & 0x0F)
                                        : ((auxByte >> 4) & 0x0F);
    uint8_t mainNibble = (lineInRow < 4) ? (mainByte & 0x0F)
                                         : ((mainByte >> 4) & 0x0F);

    // Same absolute-phase rule as LORES, plus the double-resolution path's one
    // dot of extra delay (see DOUBLE_RES_DELAY). The aux half occupies the
    // first seven dots of the cell, the main half the second seven.
    int base = col * 14 + DOUBLE_RES_DELAY;
    setKind(col * 14, col * 14 + 14, ntsc::IdealKind::CELL);
    for (int px = 0; px < 7; px++) {
      int x = base + px;
      setDot(x, (auxNibble >> ((x - DOUBLE_RES_DELAY) & 3)) & 1);
    }
    for (int px = 7; px < 14; px++) {
      int x = base + px;
      setDot(x, (mainNibble >> ((x - DOUBLE_RES_DELAY) & 3)) & 1);
    }
  }
}

void Video::emitDoubleHiResScanline(int scanline, int startCol, int endCol,
                                    const VideoSwitchState &vs) {
  if (scanline >= machine_->timing.visibleScanlines) return;

  uint16_t pageOffset = (vs.page2 && !vs.store80) ? 0x2000 : 0;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getHiResAddress(scanline, col) + pageOffset;
    uint8_t auxByte = mmu_.readRAM(addr, true);
    uint8_t mainByte = mmu_.readRAM(addr, false);

    // In double hi-res the shift register runs at the full dot rate and all
    // seven data bits of each byte get one dot. The high bit carries no byte
    // delay here — that mechanism belongs to single hi-res only — but the
    // whole 80-column path is a dot late, which is what DOUBLE_RES_DELAY is.
    int base = col * 14 + DOUBLE_RES_DELAY;
    setKind(col * 14, col * 14 + 14, ntsc::IdealKind::CELL);
    for (int bit = 0; bit < 7; bit++) {
      setDot(base + bit, (auxByte >> bit) & 1);
      setDot(base + 7 + bit, (mainByte >> bit) & 1);
    }
  }
}

// ============================================================================
// Decode stage
//
// The signal is complete once every segment of a scanline has emitted its dots.
// Only then is there something to decode — a decoder needs the dots either side
// of a pixel, so this cannot run per segment.
// ============================================================================

void Video::beginScanline() {
  dots_.fill(0);
  // Anything no emitter covers is unlit, and an unlit dot-gated dot is black.
  idealKind_.fill(ntsc::IdealKind::DOT_GATED);
}

bool Video::burstForScanline(int scanline, const VideoSwitchState &vs) const {
  // A machine that never inhibits burst — a II+ — sends a reference on every
  // line, which is why its text fringes green and violet in every mode.
  if (!machine_->caps.inhibitsBurstInText) return true;

  const bool textLine =
      vs.text ||
      (vs.mixed && scanline >= machine_->timing.mixedModeTextScanline);
  return !textLine;
}

void Video::endScanline(int scanline) {
  if (scanline < 0 || scanline >= machine_->timing.visibleScanlines) return;

  uint32_t line[ntsc::VISIBLE_DOTS];

  // Note this passes chromaEnabled_, not burst_. The burst decides whether the
  // machine sends a reference on this line; the killer decides whether the
  // receiver is decoding colour at all, and it works a field at a time.
  switch (colorMode_) {
  case VideoColorMode::MONOCHROME:
    ntsc::decodeMonochrome(dots_.data(), getMonochromeColor(true),
                           getMonochromeColor(false), line);
    break;
  case VideoColorMode::PIXEL_EXACT:
    ntsc::decodeIdeal(dots_.data(), idealKind_.data(), chromaEnabled_, false,
                      line);
    break;
  case VideoColorMode::RGB_MONITOR:
    ntsc::decodeIdeal(dots_.data(), idealKind_.data(), chromaEnabled_, true,
                      line);
    break;
  case VideoColorMode::COMPOSITE:
    ntsc::decodeComposite(dots_.data(), chromaEnabled_, line);
    break;
  }

  // A scanline occupies `lineDoubling` framebuffer rows (192 lines doubled to
  // 384) and each row is one pixel per emitted dot.
  const int rowBytes = machine_->display.pixelWidth * 4;
  const size_t row = static_cast<size_t>(scanline) *
                     static_cast<size_t>(machine_->display.lineDoubling) *
                     static_cast<size_t>(rowBytes);
  uint8_t *dst = framebuffer_.data() + row;
  for (int x = 0; x < ntsc::VISIBLE_DOTS; x++) {
    const uint32_t c = line[x];
    const size_t o = static_cast<size_t>(x) * 4;
    dst[o + 0] = (c >> 16) & 0xFF;
    dst[o + 1] = (c >> 8) & 0xFF;
    dst[o + 2] = c & 0xFF;
    dst[o + 3] = (c >> 24) & 0xFF;
  }
  for (int copy = 1; copy < machine_->display.lineDoubling; copy++) {
    std::memcpy(dst + static_cast<size_t>(copy) * rowBytes, dst, rowBytes);
  }
}

// ============================================================================
// Scanline segment dispatcher
// ============================================================================

void Video::renderScanlineSegment(int scanline, int startCol, int endCol,
                                   const VideoSwitchState &vs) {
  if (scanline >= machine_->timing.visibleScanlines || startCol >= endCol)
    return;

  // Mixed mode: the last band of scanlines always renders as text
  if (vs.mixed && scanline >= machine_->timing.mixedModeTextScanline &&
      !vs.text) {
    if (vs.col80) {
      emitText80Scanline(scanline, startCol, endCol, vs);
    } else {
      emitText40Scanline(scanline, startCol, endCol, vs);
    }
    return;
  }

  if (vs.text) {
    if (vs.col80) {
      emitText80Scanline(scanline, startCol, endCol, vs);
    } else {
      emitText40Scanline(scanline, startCol, endCol, vs);
    }
  } else if (vs.hires) {
    if (vs.col80 && !vs.an3) {
      emitDoubleHiResScanline(scanline, startCol, endCol, vs);
    } else {
      emitHiResScanline(scanline, startCol, endCol, vs);
    }
  } else {
    if (vs.col80 && !vs.an3) {
      emitDoubleLoResScanline(scanline, startCol, endCol, vs);
    } else {
      emitLoResScanline(scanline, startCol, endCol, vs);
    }
  }
}

// ============================================================================
// Progressive per-scanline rendering
// ============================================================================

void Video::renderScanlineWithChanges(int scanline) {
  // Horizontal timing: a scanline starts with its blanking interval, then one
  // cycle per visible column.
  const auto &timing = machine_->timing;

  uint32_t scanlineStartCycle = scanline * timing.cyclesPerScanline;
  uint32_t visibleStartCycle = scanlineStartCycle + timing.hblankCycles;
  uint32_t scanlineEndCycle = scanlineStartCycle + timing.cyclesPerScanline;

  beginScanline();

  // Phase 1: Consume hblank changes (cycles 0-24) and any earlier changes
  while (changeIdx_ < switchChangeCount_) {
    uint32_t changeCycle = switchChanges_[changeIdx_].cycleOffset;
    if (changeCycle >= visibleStartCycle) {
      break; // Change is in visible area or a later scanline
    }
    currentRenderState_ = switchChanges_[changeIdx_].state;
    changeIdx_++;
  }

  // The colour burst is generated during horizontal blanking, so whether this
  // line carries one is settled by the state at the end of hblank — before any
  // mid-line switch change can take effect.
  burst_ = burstForScanline(scanline, currentRenderState_);
  if (burst_) {
    burstSeenThisFrame_ = true;
  }

  // Phase 2: Process visible-area changes (one cycle per visible column)
  const int visibleColumns = timing.visibleColumns;
  int col = 0;
  while (changeIdx_ < switchChangeCount_) {
    uint32_t changeCycle = switchChanges_[changeIdx_].cycleOffset;
    if (changeCycle >= scanlineEndCycle) {
      break; // Belongs to a later scanline
    }

    int changeCol = static_cast<int>(changeCycle - visibleStartCycle);
    if (changeCol > visibleColumns) changeCol = visibleColumns;

    if (changeCol > col) {
      renderScanlineSegment(scanline, col, changeCol, currentRenderState_);
      col = changeCol;
    }

    currentRenderState_ = switchChanges_[changeIdx_].state;
    changeIdx_++;
  }

  // Render remaining visible columns
  if (col < visibleColumns) {
    renderScanlineSegment(scanline, col, visibleColumns, currentRenderState_);
  }

  endScanline(scanline);
}

void Video::renderUpToCycle(uint64_t currentCycle) {
  if (currentCycle <= frameStartCycle_) return;

  uint64_t frameCycle = currentCycle - frameStartCycle_;

  // Render scanlines whose 65 cycles are fully complete.
  // frameCycle / cyclesPerScanline gives the number of complete scanlines,
  // so targetScanline = completedScanlines - 1 is the last fully-elapsed one.
  // This ensures all CPU writes during a scanline are captured before we
  // read video memory for that scanline (critical for raster bar effects).
  int completedScanlines =
      static_cast<int>(frameCycle / machine_->timing.cyclesPerScanline);
  int targetScanline = completedScanlines - 1;
  const int lastVisible = machine_->timing.visibleScanlines - 1;
  if (targetScanline > lastVisible) targetScanline = lastVisible;

  while (lastRenderedScanline_ < targetScanline) {
    lastRenderedScanline_++;
    renderScanlineWithChanges(lastRenderedScanline_);
  }
}

// ============================================================================
// Frame rendering
// ============================================================================

void Video::renderFrame() {
  // Update flash state
  flashCounter_++;
  if (flashCounter_ >= FLASH_RATE) {
    flashCounter_ = 0;
    flashState_ = !flashState_;
  }

  // Finish any remaining unrendered scanlines (progressive rendering
  // may have already handled most of them during CPU execution)
  while (lastRenderedScanline_ < 191) {
    lastRenderedScanline_++;
    renderScanlineWithChanges(lastRenderedScanline_);
  }

  frameDirty_ = true;
}

void Video::forceRenderFrame() {
  VideoSwitchState vs = captureVideoState();

  // This renders a whole field from one state, so the killer can be settled up
  // front instead of lagging a frame behind as it does during live rendering.
  const int visibleScanlines = machine_->timing.visibleScanlines;
  const int visibleColumns = machine_->timing.visibleColumns;

  chromaEnabled_ = false;
  for (int scanline = 0; scanline < visibleScanlines; scanline++) {
    if (burstForScanline(scanline, vs)) {
      chromaEnabled_ = true;
      break;
    }
  }
  burstSeenThisFrame_ = chromaEnabled_;

  for (int scanline = 0; scanline < visibleScanlines; scanline++) {
    beginScanline();
    burst_ = burstForScanline(scanline, vs);
    renderScanlineSegment(scanline, 0, visibleColumns, vs);
    endScanline(scanline);
  }
  frameDirty_ = true;
}

// ============================================================================
// Color settings
// ============================================================================

void Video::setColorMode(VideoColorMode mode) {
  if (mode != VideoColorMode::MONOCHROME) {
    preMonochromeMode_ = mode;
  }
  colorMode_ = mode;
  frameDirty_ = true;
}

void Video::setMonochrome(bool mono) {
  setColorMode(mono ? VideoColorMode::MONOCHROME : preMonochromeMode_);
}

uint32_t Video::getMonochromeColor(bool on) const {
  if (!on) {
    return 0xFF000000; // Black
  }

  if (greenPhosphor_) {
    return 0xFF33FF33; // Green phosphor
  }
  return 0xFFFFFFFF; // White
}

uint16_t Video::getTextAddress(int row, int col) const {
  return 0x0400 + TEXT_ROW_OFFSETS[row] + col;
}

uint16_t Video::getHiResAddress(int row, int col) const {
  int block = row / 8;
  int line = row % 8;
  return 0x2000 + TEXT_ROW_OFFSETS[block] + line * 0x400 + col;
}

} // namespace a2e
