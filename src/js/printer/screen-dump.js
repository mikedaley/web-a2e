/*
 * screen-dump.js - Host-side screen → dot-matrix printer graphics dump
 *
 * Converts an RGBA framebuffer (the //e 560×384 HGR/DHGR/LORES/TEXT screen, or
 * any arbitrary bitmap) into a faithful bit-image stream for a SPECIFIC dot-matrix
 * printer family. The bytes go through that printer model's own parser — the exact
 * graphics protocol period software (Grappler ROM, Print Shop, …) drives — so the
 * dump exercises the real wire format, never painting pixels directly.
 *
 * --- Architecture: one scanner, explicit per-printer protocols -----------------
 *
 * The hard part — walking the framebuffer and packing 8 vertical pixels into a
 * column byte — is IDENTICAL for every printer (see `scanBandColumns`). What
 * differs is purely the WIRE FORMAT each printer's head speaks, and those
 * differences are the real hardware demarcations. Each is captured explicitly in a
 * frozen PROTOCOL descriptor (see CITOH_BASE / IMAGEWRITER / APPLE_DMP / EPSON_FX),
 * so adding a printer means adding a descriptor, not editing the scanner.
 *
 * The four demarcations that actually differ between these heads:
 *
 *   1. Graphics command   C. Itoh: ESC G          Epson: ESC * <mode>
 *   2. Column count form   C. Itoh: 4 ASCII digits Epson: 2 binary bytes (nL nH)
 *   3. Top-dot bit         C. Itoh: bit 0 (LSB)    Epson: bit 7 (MSB)
 *   4. Band line feed      C. Itoh: ESC T 16/144"  Epson: ESC 3 24/216"
 *
 * The Apple Dot Matrix Printer and the ImageWriter both descend from the C. Itoh
 * 8510 command set, so they COMPOSE from one CITOH_BASE descriptor — the DMP is
 * that base minus the ImageWriter II's four-band colour ribbon. The Epson FX-80 is
 * a different lineage (ESC/P) and so is its own descriptor.
 *
 * Public builders — one per printer head, named so the target is unmistakable:
 *   buildScreenDumpImageWriter()  72-dpi ESC G, mono or 4-band colour ribbon
 *   buildScreenDumpAppleDMP()     72-dpi ESC G, mono only (no colour ribbon)
 *   buildScreenDumpEpson()        72-dpi ESC * 5, mono (FX-80 has no colour ribbon)
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 *  Shawn Bullock <shawn@agenticexpert.ai>
 */

import { machineDisplay } from "../machine/machine-profile.js";

const ESC = 0x1B;

// The machine renders its screen to an RGBA framebuffer of this size (see
// main.js captureScreenshot). On a //e, 280x192 HGR doubled to 560x384.
//
// These are functions rather than constants because the size belongs to the
// machine, and the machine is not known until the core has been asked. Every
// default parameter below calls them, so each call gets the current answer
// rather than whatever was true when this module was first imported.
export const screenWidth = () => machineDisplay().width;
export const screenHeight = () => machineDisplay().height;

// ---- Wire-format primitives (the per-printer demarcations) --------------------

// C. Itoh column count: 4 ASCII digits, leading zeros (Table 8-1); 560 → "0560".
function pushCount4(out, n) {
  const s = String(Math.max(0, Math.min(9999, n | 0))).padStart(4, "0");
  for (let i = 0; i < 4; i++) out.push(s.charCodeAt(i));
}

// Epson column count: two binary bytes, low then high (n = nL + nH·256).
function pushCount2(out, n) {
  const v = Math.max(0, Math.min(0xFFFF, n | 0));
  out.push(v & 0xFF, (v >> 8) & 0xFF);
}

// Bit-reverse an 8-bit column. The scanner always builds columns with bit 0 = top
// (the natural reading order). C. Itoh heads want exactly that; Epson heads put the
// top dot in bit 7, so msb-top protocols reverse each column on the way out.
const REVERSE8 = (() => {
  const t = new Uint8Array(256);
  for (let b = 0; b < 256; b++) {
    let r = 0;
    for (let i = 0; i < 8; i++) if (b & (1 << i)) r |= 1 << (7 - i);
    t[b] = r;
  }
  return t;
})();

