/*
 * iigs_spec.hpp - The numbers that belong to a IIgs and to nothing else
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstddef>
#include <cmath>
#include <cstdint>

namespace a2e::iigs {

// ============================================================================
// Why these are not in MachineProfile
//
// MachineProfile is the vocabulary every machine here shares: a clock, a
// scanline count, how much RAM answers, which slots exist. A IIgs has all of
// those and they are in its profile.
//
// What is below has no meaning for a //e, a II+ or a //c. There is no 8-bit
// Apple II with a second clock rate, a bank of RAM the CPU reaches only through
// a 24-bit address, a shadowing map, a 4096-colour palette or 64KB of sound
// RAM. Putting them in the shared struct would be putting a IIgs's parts in
// every other machine's description and then explaining, in each of them, that
// the numbers do not apply — which is the kind of thing that rots.
//
// So the split is: what the machines have in common is described in common,
// and what only a IIgs has is described here, next to the code that reads it.
// ============================================================================

// ----------------------------------------------------------------------------
// The two sides of the machine
//
// A IIgs is two computers sharing an address space. The FPI (Fast Processor
// Interface) side is the 65816 and its fast RAM and ROM, running at 2.8MHz.
// The Mega II side is an entire //e — the same video generator, the same soft
// switches, the same 128KB — running at 1.023MHz in banks $E0 and $E1.
//
// Every access the 65816 makes to the slow side runs at the slow clock, which
// is why a IIgs is not simply "a //e at 2.8MHz": the speed a program gets
// depends on where it is reading.
// ----------------------------------------------------------------------------
inline constexpr double FAST_CLOCK_HZ = 2800000.0;
inline constexpr double SLOW_CLOCK_HZ = 1023000.0;

// ----------------------------------------------------------------------------
// Memory
//
// Banks $00-$7F are RAM: 256KB on a ROM 01 motherboard, up to 8MB with a card
// in the memory expansion slot. Banks $E0 and $E1 are the Mega II's 128KB, and
// are the ones the //e-mode video reads. Banks $F0-$FF are ROM: 128KB on ROM
// 01 (banks $FE-$FF), 256KB on ROM 3 (banks $FC-$FF).
//
// Shadowing is what keeps the two sides in step: writes to the display pages of
// banks $00 and $01 are copied into $E0 and $E1, so a program can run in fast
// RAM and still be seen by a video generator that only ever looks at the slow
// side. Which pages are shadowed is a register ($C035), not a constant.
// ----------------------------------------------------------------------------
inline constexpr size_t BANK_SIZE = 64 * 1024;

inline constexpr size_t FAST_RAM_SIZE_ROM01 = 256 * 1024; // What a ROM 01 shipped with
inline constexpr size_t FAST_RAM_SIZE_MAX = 8 * 1024 * 1024;

/**
 * Round a requested amount of fast RAM to something a IIgs could have.
 *
 * RAM arrives a bank at a time — 64K — and the machine cannot have less than
 * the 256K soldered to a ROM 01's board or more than the 24-bit bus can reach.
 * Everything between is a memory expansion card, which is what most IIgs
 * owners fitted and what anything bigger than ProDOS 8 expects to find.
 */
inline constexpr size_t clampFastRamSize(size_t bytes) {
  if (bytes < FAST_RAM_SIZE_ROM01) return FAST_RAM_SIZE_ROM01;
  if (bytes > FAST_RAM_SIZE_MAX) return FAST_RAM_SIZE_MAX;
  return bytes - (bytes % BANK_SIZE);
}
inline constexpr size_t SLOW_RAM_SIZE = 128 * 1024; // Banks $E0-$E1: the Mega II's

inline constexpr uint8_t SLOW_BANK_MAIN = 0xE0;
inline constexpr uint8_t SLOW_BANK_AUX = 0xE1;

