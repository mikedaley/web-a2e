/*
 * types.hpp - Shared constants, types, and color palettes for the Apple IIe emulator
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace a2e {

// CPU variant types
//
// Lives here rather than in cpu6502.hpp so a machine profile can name the CPU a
// machine is fitted with without dragging in the whole processor header.
enum class CPUVariant { NMOS_6502, CMOS_65C02 };

// ============================================================================
// Apple //e machine constants
//
// These describe one machine, and the authoritative description of a machine is
// now MachineProfile (machine/machine_profile.hpp). They survive as constexpr
// because they size std::array members at compile time, which a runtime profile
// lookup cannot do. machine_profile.hpp static_asserts every one of them
// against the //e profile, so the two cannot drift.
//
// Prefer the profile in new code. Reach for these only where a compile-time
// constant is genuinely required.
// ============================================================================

// Memory size constants
constexpr size_t MAIN_RAM_SIZE = 64 * 1024; // 64KB main RAM
constexpr size_t AUX_RAM_SIZE = 64 * 1024;  // 64KB auxiliary RAM
constexpr size_t ROM_SIZE = 16 * 1024;      // 16KB ROM ($C000-$FFFF)
constexpr size_t CHAR_ROM_SIZE = 8 * 1024;  // 8KB character ROM (US + UK sets)
constexpr size_t DISK_ROM_SIZE = 256;       // 256 bytes Disk II ROM

// Display constants
constexpr int SCREEN_WIDTH = 560;  // 280 * 2 for double-width pixels
constexpr int SCREEN_HEIGHT = 384; // 192 * 2 for double-height pixels
constexpr int FRAMEBUFFER_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT * 4; // RGBA

// Timing constants
constexpr double CPU_CLOCK_HZ = 1023000.0; // 1.023 MHz
constexpr int AUDIO_SAMPLE_RATE = 48000;
constexpr double CYCLES_PER_SAMPLE =
    CPU_CLOCK_HZ / AUDIO_SAMPLE_RATE; // ~21.3125

constexpr int CYCLES_PER_SCANLINE = 65;
constexpr int SCANLINES_PER_FRAME = 262;
constexpr int CYCLES_PER_FRAME =
    CYCLES_PER_SCANLINE * SCANLINES_PER_FRAME; // 17030

// Video mode flags
enum class VideoMode : uint8_t {
  TEXT_40 = 0,
  TEXT_80,
  LORES,
  HIRES,
  DOUBLE_LORES,
  DOUBLE_HIRES
};

// How the 14.31818 MHz dot stream is turned into pixels.
//
// The Apple IIe emits a 1-bit serial video signal; every colour you see is made
// by the *receiver*, not by the machine. These are four receivers.
enum class VideoColorMode : uint8_t {
  MONOCHROME = 0, // One phosphor: dot on/off, no decode at all
  PIXEL_EXACT,    // Idealised 4-dot decode, hard edges, no filtering
  RGB_MONITOR,    // Idealised decode plus the mild chroma smoothing of an RGB set
  COMPOSITE       // Full NTSC demodulation — what a composite monitor really shows
};

// Soft switch state - comprehensive Apple IIe soft switches
struct SoftSwitches {
  // Display switches ($C050-$C057)
  bool text = true;   // $C050/$C051: TEXT/GRAPHICS mode
  bool mixed = false; // $C052/$C053: Mixed mode (4 lines text at bottom)
  bool page2 = false; // $C054/$C055: PAGE1/PAGE2
  bool hires = false; // $C056/$C057: LORES/HIRES

  // 80-column switches ($C00C-$C00F)
  bool col80 = false;      // $C00C/$C00D: 40/80 column mode
  bool altCharSet = false; // $C00E/$C00F: Primary/alternate character set

  // Memory switches ($C000-$C00B)
  bool store80 = false;   // $C000/$C001: 80STORE
  bool ramrd = false;     // $C002/$C003: RAMRD - aux RAM read
  bool ramwrt = false;    // $C004/$C005: RAMWRT - aux RAM write
  bool intcxrom = false;  // $C006/$C007: INTCXROM - internal slot ROM
  bool altzp = false;     // $C008/$C009: ALTZP - aux zero page/stack
  bool slotc3rom = false; // $C00A/$C00B: SLOTC3ROM - slot 3 ROM
  bool intc8rom = false;  // Internal $C800-$CFFF ROM active

  // Language card ($C080-$C08F)
  bool lcram = false;      // LC RAM enabled for read
  bool lcram2 = false;     // LC RAM bank 2
  bool lcwrite = false;    // LC RAM write-enabled
  bool lcprewrite = false; // LC pre-write state

  // Annunciators ($C058-$C05F)
  bool an0 = false; // $C058/$C059: Annunciator 0
  bool an1 = false; // $C05A/$C05B: Annunciator 1
  bool an2 = false; // $C05C/$C05D: Annunciator 2
  bool an3 = false; // $C05E/$C05F: Annunciator 3 (DHIRES control)

  // I/O state (read-only status)
  bool vblBar = false; // $C019: Vertical blank (true = in VBL)

  // Button states ($C061-$C063)
  bool button0 = false; // $C061: Open Apple / Button 0
  bool button1 = false; // $C062: Closed Apple / Button 1
  bool button2 = false; // $C063: Button 2 / Shift key state

  // Keyboard
  uint8_t keyLatch = 0;   // $C000: Keyboard latch (bit 7 = key available)
  bool keyStrobe = false; // $C010: Keyboard strobe (key available)

  // Paddle/Joystick
  uint8_t paddle0 = 128; // $C064: PDL0 value (0-255, 128 = center)
  uint8_t paddle1 = 128; // $C065: PDL1 value
  uint8_t paddle2 = 128; // $C066: PDL2 value
  uint8_t paddle3 = 128; // $C067: PDL3 value

  // Cassette (stub)
  bool cassetteOut = false; // $C020: Cassette output
  bool cassetteIn = false;  // $C060: Cassette input

  // IOU / DHIRES
  bool ioudis = false; // $C07E/$C07F: IOU disable (IIc specific)
  bool dhires = false; // Double hi-res mode (AN3 off + 80COL + HIRES)
};

// Disk drive state
struct DriveState {
  bool motorOn = false;
  bool writeMode = false;
  int currentTrack = 0; // 0-34
  int currentPhase = 0; // Stepper motor phase
  int headPosition = 0; // Bit position on track
  uint8_t dataLatch = 0;
  bool diskInserted = false;
};

// CPU status flags are defined in cpu/cpu6502.hpp

// Color palette for Apple II
constexpr std::array<uint32_t, 16> LORES_COLORS = {{
    0xFF000000, // 0: Black
    0xFFE31E60, // 1: Magenta
    0xFF604EBD, // 2: Dark Blue
    0xFFFF44FD, // 3: Purple
    0xFF00A360, // 4: Dark Green
    0xFF9C9C9C, // 5: Grey 1
    0xFF14CFFD, // 6: Medium Blue
    0xFFD0C3FF, // 7: Light Blue
    0xFF607203, // 8: Brown
    0xFFFF6A3C, // 9: Orange
    0xFF9C9C9C, // 10: Grey 2
    0xFFFFA0D0, // 11: Pink
    0xFF14F53C, // 12: Light Green
    0xFFD0DD8D, // 13: Yellow
    0xFF72FFD0, // 14: Aqua
    0xFFFFFFFF  // 15: White
}};

// HiRes artifact colors (NTSC-accurate values)
// Group 1 (high bit = 0): Black, Green, Violet, White
// Group 2 (high bit = 1): Black, Orange, Blue, White
constexpr std::array<uint32_t, 6> HIRES_COLORS = {{
    0xFF000000, // 0: Black
    0xFF14F53C, // 1: Green (odd pixels, high bit = 0)
    0xFFFF44FD, // 2: Violet (even pixels, high bit = 0)
    0xFFFFFFFF, // 3: White
    0xFF14CFFD, // 4: Blue (even pixels, high bit = 1)
    0xFFFF6A3C  // 5: Orange (odd pixels, high bit = 1)
}};

// Double Lo-Res / Double Hi-Res color palette
// Different from Lo-Res due to 14MHz dot rate vs 7MHz (changes NTSC phase
// relationship) Based on AppleWin's DoubleHiresPalIndex mapping See:
// https://github.com/AppleWin/AppleWin/blob/master/source/RGBMonitor.cpp
constexpr std::array<uint32_t, 16> DLGR_COLORS = {{
    0xFF000000, // 0: Black        (LORES[0])
    0xFF604EBD, // 1: Dark Blue    (LORES[2])
    0xFF00A360, // 2: Dark Green   (LORES[4])
    0xFF14CFFD, // 3: Medium Blue  (LORES[6])
    0xFF607203, // 4: Brown        (LORES[8])
    0xFF9C9C9C, // 5: Grey         (LORES[10])
    0xFF14F53C, // 6: Light Green  (LORES[12])
    0xFF72FFD0, // 7: Aqua         (LORES[14])
    0xFFE31E60, // 8: Magenta      (LORES[1])
    0xFFFF44FD, // 9: Purple       (LORES[3])
    0xFF9C9C9C, // 10: Grey        (LORES[5])
    0xFFD0C3FF, // 11: Light Blue  (LORES[7])
    0xFFFF6A3C, // 12: Orange      (LORES[9])
    0xFFFFA0D0, // 13: Pink        (LORES[11])
    0xFFD0DD8D, // 14: Yellow      (LORES[13])
    0xFFFFFFFF  // 15: White       (LORES[15])
}};

// Snapshot of video-relevant soft switch state for per-scanline rendering
struct VideoSwitchState {
  bool text;
  bool mixed;
  bool page2;
  bool hires;
  bool col80;
  bool altCharSet;
  bool store80;
  bool an3;
};

// Records a video switch change at a specific cycle within a frame
struct VideoSwitchChange {
  uint32_t cycleOffset;       // Cycle offset from frame start
  VideoSwitchState state;     // Full snapshot after the change
};

} // namespace a2e