// A framebuffer pixel is "ink" when it is lit on the dark screen. Testing each
// channel against the threshold means coloured HGR pixels (green, purple, …)
// dump as black ink too, which is what a monochrome screen dump should do.
function isLit(fb, idx, threshold) {
  return fb[idx] >= threshold || fb[idx + 1] >= threshold || fb[idx + 2] >= threshold;
}

// ---- Shared scanner (printer-agnostic — the half every head shares) -----------
//
// Pack one 8-dot-high band of the framebuffer into `width` column bytes, bit r of
// each column = framebuffer row y+r, bit 0 = TOP. This is the only place the
// pixels are read; protocols below decide how the resulting bytes hit the wire.
function scanBandColumns(fb, width, height, y, bandH, threshold) {
  const cols = new Array(width);
  for (let x = 0; x < width; x++) {
    let col = 0;
    for (let r = 0; r < bandH; r++) {
      const yy = y + r;
      if (yy >= height) break;
      if (isLit(fb, (yy * width + x) * 4, threshold)) col |= 1 << r; // bit 0 = top
    }
    cols[x] = col;
  }
  return cols;
}

// ============================================================================
// PROTOCOL DESCRIPTORS — one frozen object per printer head. Every field is a
// hardware demarcation; nothing here reads pixels. `enterGraphics`/`beginRow`/
// `endRow`/`exitText` push raw protocol bytes, `msbTop` flips the bit order.
// ============================================================================

// --- C. Itoh 8510 base: shared by the Apple DMP and the ImageWriter family ---
// ESC n selects 72-dpi horizontal (the square 72×72 grid every IW Tech-Ref
// graphics example uses); ESC T 16 = 16/144" feed = exactly 8 wires at 72 dpi, so
// each 8-dot band butts seamlessly against the next. ESC G takes a 4-ASCII-digit
// column count; data byte bit 0 = top dot (Figure 8-1), all 8 bits significant.
const CITOH_BASE = {
  bandHeight: 8,
  msbTop: false,                                          // bit 0 = top wire
  enterGraphics(out) {
    out.push(ESC, 0x6E);                                  // ESC n  — 72 dpi
    out.push(ESC, 0x54, 0x31, 0x36);                      // ESC T 16 — 16/144" feed
  },
  beginRow(out, nCols) { out.push(ESC, 0x47); pushCount4(out, nCols); }, // ESC G nnnn
  endRow(out)  { out.push(0x0D, 0x0A); },                 // CR+LF — return + 1 band
  exitText(out) { out.push(ESC, 0x4E); out.push(ESC, 0x41); }, // ESC N pica, ESC A 6 lpi
};

// Apple ImageWriter / ImageWriter II — C. Itoh base, with the optional four-band
// colour ribbon (handled by buildImageWriterColor, not the mono path).
const IMAGEWRITER = { ...CITOH_BASE };

// Apple Dot Matrix Printer — same C. Itoh wire format, but a single black ribbon:
// no colour passes. Literally the base with no colour ribbon attached.
const APPLE_DMP = { ...CITOH_BASE };

// --- Epson FX-80: ESC/P lineage, a different head -----------------------------
// ESC * 5 selects 72-dpi single-density graphics (the FX mode whose dot pitch
// matches the IW square grid); the count is two binary bytes (nL nH). The vertical
// pin pitch is 1/72", so ESC 3 24 = 24/216" = exactly 8 pins advances one band.
// The Epson head puts the TOP pin in bit 7, so columns reverse on the way out.
const EPSON_FX = {
  bandHeight: 8,
  msbTop: true,                                           // bit 7 = top pin
  enterGraphics(out) { out.push(ESC, 0x33, 24); },        // ESC 3 24 — 24/216" feed
  beginRow(out, nCols) { out.push(ESC, 0x2A, 0x05); pushCount2(out, nCols); }, // ESC * 5 nL nH
  endRow(out)  { out.push(0x0D, 0x0A); },                 // CR+LF — return + 1 band
  exitText(out) { out.push(ESC, 0x32); },                 // ESC 2 — back to 1/6" (6 lpi)
};

