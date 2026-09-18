/*
 * ntsc.cpp - Composite video decoding for the Apple IIe
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "ntsc.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace a2e {
namespace ntsc {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DOT_RATE = 14318181.8; // 14.31818 MHz

// ============================================================================
// Filter design
//
// Two constraints drive these kernels, and neither of them is aesthetic.
//
// The luma path must have exact zeros at BOTH the subcarrier and its second
// harmonic. The subcarrier zero is the obvious one: a flat area of a LORES
// colour is a 3.58 MHz pattern, so any leakage into luma makes greys ripple and
// every colour come out at the wrong brightness. The 7.16 MHz zero is subtler
// but just as necessary — without it the brightness of a steady colour depends
// on which subcarrier phase a dot happens to land on, and a solid block of
// colour comes out visibly banded at a couple of levels per 255.
//
// A four-tap boxcar has zeros at both, because averaging over exactly one
// subcarrier cycle annihilates that cycle and every harmonic of it. Cascading
// it with a windowed sinc shapes the rest of the response without disturbing
// either zero. Rejecting 7.16 MHz completely is also just true to the hardware:
// no composite monitor ever had luma bandwidth beyond about 4 MHz.
//
// The chroma path needs the same zero at twice the subcarrier, because
// multiplying by the reference produces the wanted baseband term plus an image
// at 2*fsc. Four cascaded boxcars kill that image dead and land the chroma
// bandwidth near 0.85 MHz, which is where period sets sat.
// ============================================================================

std::vector<double> convolve(const std::vector<double> &a,
                             const std::vector<double> &b) {
  std::vector<double> r(a.size() + b.size() - 1, 0.0);
  for (size_t i = 0; i < a.size(); i++) {
    for (size_t j = 0; j < b.size(); j++) {
      r[i + j] += a[i] * b[j];
    }
  }
  return r;
}

std::vector<double> boxcar(int n) {
  return std::vector<double>(static_cast<size_t>(n), 1.0 / n);
}

// Hann-windowed sinc low pass, normalised to unity DC gain.
//
// Unused since the luma path stopped cascading one on top of its boxcar (see
// lumaKernel), and kept because it is the tool for shaping a response without
// disturbing a notch, which is what any future change here will want.
[[maybe_unused]] std::vector<double> hannSinc(int len, double cutoffHz) {
  std::vector<double> h(static_cast<size_t>(len));
  const double c = (len - 1) / 2.0;
  double sum = 0.0;
  for (int i = 0; i < len; i++) {
    const double n = i - c;
    const double s = (std::abs(n) < 1e-12)
                         ? 2.0 * cutoffHz / DOT_RATE
                         : std::sin(2.0 * PI * cutoffHz * n / DOT_RATE) / (PI * n);
    const double w = 0.5 - 0.5 * std::cos(2.0 * PI * i / (len - 1));
    h[static_cast<size_t>(i)] = s * w;
    sum += h[static_cast<size_t>(i)];
  }
  for (double &v : h) {
    v /= sum;
  }
  return h;
}

// Centre a kernel in a WINDOW-tap frame.
std::array<double, WINDOW> centre(const std::vector<double> &h) {
  std::array<double, WINDOW> out{};
  const int offset = (WINDOW - static_cast<int>(h.size())) / 2;
  for (size_t i = 0; i < h.size(); i++) {
    out[static_cast<size_t>(offset) + i] = h[i];
  }
  return out;
}

std::array<double, WINDOW> lumaKernel() {
  // One subcarrier cycle of integration, and nothing else: four taps, with
  // hard zeros at 3.58 and 7.16 MHz.
  //
  // This used to be cascaded with an 11-tap 4 MHz low pass, which is where a
  // composite picture's extra softness came from. The two filters agree to
  // within a decibel below 3 MHz, so the cascade bought nothing in the band
  // that carries the shape of a character; what it did was take 13 to 30 dB
  // out of the 4 to 6 MHz band, which is where the *edges* live. Text came
  // out blurred to a degree no composite monitor ever managed.
  //
  //             2MHz    3MHz    4MHz    5MHz    6MHz
  //   cascade   -4.92  -15.77  -24.62  -26.15  -45.88
  //   boxcar    -4.76  -14.02  -18.62  -11.49  -13.16
  //
  // Dropping it is also the more faithful model. A period set's luma path was
  // a trap at the subcarrier, not a brick wall at 4 MHz, and the boxcar *is*
  // that trap: averaging over exactly one colour cycle annihilates the cycle
  // and its second harmonic, which is the whole job. Nothing above 7.16 MHz
  // exists in the input to leak back in, since that is the dot rate's Nyquist.
  //
  // The centroid is unchanged at 6.5 dots, so luma keeps the same group delay
  // against chroma that it always had, and a boxcar's taps are all positive,
  // so there is no ringing to overshoot an edge.
  return centre(boxcar(4));
}

std::array<double, WINDOW> chromaKernel() {
  // Four cascaded four-tap boxcars: 13 taps, ~0.85 MHz, with hard zeros at both
  // the subcarrier and its second harmonic.
  const std::vector<double> b4 = boxcar(4);
  return centre(convolve(convolve(b4, b4), convolve(b4, b4)));
}

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

inline uint32_t packRGB(double r, double g, double b) {
  const auto q = [](double v) {
    return static_cast<uint32_t>(std::lround(clamp01(v) * 255.0));
  };
  return 0xFF000000u | (q(r) << 16) | (q(g) << 8) | q(b);
}

// YIQ to RGB, then the residual gamma between the NTSC signal and sRGB.
uint32_t yiqToRGB(double y, double i, double q) {
  y = std::pow(clamp01(y), LUMA_GAMMA);
  i *= CHROMA_GAIN;
  q *= CHROMA_GAIN;
  return packRGB(y + 0.956 * i + 0.621 * q, y - 0.272 * i - 0.647 * q,
                 y - 1.106 * i + 1.703 * q);
}

// ============================================================================
// Lookup tables
//
// The demodulated value at dot x depends on exactly two things: the 15 dots
// around it, and x mod 4 (which subcarrier phase it lands on). That is a finite
// domain, so the whole FIR can be precomputed. This is a memoisation of the
// reference implementation, not an approximation of it — test_ntsc.cpp checks
// every one of the 131072 entries against demodulateReference().
// ============================================================================

struct Tables {
  std::array<double, WINDOW> hy{};
  std::array<double, WINDOW> hc{};
  // [phase][window] -> 0xAARRGGBB, colour burst present
  std::vector<uint32_t> composite;
  // [window] -> grey level, colour burst inhibited
  std::vector<uint8_t> luma;
  std::array<uint32_t, 16> palette{};
  // Palette entries pre-split into YIQ so the RGB decoder can smooth chroma
  // without touching luma.
  std::array<double, 16> palY{};
  std::array<double, 16> palI{};
  std::array<double, 16> palQ{};

  Tables() {
    hy = lumaKernel();
    hc = chromaKernel();

    // Modulation reference per (phase, tap). A tap j of the window sitting at
    // output dot x reads the dot at absolute position x - CENTER + j, and
    // -CENTER is congruent to +1 modulo 4.
    double cosRef[4][WINDOW];
    double sinRef[4][WINDOW];
    for (int p = 0; p < 4; p++) {
      for (int j = 0; j < WINDOW; j++) {
        const double theta = PI / 2.0 * ((p + j + 1) & 3) + BURST_PHASE;
        cosRef[p][j] = 2.0 * hc[static_cast<size_t>(j)] * std::cos(theta);
        sinRef[p][j] = 2.0 * hc[static_cast<size_t>(j)] * std::sin(theta);
      }
    }

    composite.resize(static_cast<size_t>(4) * WINDOW_STATES);
    luma.resize(WINDOW_STATES);

    for (uint32_t w = 0; w < WINDOW_STATES; w++) {
      double y = 0.0;
      for (int j = 0; j < WINDOW; j++) {
        if (w & (1u << (WINDOW - 1 - j))) {
          y += hy[static_cast<size_t>(j)];
        }
      }
      luma[w] = static_cast<uint8_t>(
          std::lround(clamp01(std::pow(clamp01(y), LUMA_GAMMA)) * 255.0));

      for (int p = 0; p < 4; p++) {
        double i = 0.0;
        double q = 0.0;
        for (int j = 0; j < WINDOW; j++) {
          if (w & (1u << (WINDOW - 1 - j))) {
            i += cosRef[p][j];
            q += sinRef[p][j];
          }
        }
        composite[static_cast<size_t>(p) * WINDOW_STATES + w] = yiqToRGB(y, i, q);
      }
    }

    // The 16 ideal colours: one subcarrier cycle of a repeating 4-dot pattern,
    // demodulated with a perfect single-cycle integrator. Same phase reference
    // and same gains as the composite path, so hues agree across every mode.
    for (int v = 0; v < 16; v++) {
      double y = 0.0;
      double i = 0.0;
      double q = 0.0;
      for (int n = 0; n < 4; n++) {
        if (!((v >> n) & 1)) {
          continue;
        }
        const double theta = PI / 2.0 * n + BURST_PHASE;
        y += 0.25;
        i += 0.5 * std::cos(theta);
        q += 0.5 * std::sin(theta);
      }
      palette[static_cast<size_t>(v)] = yiqToRGB(y, i, q);
      palY[static_cast<size_t>(v)] = std::pow(clamp01(y), LUMA_GAMMA);
      palI[static_cast<size_t>(v)] = i * CHROMA_GAIN;
      palQ[static_cast<size_t>(v)] = q * CHROMA_GAIN;
    }
  }
};

const Tables &tables() {
  static const Tables t;
  return t;
}

constexpr uint32_t WHITE = 0xFFFFFFFFu;
constexpr uint32_t BLACK = 0xFF000000u;

} // namespace

// ============================================================================
// Decoders
// ============================================================================

void decodeComposite(const uint8_t *dots, bool burst, uint32_t *out) {
  const Tables &t = tables();

  // Prime the sliding window with the CENTER dots of left margin plus the first
  // CENTER + 1 visible dots, so bit (WINDOW-1-j) holds dot (x - CENTER + j).
  uint32_t w = 0;
  for (int j = 0; j < WINDOW; j++) {
    w = (w << 1) | (dots[j] ? 1u : 0u);
  }

  if (burst) {
    const uint32_t *table = t.composite.data();
    for (int x = 0; x < VISIBLE_DOTS; x++) {
      out[x] = table[static_cast<size_t>(x & 3) * WINDOW_STATES + w];
      w = ((w << 1) | (dots[DOT_ORIGIN + x + CENTER + 1] ? 1u : 0u)) & WINDOW_MASK;
    }
  } else {
    // Burst inhibited: the chroma demodulator has no reference to lock to, so
    // the picture is whatever the luma path passes and nothing more.
    const uint8_t *table = t.luma.data();
    for (int x = 0; x < VISIBLE_DOTS; x++) {
      const uint32_t g = table[w];
      out[x] = 0xFF000000u | (g << 16) | (g << 8) | g;
      w = ((w << 1) | (dots[DOT_ORIGIN + x + CENTER + 1] ? 1u : 0u)) & WINDOW_MASK;
    }
  }
}

void decodeIdeal(const uint8_t *dots, const IdealKind *kind, bool chroma,
                 bool smooth, uint32_t *out) {
  const Tables &t = tables();
  const uint8_t *v = dots + DOT_ORIGIN;

  if (!chroma) {
    // The colour killer has engaged, so there is no colour to render however
    // the dots are grouped.
    for (int x = 0; x < VISIBLE_DOTS; x++) {
      out[x] = v[x] ? WHITE : BLACK;
    }
    return;
  }

  // Palette index per dot. Everything here is a lookup, never a filter, so no
  // value can leak sideways into a neighbouring dot.
  uint8_t idx[VISIBLE_DOTS];
  bool literal[VISIBLE_DOTS]; // resolved to plain black/white, not a palette hue

  for (int x = 0; x < VISIBLE_DOTS; x++) {
    switch (kind[x]) {
    case IdealKind::CELL: {
      // One flat colour across the aligned four-dot group. Each dot
      // contributes at its own subcarrier phase, so a steady LORES nibble
      // gives that nibble's colour in every column.
      const int g = x & ~3;
      int n = 0;
      for (int j = 0; j < 4; j++) {
        if (v[g + j]) n |= 1 << ((g + j) & 3);
      }
      literal[x] = false;
      idx[x] = static_cast<uint8_t>(n);
      break;
    }

    case IdealKind::DOT_GATED: {
      if (!v[x]) {
        // An unlit dot is black. This is the whole difference from a
        // demodulator, which would happily paint colour here.
        literal[x] = true;
        idx[x] = 0;
        break;
      }
      // Measure the run of lit dots this dot belongs to. A HIRES pixel — and a
      // 40-column text stroke — is two dots wide, so a run of two carries an
      // artifact colour, while three or more means the lit area is wide enough
      // to read as white. Using run length rather than byte position keeps this
      // correct across the high bit's half-dot shift, and means text picks up
      // the same NTSC colouring its dot pattern would produce on real hardware.
      int lo = x, hi = x;
      while (lo > 0 && v[lo - 1]) lo--;
      while (hi < VISIBLE_DOTS - 1 && v[hi + 1]) hi++;

      if (hi - lo + 1 >= 3) {
        literal[x] = true;
        idx[x] = 15;
      } else {
        int n = 0;
        for (int p = lo; p <= hi; p++) n |= 1 << (p & 3);
        literal[x] = false;
        idx[x] = static_cast<uint8_t>(n);
      }
      break;
    }
    }
  }

  if (!smooth) {
    for (int x = 0; x < VISIBLE_DOTS; x++) {
      out[x] = literal[x] ? (idx[x] ? WHITE : BLACK) : t.palette[idx[x]];
    }
    return;
  }

  // Analogue RGB output stage: chroma gets a gentle 1-2-1 smear, luma does not,
  // so colour softens slightly at edges while the picture stays sharp.
  for (int x = 0; x < VISIBLE_DOTS; x++) {
    const auto chromaAt = [&](int p) {
      p = p < 0 ? 0 : (p >= VISIBLE_DOTS ? VISIBLE_DOTS - 1 : p);
      // Literal black and white carry no chroma to spread.
      return literal[p] ? std::pair<double, double>{0.0, 0.0}
                        : std::pair<double, double>{t.palI[idx[p]],
                                                    t.palQ[idx[p]]};
    };
    const auto [li, lq] = chromaAt(x - 1);
    const auto [ci, cq] = chromaAt(x);
    const auto [ri, rq] = chromaAt(x + 1);
    const double i = (li + 2.0 * ci + ri) * 0.25;
    const double q = (lq + 2.0 * cq + rq) * 0.25;
    const double y = literal[x] ? (idx[x] ? 1.0 : 0.0) : t.palY[idx[x]];
    out[x] = packRGB(y + 0.956 * i + 0.621 * q, y - 0.272 * i - 0.647 * q,
                     y - 1.106 * i + 1.703 * q);
  }
}

void decodeMonochrome(const uint8_t *dots, uint32_t on, uint32_t off,
                      uint32_t *out) {
  const uint8_t *v = dots + DOT_ORIGIN;
  for (int x = 0; x < VISIBLE_DOTS; x++) {
    out[x] = v[x] ? on : off;
  }
}

const std::array<uint32_t, 16> &idealPalette() { return tables().palette; }

uint32_t demodulateReference(const uint8_t *window, int phase, bool burst) {
  const Tables &t = tables();
  double y = 0.0;
  double i = 0.0;
  double q = 0.0;

  for (int j = 0; j < WINDOW; j++) {
    if (!window[j]) {
      continue;
    }
    y += t.hy[static_cast<size_t>(j)];
    if (burst) {
      const double theta = PI / 2.0 * ((phase + j + 1) & 3) + BURST_PHASE;
      i += 2.0 * t.hc[static_cast<size_t>(j)] * std::cos(theta);
      q += 2.0 * t.hc[static_cast<size_t>(j)] * std::sin(theta);
    }
  }

  if (!burst) {
    const uint32_t g = static_cast<uint32_t>(
        std::lround(clamp01(std::pow(clamp01(y), LUMA_GAMMA)) * 255.0));
    return 0xFF000000u | (g << 16) | (g << 8) | g;
  }
  return yiqToRGB(y, i, q);
}

} // namespace ntsc
} // namespace a2e