inline constexpr size_t ROM_SIZE_ROM01 = 128 * 1024; // Banks $FE-$FF
inline constexpr size_t ROM_SIZE_ROM3 = 256 * 1024;  // Banks $FC-$FF
inline constexpr uint8_t ROM_TOP_BANK = 0xFF;

// ----------------------------------------------------------------------------
// Super Hi-Res
//
// A second video system, reading bank $E1 from $2000 to $9FFF: 32KB of pixels
// followed by a scanline control byte and sixteen palettes for each of the 200
// lines. Every line chooses its own mode and palette, which is why a IIgs
// screen can be 320 and 640 pixels wide at the same time.
// ----------------------------------------------------------------------------
inline constexpr uint16_t SHR_PIXEL_BASE = 0x2000;   // In bank $E1
inline constexpr uint16_t SHR_SCB_BASE = 0x9D00;     // One control byte a line
inline constexpr uint16_t SHR_PALETTE_BASE = 0x9E00; // 16 palettes of 16 colours
inline constexpr int SHR_LINES = 200;
inline constexpr int SHR_BYTES_PER_LINE = 160; // 320 pixels at 4bpp, or 640 at 2bpp
inline constexpr int SHR_PALETTE_COUNT = 16;
inline constexpr int SHR_PALETTE_ENTRIES = 16;

// A palette entry is $0RGB: four bits each, so 4096 colours to choose from.
inline constexpr int SHR_COLOUR_DEPTH_BITS = 4;

// ----------------------------------------------------------------------------
// The raster
//
// What a monitor is sent is more than the picture. The counters are the
// //e's, and Sather's Table 3.2 (Understanding the Apple IIe) is the source
// for where everything falls in them:
//
//   Horizontal: 65 states, $00 then $40-$7F, one state a cycle.
//     $58-$7F  the picture, 40 cycles
//     $00,$40-$47  after the picture, 9 cycles
//     $48-$4B  horizontal sync, 4 cycles
//     $4C-$4F  colour burst, 4 cycles
//     $50-$57  before the picture, 8 cycles
//   Vertical: 262 lines, $FA-$FF then $100-$1FF.
//     lines 0-191 the //e's picture ($100-$1BF); Super Hi-Res draws 0-199
//     line 192 VBL; lines 224-227 vertical sync ($1E0-$1E3)
//
// The IIgs sends border colour wherever it is sending neither picture nor
// the blanking a receiver needs, and no Apple document says where the VGC
// draws that line. This is the NTSC standard applied to the counters above:
// a front porch of 1.5us (one and a half cycles) before sync, so 7 of the 9
// cycles after the picture are border; the back porch's 1.6us after the
// burst, so 6 of the 8 cycles before the picture are border; three
// equalising lines before vertical sync, so the border ends at line 220 and
// the //e's picture leaves 29 lines of it (Super Hi-Res 21); and blanking
// through to line 242, so 19 lines of border precede the picture. The
// visible raster is therefore 53 cycles by 240 lines.
//
// That is within a cycle of every emulator that models it and none of them
// agrees exactly: GSSquared's scanner flags the same 7/12/6 and 221-242,
// MAME's driver says 6/13/6, and KEGS draws 4 cycles a side. A monitor's own
// overscan hides a cycle or two of border in any case, so the difference is
// not one a screen would show. The picture's place and size are exact.
//
// Super Hi-Res clocks 16 pixels a cycle, so the raster is 848 pixels wide;
// the //e's 14 dots a cycle cover the same width, and are stretched to it,
// because on the monitor a text screen and a Super Hi-Res screen are the
// same width. Lines are doubled, as the picture's are. One cycle in 65 is
// two dots longer than the rest, inside the blanking, and is not modelled.
// The raster is 51.9us of a 63.7us line and 240 of 262 lines, which is the
// NTSC standard's active 52.6us and 242 lines to within a percent: a 4:3
// monitor shows the whole of it, and that is the shape the profile names.
// ----------------------------------------------------------------------------
inline constexpr int SHR_PIXELS_PER_CYCLE = 16;
inline constexpr int SHR_PIXELS_PER_LINE = 640;
inline constexpr int PICTURE_CYCLES = 40;
inline constexpr int BORDER_LEFT_CYCLES = 6;
inline constexpr int BORDER_RIGHT_CYCLES = 7;
inline constexpr int BORDER_TOP_LINES = 19;
inline constexpr int BORDER_BOTTOM_LINES = 21;
inline constexpr int RASTER_LINE_DOUBLING = 2;