// ---- Generic mono builder: shared scanner + a printer's protocol --------------
function buildMono(fb, width, height, proto, opts = {}) {
  const threshold = opts.threshold ?? 0x40;
  const invert    = opts.invert    ?? false;
  const nCols     = Math.min(width, Math.max(1, (opts.maxCols ?? width) | 0));
  const out = [];
  proto.enterGraphics(out);
  for (let y = 0; y < height; y += proto.bandHeight) {
    const cols = scanBandColumns(fb, width, height, y, proto.bandHeight, threshold);
    proto.beginRow(out, nCols);
    for (let x = 0; x < nCols; x++) {
      let col = cols[x] & 0xFF;
      if (invert) col = (~col) & 0xFF;
      out.push(proto.msbTop ? REVERSE8[col] : col);
    }
    proto.endRow(out);
  }
  proto.exitText(out);
  return out;
}

// ============================================================================
// PUBLIC BUILDERS — one per printer head.
// ============================================================================

/**
 * Apple ImageWriter / ImageWriter II screen dump (C. Itoh ESC G, 72 dpi).
 * Mono by default; `opts.color` routes to the four-band ribbon separation.
 *
 * @param {Uint8ClampedArray|Uint8Array|number[]} fb  RGBA pixels, row-major.
 * @param {number} width   pixels per row.
 * @param {number} height  rows.
 * @param {object} [opts]
 * @param {number}  [opts.threshold=0x40]  per-channel lit threshold (0–255).
 * @param {boolean} [opts.color]           true → colour ribbon separation (IW II).
 * @param {boolean} [opts.invert]          colour: force greyscale polarity.
 * @returns {number[]}  byte stream for PrinterManager.feedBytes().
 */
export function buildScreenDumpImageWriter(fb, width = screenWidth(), height = screenHeight(), opts = {}) {
  return opts.color
    ? buildScreenDumpColor(fb, width, height, opts)
    : buildMono(fb, width, height, IMAGEWRITER, opts);
}

/**
 * Apple Dot Matrix Printer screen dump (C. Itoh ESC G, 72 dpi, single black
 * ribbon — no colour passes). Same wire format as the ImageWriter mono dump.
 */
export function buildScreenDumpAppleDMP(fb, width = screenWidth(), height = screenHeight(), opts = {}) {
  return buildMono(fb, width, height, APPLE_DMP, opts);
}

/**
 * Epson FX-80 screen dump (ESC/P ESC * 5, 72 dpi, MSB-top columns). Mono — the
 * FX-80 has no colour ribbon.
 */
export function buildScreenDumpEpson(fb, width = screenWidth(), height = screenHeight(), opts = {}) {
  return buildMono(fb, width, height, EPSON_FX, opts);
}

// Back-compat aliases — the original ImageWriter-only entry points.
export function buildScreenDump(fb, width = screenWidth(), height = screenHeight(), opts = {}) {
  return buildMono(fb, width, height, IMAGEWRITER, opts);
}

// ---- Period-correct ImageWriter II colour model ------------------------------
//
// The colour ribbon has four physical bands — yellow, magenta, cyan, black.
// Every other printable colour is made the way the real printer makes it: by
// OVERPRINTING two band passes on the same dot (orange = Y+M, green = Y+C,
// purple = M+C). So a colour dump is a band SEPARATION — a yellow pass, a
// magenta pass, a cyan pass and a black pass per 8-dot band — not a palette of
// pre-mixed inks. Where two passes strike the same dot the inks physically
// overlay, exactly as the renderer's band accumulation reproduces.
//
// Internal band bits (independent of the ESC K codes below).
const bY = 1, bM = 2, bC = 4, bK = 8;

