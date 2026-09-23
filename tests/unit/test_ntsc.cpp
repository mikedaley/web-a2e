/*
 * test_ntsc.cpp - Unit tests for composite video decoding
 *
 * These tests are about signal processing rather than pixels. The properties
 * they pin down are the ones that make the decode correct rather than merely
 * plausible: that the lookup table is an exact memoisation of the filter, that
 * the luma path rejects the subcarrier so greys stay neutral, that colour burst
 * gating really does produce colourless text, and that every mode agrees on
 * hue because they all share one phase reference.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "video/ntsc.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

using namespace a2e;
using namespace a2e::ntsc;

namespace {

struct Line {
    uint8_t dots[DOT_BUFFER];
    uint32_t out[VISIBLE_DOTS];

    Line() { std::memset(dots, 0, sizeof(dots)); }

    // Fill the whole line from a function of absolute visible dot position.
    template <typename F> void fill(F f) {
        for (int i = 0; i < DOT_BUFFER; i++) {
            dots[i] = f(i - DOT_ORIGIN) ? 1 : 0;
        }
    }

    void set(int x, uint8_t v) { dots[DOT_ORIGIN + x] = v; }

    IdealKind kind[VISIBLE_DOTS];
    void tag(IdealKind k) {
        for (int x = 0; x < VISIBLE_DOTS; x++) kind[x] = k;
    }
};

inline int R(uint32_t c) { return (c >> 16) & 0xFF; }
inline int G(uint32_t c) { return (c >> 8) & 0xFF; }
inline int B(uint32_t c) { return c & 0xFF; }

inline bool neutral(uint32_t c) { return R(c) == G(c) && G(c) == B(c); }

// A steady LORES nibble: dot x carries nibble bit (x mod 4).
inline auto loresPattern(int nibble) {
    return [nibble](int n) {
        const int m = ((n % 4) + 4) % 4;
        return (nibble >> m) & 1;
    };
}

// Largest per-channel difference between two colours.
inline int maxDelta(uint32_t a, uint32_t b) {
    return std::max({std::abs(R(a) - R(b)), std::abs(G(a) - G(b)),
                     std::abs(B(a) - B(b))});
}

} // namespace

// ============================================================================
// The lookup table is a memoisation, not an approximation
// ============================================================================

TEST_CASE("Composite lookup table equals the reference FIR exactly",
          "[ntsc][lut]") {
    // Every reachable state of the demodulator: all 2^15 window patterns at all
    // four subcarrier phases. If this passes, the table is the filter — there is
    // no accuracy being traded for the speed of a single lookup per dot.
    long mismatches = 0;

    for (uint32_t w = 0; w < static_cast<uint32_t>(WINDOW_STATES); w++) {
        uint8_t window[WINDOW];
        for (int j = 0; j < WINDOW; j++) {
            window[j] = (w >> (WINDOW - 1 - j)) & 1;
        }

        for (int phase = 0; phase < 4; phase++) {
            // Place the window so its centre lands on a dot with x % 4 == phase.
            const int x = 280 + ((phase - 280) & 3);

            Line line;
            for (int j = 0; j < WINDOW; j++) {
                line.set(x - CENTER + j, window[j]);
            }
            decodeComposite(line.dots, true, line.out);

            if (line.out[x] != demodulateReference(window, phase, true)) {
                mismatches++;
            }
        }
    }

    REQUIRE(mismatches == 0);
}

// ============================================================================
// Luma path: the subcarrier must be rejected exactly
// ============================================================================

TEST_CASE("Solid white and solid black survive the filter intact",
          "[ntsc][luma]") {
    Line line;

    line.fill([](int) { return 1; });
    decodeComposite(line.dots, true, line.out);
    REQUIRE(line.out[280] == 0xFFFFFFFFu);

    line.fill([](int) { return 0; });
    decodeComposite(line.dots, true, line.out);
    REQUIRE(line.out[280] == 0xFF000000u);
}

TEST_CASE("Greys decode neutral, proving the subcarrier notch works",
          "[ntsc][luma]") {
    // A LORES grey is an alternating 0101 pattern — which is to say, a pure
    // 3.58 MHz tone. Any subcarrier leaking into the luma path would show up
    // here as a colour cast, and any chroma left unbalanced as a tint.
    for (const int nibble : {5, 10}) {
        Line line;
        line.fill(loresPattern(nibble));
        decodeComposite(line.dots, true, line.out);

        for (int x = 100; x < 460; x++) {
            REQUIRE(neutral(line.out[x]));
        }
    }
}

// ============================================================================
// Colour burst gating
// ============================================================================

TEST_CASE("Burst inhibited means a colourless picture", "[ntsc][burst]") {
    // This is the mechanism behind crisp //e text. Drive the worst case for
    // artifact colour — alternating dot pairs, which with burst on is a
    // saturated hue — and require every pixel to come out grey.
    Line line;
    line.fill([](int n) { return (n / 2) & 1; });

    decodeComposite(line.dots, false, line.out);
    for (int x = 0; x < VISIBLE_DOTS; x++) {
        REQUIRE(neutral(line.out[x]));
    }

    // With burst present the same signal is emphatically not grey.
    decodeComposite(line.dots, true, line.out);
    bool anyColour = false;
    for (int x = 100; x < 460; x++) {
        if (!neutral(line.out[x])) anyColour = true;
    }
    REQUIRE(anyColour);
}

TEST_CASE("Digital decoders pass the dot stream through when burst is off",
          "[ntsc][burst]") {
    // No burst, no colour to look up — and nothing to soften the picture with
    // either, so text stays pin sharp at full contrast.
    Line line;
    line.fill([](int n) { return (n / 2) & 1; });

    line.tag(IdealKind::DOT_GATED);
    for (const bool smooth : {false, true}) {
        decodeIdeal(line.dots, line.kind, false, smooth, line.out);
        for (int x = 0; x < VISIBLE_DOTS; x++) {
            const uint32_t want = line.dots[DOT_ORIGIN + x] ? 0xFFFFFFFFu : 0xFF000000u;
            REQUIRE(line.out[x] == want);
        }
    }
}

// ============================================================================
// Colour reproduction
// ============================================================================

TEST_CASE("Steady LORES patterns decode to the ideal palette", "[ntsc][color]") {
    // The composite decoder runs a 15-tap filter; the ideal palette is a single
    // clean subcarrier cycle. On a steady pattern they must agree, because the
    // filters have unity gain at DC and an exact null at the subcarrier.
    const auto &palette = idealPalette();

    for (int nibble = 0; nibble < 16; nibble++) {
        Line line;
        line.fill(loresPattern(nibble));
        decodeComposite(line.dots, true, line.out);

        INFO("LORES nibble " << nibble);
        REQUIRE(maxDelta(line.out[280], palette[nibble]) <= 2);
    }
}

TEST_CASE("HIRES artifact colours are the LORES colours they must be",
          "[ntsc][color]") {
    // Artifact colour is not a separate phenomenon with its own palette. A HGR
    // pixel pair is the same 4-dot pattern a LORES nibble makes, so violet must
    // come out as LORES purple, green as light green, and the high-bit pair as
    // medium blue and orange. The palette this replaced had these inconsistent.
    const auto &palette = idealPalette();

    struct Case {
        const char *name;
        bool evenPixels;
        int delay;
        int loresIndex;
    };
    const Case cases[] = {
        {"violet", true, 0, 3},   // LORES 3  = Purple
        {"green", false, 0, 12},  // LORES 12 = Light Green
        {"blue", true, 1, 6},     // LORES 6  = Medium Blue
        {"orange", false, 1, 9},  // LORES 9  = Orange
    };

    for (const Case &c : cases) {
        Line line;
        line.fill([&c](int n) {
            const int d = n - c.delay;
            const int m = ((d % 4) + 4) % 4;
            return ((m / 2) == 0) == c.evenPixels;
        });
        decodeComposite(line.dots, true, line.out);

        INFO("HIRES " << c.name);
        REQUIRE(maxDelta(line.out[280], palette[c.loresIndex]) <= 3);
    }
}

TEST_CASE("Pixel exact emits palette colours only", "[ntsc][pixel-exact]") {
    // The sharp decoder must not invent intermediate values: every pixel it
    // produces has to be one of the sixteen, or the mode is not exact.
    const auto &palette = idealPalette();

    Line line;
    line.fill([](int n) { return (n / 3) & 1; }); // deliberately off-grid
    line.tag(IdealKind::CELL);
    decodeIdeal(line.dots, line.kind, true, false, line.out);

    for (int x = 0; x < VISIBLE_DOTS; x++) {
        bool found = false;
        for (int i = 0; i < 16; i++) {
            if (line.out[x] == palette[i]) found = true;
        }
        INFO("pixel " << x);
        REQUIRE(found);
    }
}

TEST_CASE("Pixel exact never colours an unlit dot", "[ntsc][pixel-exact]") {
    // The defining property of a mode with no composite effects. A demodulator
    // spreads colour either side of an edge because its window straddles it;
    // this decoder must not, so every dot that is off has to be exactly black
    // and colour must stop dead at the last lit dot.
    Line line;
    line.fill([](int n) { return n >= 200 && n < 300; });
    line.tag(IdealKind::DOT_GATED);
    decodeIdeal(line.dots, line.kind, true, false, line.out);

    for (int x = 0; x < VISIBLE_DOTS; x++) {
        if (!line.dots[DOT_ORIGIN + x]) {
            INFO("unlit dot " << x << " must be black");
            REQUIRE(line.out[x] == 0xFF000000u);
        }
    }
    // The lit block is a long run, so it is white edge to edge.
    REQUIRE(line.out[200] == 0xFFFFFFFFu);
    REQUIRE(line.out[299] == 0xFFFFFFFFu);
}

TEST_CASE("Pixel exact gives an isolated HIRES pixel its artifact colour",
          "[ntsc][pixel-exact]") {
    // A lone HIRES pixel is two dots. It should be a clean, flat artifact
    // colour over exactly those two dots and nothing either side of them.
    const auto &palette = idealPalette();

    for (int start = 200; start < 204; start++) {
        Line line;
        line.fill([](int) { return 0; });
        line.set(start, 1);
        line.set(start + 1, 1);
        line.tag(IdealKind::DOT_GATED);
        decodeIdeal(line.dots, line.kind, true, false, line.out);

        const int expect = (1 << (start & 3)) | (1 << ((start + 1) & 3));
        INFO("pixel starting at dot " << start);
        REQUIRE(line.out[start] == palette[expect]);
        REQUIRE(line.out[start + 1] == palette[expect]);
        REQUIRE(line.out[start - 1] == 0xFF000000u);
        REQUIRE(line.out[start + 2] == 0xFF000000u);
    }
}

TEST_CASE("Pixel exact makes adjacent HIRES pixels white", "[ntsc][pixel-exact]") {
    // Two lit pixels side by side are four dots, which on hardware reads as
    // white rather than as two colours.
    Line line;
    line.fill([](int) { return 0; });
    for (int x = 200; x < 204; x++) line.set(x, 1);
    line.tag(IdealKind::DOT_GATED);
    decodeIdeal(line.dots, line.kind, true, false, line.out);

    for (int x = 200; x < 204; x++) REQUIRE(line.out[x] == 0xFFFFFFFFu);
    REQUIRE(line.out[199] == 0xFF000000u);
    REQUIRE(line.out[204] == 0xFF000000u);
}

TEST_CASE("Pixel exact keeps hard edges where composite softens them",
          "[ntsc][pixel-exact]") {
    // A white block on black. The sharp decoder should step straight from black
    // to white; the composite decoder should ring and ramp across several dots.
    Line line;
    line.fill([](int n) { return n >= 200 && n < 300; });

    line.tag(IdealKind::DOT_GATED);
    decodeIdeal(line.dots, line.kind, true, false, line.out);
    REQUIRE(line.out[195] == 0xFF000000u);
    REQUIRE(line.out[250] == 0xFFFFFFFFu);
    // No ramp at all: the dot before the block is black, the first lit dot is
    // already full white.
    REQUIRE(line.out[199] == 0xFF000000u);
    REQUIRE(line.out[200] == 0xFFFFFFFFu);

    decodeComposite(line.dots, true, line.out);
    REQUIRE(line.out[250] == 0xFFFFFFFFu);
    // The filter has not settled anywhere near as quickly.
    REQUIRE(line.out[201] != 0xFFFFFFFFu);
}

// ============================================================================
// Monochrome
// ============================================================================

TEST_CASE("Monochrome is the dot stream and nothing else", "[ntsc][mono]") {
    Line line;
    line.fill([](int n) { return (n / 2) & 1; });

    const uint32_t on = 0xFF33FF33u;
    const uint32_t off = 0xFF000000u;
    decodeMonochrome(line.dots, on, off, line.out);

    for (int x = 0; x < VISIBLE_DOTS; x++) {
        REQUIRE(line.out[x] == (line.dots[DOT_ORIGIN + x] ? on : off));
    }
}

// ============================================================================
// Palette sanity
// ============================================================================

TEST_CASE("Ideal palette anchors match the Apple II colours", "[ntsc][color]") {
    const auto &p = idealPalette();

    REQUIRE(p[0] == 0xFF000000u);  // black
    REQUIRE(p[15] == 0xFFFFFFFFu); // white

    // Both grey codes are the same neutral grey.
    REQUIRE(p[5] == p[10]);
    REQUIRE(neutral(p[5]));

    // Complementary pairs sit opposite each other, so their chroma cancels to
    // the same luma. 3/12 are purple/green, 6/9 medium blue/orange.
    for (const auto pair : {std::make_pair(3, 12), std::make_pair(6, 9)}) {
        const int sumR = R(p[pair.first]) + R(p[pair.second]);
        const int sumG = G(p[pair.first]) + G(p[pair.second]);
        const int sumB = B(p[pair.first]) + B(p[pair.second]);
        // Within quantisation and gamut clipping of each other.
        REQUIRE(std::abs(sumR - sumG) < 90);
        REQUIRE(std::abs(sumG - sumB) < 90);
    }
}

// ============================================================================
// Solid: no decoder at all
// ============================================================================

TEST_CASE("Solid paints a cell its own colour over every one of its dots",
          "[ntsc][solid]") {
    // A seven-dot cell is not a whole number of four-dot groups, so every
    // decoder — the sharp ones included — turns a lone cell into its colour in
    // the middle and something else at the edges. Solid is the mode that does
    // not decode: the emitter said which palette entry the cell carries, and
    // that entry goes on all seven dots, whatever the dots themselves are.
    const auto &palette = idealPalette();

    for (int colour = 1; colour < 15; colour++) {
        Line line;
        // A single cell at dots 7..13 carrying colour `colour`, on black. The
        // dots are the double lo-res pattern for it; they do not matter here
        // and that is the point.
        const int nibble = ((colour >> 1) | ((colour & 1) << 3)) & 15;
        line.fill([nibble](int n) {
            if (n < 7 || n > 13) return 0;
            return (nibble >> (((n - 1) % 4 + 4) % 4)) & 1;
        });
        line.tag(IdealKind::DOT_GATED);
        uint8_t cell[VISIBLE_DOTS] = {};
        for (int x = 7; x < 14; x++) {
            line.kind[x] = IdealKind::CELL;
            cell[x] = static_cast<uint8_t>(colour);
        }
        decodeSolid(line.dots, line.kind, cell, line.out);

        INFO("colour " << colour);
        for (int x = 7; x < 14; x++) {
            INFO("dot " << x);
            REQUIRE(line.out[x] == palette[colour]);
        }
        // ...and it stops exactly at the cell's edge: the neighbours are black.
        REQUIRE(line.out[6] == 0xFF000000u);
        REQUIRE(line.out[14] == 0xFF000000u);
    }
}

TEST_CASE("Solid treats dot-gated dots exactly as pixel exact does",
          "[ntsc][solid]") {
    // HIRES and text are drawn, not encoded, so there is no cell value to
    // paint. Those dots go the sharp decoder's way, and the two modes must
    // agree on them to the pixel or a HIRES picture would change when the
    // preset does.
    Line a, b;
    a.fill([](int n) { return ((n / 5) % 3) == 0; });
    b.fill([](int n) { return ((n / 5) % 3) == 0; });
    a.tag(IdealKind::DOT_GATED);
    b.tag(IdealKind::DOT_GATED);
    uint8_t cell[VISIBLE_DOTS] = {};
    decodeIdeal(a.dots, a.kind, true, false, a.out);
    decodeSolid(b.dots, b.kind, cell, b.out);
    for (int x = 0; x < VISIBLE_DOTS; x++) {
        INFO("dot " << x);
        REQUIRE(a.out[x] == b.out[x]);
    }
}