inline constexpr int RASTER_WIDTH =
    (BORDER_LEFT_CYCLES + PICTURE_CYCLES + BORDER_RIGHT_CYCLES) * SHR_PIXELS_PER_CYCLE;
inline constexpr int RASTER_LINES = BORDER_TOP_LINES + SHR_LINES + BORDER_BOTTOM_LINES;
inline constexpr int RASTER_HEIGHT = RASTER_LINES * RASTER_LINE_DOUBLING;

/** Where the picture's top-left pixel lands in the raster. */
inline constexpr int PICTURE_LEFT = BORDER_LEFT_CYCLES * SHR_PIXELS_PER_CYCLE;
inline constexpr int PICTURE_TOP = BORDER_TOP_LINES * RASTER_LINE_DOUBLING;

static_assert(PICTURE_CYCLES * SHR_PIXELS_PER_CYCLE == SHR_PIXELS_PER_LINE);
static_assert(RASTER_WIDTH == 848);
static_assert(RASTER_HEIGHT == 480);

// ----------------------------------------------------------------------------
// Sound
//
// An Ensoniq 5503 DOC with 32 oscillators and its own 64KB of RAM, which the
// CPU reaches only a byte at a time through a window at $C03C-$C03F. Nothing
// else in the Apple II line has anything like it: the //e's speaker is one bit
// and a Mockingboard is a pair of AY-3-8910s on a card.
// ----------------------------------------------------------------------------
inline constexpr size_t SOUND_RAM_SIZE = 64 * 1024;
inline constexpr int DOC_OSCILLATOR_COUNT = 32;

/**
 * The amplifier's gain, from the volume nibble in $C03C.
 *
 * One amplifier carries both of this machine's sound sources — the speaker and
 * the Ensoniq — so both ask this, and the ROM's bell still fades because the
 * nibble's own changes still move it.
 *
 * **It is not a straight amplitude ratio, and that is a measurement rather
 * than a preference.** The nibble drives an analogue attenuator whose taper is
 * in no document I can find, so something has to be assumed; assuming
 * `nibble / 15` put the machine 9.5dB below a //e for the same speaker click
 * at the setting the machine's own firmware boots with (0.150 peak against
 * 0.450), and a single Ensoniq oscillator at -27dBFS. A machine sold on its
 * sound does not arrive quieter than a //e out of the box, and there is no way
 * to turn it up from inside: the Control Panel hotkey is not implemented and
 * the firmware rewrites battery RAM's volume byte whenever its checksum does
 * not match.
 *
 * A cube-root taper is the assumption instead. It keeps everything the nibble
 * is for — zero is silence, fifteen is full output, and every step between is
 * ordered, so the bell's ramp down still fades — and puts the default within
 * 3dB of the other machines. Change the exponent here and both sources follow.
 */
inline double amplifierGain(uint8_t nibble) {
  const double setting = static_cast<double>(nibble & 0x0F) / 15.0;
  return setting <= 0.0 ? 0.0 : std::cbrt(setting);
}

// ----------------------------------------------------------------------------
// Battery RAM
//
// 256 bytes of settings kept alive by the battery: the slot assignments, the
// display and speed the machine comes up in, the printer's port. Read and
// written through the clock chip's serial interface, one bit at a time.
// ----------------------------------------------------------------------------
inline constexpr size_t BATTERY_RAM_SIZE = 256;

} // namespace a2e::iigs