// ESC K colour-select codes for each primary band pass (Table A-18).
const BAND_PASSES = [
  { bit: bY, esc: 1 }, // yellow
  { bit: bM, esc: 2 }, // magenta
  { bit: bC, esc: 3 }, // cyan
  { bit: bK, esc: 0 }, // black
];

// Quantisation anchors = the //e's actual 16-colour palette (video.cpp:445,
// 0xAARRGGBB), each tagged with the ribbon band(s) that overlay to print it.
// Anchoring on the *whole* palette — not just the six saturated hues — is what
// keeps solid regions solid: the //e's dark and pastel colours (dark blue,
// olive, light blue …) each land on their OWN anchor instead of being scattered
// by the Bayer jog between three far-apart saturated points (the crosshatch
// artefact). The ribbon can't make blue or any dark colour, so those map to the
// nearest achievable ink: every "blue" → M+C (purple), the only blue it has.
// The greyscale poles (black idx0/10-grey, white idx15) are added by gamut()
// below — grey is intentionally omitted so it falls to a black/white dither.
const COLOUR_POINTS = [
  { rgb: [227, 30,  96 ], bands: bM         }, // red / magenta → magenta
  { rgb: [96,  114, 3  ], bands: bY | bC    }, // dark olive    → Y + C (green)
  { rgb: [255, 106, 60 ], bands: bY | bM    }, // orange        → Y + M
  { rgb: [0,   163, 96 ], bands: bY | bC    }, // dark green    → Y + C
  { rgb: [20,  245, 60 ], bands: bY | bC    }, // green         → Y + C
  { rgb: [208, 221, 141], bands: bY         }, // yellow        → yellow
  { rgb: [96,  78,  189], bands: bM | bC    }, // dark blue     → M + C (purple)
  { rgb: [255, 68,  253], bands: bM | bC    }, // violet/purple → M + C
  { rgb: [255, 160, 208], bands: bM         }, // pink          → magenta
  { rgb: [20,  207, 253], bands: bC         }, // medium blue   → cyan
  { rgb: [208, 195, 255], bands: bC         }, // light blue    → cyan
  { rgb: [114, 255, 208], bands: bC         }, // aqua          → cyan
];

// Build the 8-entry quantisation gamut. Saturated colours always print as their
// band pair; only the greyscale poles flip with the mode:
//  - WYSIWYG (invert=false): screen BLACK → black band, screen WHITE → bare
//    paper. Reproduces a dense colour screen as seen.
//  - INVERTED (invert=true): screen BLACK → bare paper, screen WHITE → black
//    band. The traditional dump for sparse/light screens (text, line art) so a
//    mostly-black field stays white paper instead of a near-solid black page.
function gamut(invert) {
  return [
    { rgb: [0,   0,   0  ], bands: invert ? 0  : bK },  // black field
    { rgb: [255, 255, 255], bands: invert ? bK : 0  },  // white
    ...COLOUR_POINTS,
  ];
}

// Fraction of pixels that carry visible content (any channel above LIT_THRESH).
// Drives the auto WYSIWYG-vs-inverted choice: a dense colour screen reproduces
// as seen, a sparse one (mostly black field) inverts so paper stays white.
const LIT_THRESH = 48;
export function litDensity(fb, width = screenWidth(), height = screenHeight()) {
  const n = width * height;
  let lit = 0;
  for (let i = 0; i < n; i++) {
    const o = i * 4;
    if (fb[o] >= LIT_THRESH || fb[o + 1] >= LIT_THRESH || fb[o + 2] >= LIT_THRESH) lit++;
  }
  return n ? lit / n : 0;
}

// 4×4 Bayer matrix (normalised −0.5..+0.5) for ordered/pattern dithering — the
// period-authentic technique on these 8-bit machines (Print Shop, Dazzle Draw,
// 8/16 Paint all used ordered patterns, not the modern error-diffusion FS).
const BAYER4 = [
  [0,  8,  2,  10],
  [12, 4,  14, 6 ],
  [3,  11, 1,  9 ],
  [15, 7,  13, 5 ],
];
const DITHER_AMP = 36; // luminance jog before quantising. Low: the full-palette
                       // anchors already resolve solid colours exactly, so the
                       // jog only needs to break up off-palette/fringe pixels
                       // and dither greys toward the black/white poles.

