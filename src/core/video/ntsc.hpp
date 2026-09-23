/*
 * ntsc.hpp - Composite video decoding for the Apple IIe
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../types.hpp"
#include <array>
#include <cstdint>

namespace a2e {
namespace ntsc {

// ============================================================================
// The dot domain
//
// An Apple IIe does not output pixels. It outputs one bit per 14.31818 MHz dot,
// and four dots make exactly one cycle of the 3.579545 MHz colour subcarrier.
// Every colour the machine appears to produce is manufactured by the receiver
// from that 1-bit stream, which is why the same bytes look different on a
// composite monitor, an RGB card and a green screen.
//
// A visible scanline is 40 columns x 14 dots = 560 dots, which is exactly the
// framebuffer width — the signal maps 1:1 onto pixels with no resampling.
// ============================================================================

constexpr int DOTS_PER_SUBCARRIER = 4;
constexpr int VISIBLE_DOTS = SCREEN_WIDTH; // 560

// Demodulation window, in dots. 15 (~1.05 us) is the widest that still fits in
// a single lookup table: 2^15 window states x 4 subcarrier phases x 4 bytes =
// 512 KB. It also sets the floor on how narrow a filter can be — a 15-tap FIR
// at 14.3 MHz cannot resolve features below about 1 MHz, which is the real
// reason a composite picture is soft rather than a shortcoming of this model.
constexpr int WINDOW = 15;
constexpr int CENTER = WINDOW / 2; // 7 dots either side of the output dot
constexpr int WINDOW_STATES = 1 << WINDOW;
constexpr uint32_t WINDOW_MASK = WINDOW_STATES - 1;

// A set high bit in the last HGR byte of a line delays its dots past the
// visible area, so the buffer needs somewhere for them to land.
constexpr int SPILL = 4;

// Per-scanline dot buffer. CENTER dots of margin either side keep the
// demodulation window from running off the ends, so no edge special-casing.
constexpr int DOT_ORIGIN = CENTER; // visible dot x lives at dots[DOT_ORIGIN + x]
constexpr int DOT_BUFFER = CENTER + VISIBLE_DOTS + SPILL + CENTER;

// ============================================================================
// Monitor calibration
//
// These three numbers are not taste. They were fitted by least squares against
// the entries of the Apple II palette that are physically self-consistent —
// black, white, both greys, and the four single-bit LORES hues (magenta, dark
// blue, dark green, brown), whose phases a real decoder fixes exactly 90 apart.
// Two independent fits, one over the LORES palette and one over the HGR
// artifact colours, agreed on the phase to within a quarter of a degree.
//
// The multi-bit palette entries were deliberately left out of the fit: they had
// been hand-tuned over the years and sit 17-24 degrees off where a real
// demodulator puts them, which no single phase reference can reproduce.
// ============================================================================

// Phase of the colour burst reference, in radians.
constexpr double BURST_PHASE = 0.601495; // 34.463 degrees

// A real set does not run at the full theoretical chroma amplitude.
constexpr double CHROMA_GAIN = 0.8211;

// The NTSC signal is gamma-encoded for a CRT; the framebuffer is read as sRGB.
// The residual between the two is a single exponent on luma.
constexpr double LUMA_GAMMA = 0.6990;

// ============================================================================
// Decoders
//
// Each takes the whole DOT_BUFFER-sized dot array for one scanline and writes
// VISIBLE_DOTS pixels as 0xAARRGGBB. `burst` is the colour burst gate: a IIe
// inhibits burst in text mode, which is the actual reason text is crisp and
// colourless while graphics fringe.
// ============================================================================

// Full NTSC demodulation — what a composite monitor really shows.
void decodeComposite(const uint8_t *dots, bool burst, uint32_t *out);

// How a run of dots should be turned into colour when no signal is being
// simulated. Each mode emitter tags the dots it writes, because "what colour is
// this" is a question about the mode's own semantics, not about the waveform.
//
// These name mechanisms rather than modes, because the split does not fall
// along mode lines: a text stroke and a HIRES pixel are both two dots wide and
// both take their colour the same way.
enum class IdealKind : uint8_t {
  // Unlit dots are black; lit ones take the artifact colour their run implies.
  // Used by text and HIRES — anything whose dots are drawn rather than encoded.
  DOT_GATED = 0,
  // Flat colour across the aligned four-dot group, which is how LORES, DLORES
  // and DHGR carry an actual colour value rather than a drawn shape.
  CELL,
  // Drawn dots to a receiver, a colour to SOLID. HIRES only.
  //
  // A HIRES picture is both of those things at once, and which one it is
  // depends on who is looking. To a monitor it is a drawn shape: two lit dots
  // are one pixel and its colour is an artifact of where they fell, which is
  // what DOT_GATED models and what PIXEL_EXACT, RGB_MONITOR and COMPOSITE all
  // want. But the artist chose those dots FOR their colour — a solid violet
  // field is $55 and $2A alternating, which lights only half the dots — so
  // gating on lit dots renders that field as violet and black stripes when
  // what was drawn was a violet field.
  //
  // So every receiver sees this exactly as DOT_GATED, and SOLID paints the
  // colour the emitter worked out for each pixel from the BITS - see
  // Video::emitHiResScanline for the rule.
  DOT_GATED_CELL,
};

// Idealised decode — no composite effects at all.
//
// This is not a demodulator and deliberately does not behave like one. Nothing
// bleeds: an unlit dot is black, and colour never extends past the pixels that
// are actually on. HIRES uses run length rather than byte alignment to decide
// between an artifact colour and white, so it stays correct across the half-dot
// shift without needing to know where byte cells begin.
//
// `smooth` adds the mild chroma-only softening of an analogue RGB output stage
// (RGB Monitor); without it the result is maximally sharp (Pixel Exact).
void decodeIdeal(const uint8_t *dots, const IdealKind *kind, bool chroma,
                 bool smooth, uint32_t *out);

// No decoder: the picture the program meant.
//
// Where the emitter tagged a dot CELL or DOT_GATED_CELL it also said which
// palette entry that dot carries, and this paints that entry over the whole
// cell — lit dots, unlit dots, edge dots, all of it — so a lone LORES cell on
// black is one colour from its first dot to its last, which no four-dot
// decode can manage on a cell seven dots wide, and a HIRES colour field is
// one colour with no stripes in it.
//
// Text stays dot-gated and is treated as decodeIdeal treats it, with no
// smoothing: black where unlit, white where lit.
//
// `cell` is the palette index per dot and is only read where kind is CELL or
// DOT_GATED_CELL.
void decodeSolid(const uint8_t *dots, const IdealKind *kind,
                 const uint8_t *cell, uint32_t *out);

// One phosphor, no decode at all: the dot stream straight to the screen.
void decodeMonochrome(const uint8_t *dots, uint32_t on, uint32_t off,
                      uint32_t *out);

// The 16 ideal colours, derived from the calibration above. Shared by the
// digital decoders so every mode agrees on hue.
const std::array<uint32_t, 16> &idealPalette();

// Demodulate a single dot directly, without the lookup table. This is the
// reference implementation the table is built from; tests use it to prove the
// table is an exact memoisation rather than an approximation.
uint32_t demodulateReference(const uint8_t *window, int phase, bool burst);

} // namespace ntsc
} // namespace a2e