// Ordered-dither the framebuffer onto GAMUT. Returns a Uint8Array band mask per
// pixel (bY|bM|bC|bK bits); 0 = bare paper. Greys/pastels fall between gamut
// points, so the Bayer jog scatters them into period checkerboard patterns.
function ditherToBands(fb, width, height, gam) {
  const n   = width * height;
  const map = new Uint8Array(n);
  for (let y = 0; y < height; y++) {
    const brow = BAYER4[y & 3];
    for (let x = 0; x < width; x++) {
      const p = y * width + x;
      const jog = (brow[x & 3] / 16 - 0.5) * DITHER_AMP * 2;
      const r = fb[p * 4] + jog, g = fb[p * 4 + 1] + jog, b = fb[p * 4 + 2] + jog;
      let best = 0, bestD = Infinity;
      for (let t = 0; t < gam.length; t++) {
        const c = gam[t].rgb;
        const dr = r - c[0], dg = g - c[1], db = b - c[2];
        const d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = t; }
      }
      map[p] = gam[best].bands;
    }
  }
  return map;
}

/**
 * Build a period-correct COLOUR ImageWriter II screen dump: a four-band ribbon
 * separation, ordered-dithered, with overlapping Y/M/C/K passes per 8-dot band
 * so secondaries form by physical ink overlay. Requires Auto-LF OFF for the
 * duration so the per-pass CR returns the head without feeding (the caller
 * arranges this); the per-band LF advances exactly one band.
 *
 * @param {Uint8ClampedArray|Uint8Array|number[]} fb  RGBA pixels, row-major.
 * @param {number} width   pixels per row.
 * @param {number} height  rows.
 * @param {object} [opts]
 * @param {boolean} [opts.invert]  Force greyscale polarity. Omit to auto-pick:
 *   sparse screens (< 5% lit) invert (paper stays white), dense screens are
 *   reproduced WYSIWYG.
 * @returns {number[]}  byte stream for PrinterManager.feedBytes().
 */
export function buildScreenDumpColor(fb, width = screenWidth(), height = screenHeight(), opts = {}) {
  const invert = opts.invert ?? (litDensity(fb, width, height) < 0.05);
  const nCols  = Math.min(width, Math.max(1, (opts.maxCols ?? width) | 0));
  const map = ditherToBands(fb, width, height, gamut(invert));
  const out = [];

  out.push(ESC, 0x6E);              // ESC n  — 72 dpi horizontal (square grid)
  out.push(ESC, 0x54, 0x31, 0x36); // ESC T 16 — 16/144" feed, bands butt together

  for (let y = 0; y < height; y += 8) {
    for (const pass of BAND_PASSES) {
      // Only emit this band's pass if some dot in the band uses it.
      let used = false;
      for (let x = 0; x < nCols && !used; x++) {
        for (let r = 0; r < 8; r++) {
          const yy = y + r; if (yy >= height) break;
          if (map[yy * width + x] & pass.bit) { used = true; break; }
        }
      }
      if (!used) continue;

      out.push(ESC, 0x4B, 0x30 + pass.esc); // ESC K — select this band's colour
      out.push(ESC, 0x47);                  // ESC G
      pushCount4(out, nCols);
      for (let x = 0; x < nCols; x++) {
        let col = 0;
        for (let r = 0; r < 8; r++) {
          const yy = y + r; if (yy >= height) break;
          if (map[yy * width + x] & pass.bit) col |= 1 << r; // bit 0 = top
        }
        out.push(col);
      }
      out.push(0x0D);                       // CR — return so the next band overlays
    }
    out.push(0x0A);                         // LF — advance exactly one 8-dot band
  }

  out.push(ESC, 0x4B, 0x30);        // back to black
  out.push(ESC, 0x4E);              // ESC N — back to pica
  out.push(ESC, 0x41);              // ESC A — back to 6 lpi
  return out;
}
