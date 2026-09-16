# Video Rendering

This page describes the video rendering subsystem in detail, covering all six Apple IIe display modes, the per-scanline progressive rendering pipeline, NTSC artifact color generation, character ROM handling, screen address calculation, and the change-log based mid-frame mode-switching system.

---

## Table of Contents

- [Overview](#overview)
- [Framebuffer Layout](#framebuffer-layout)
- [Video Modes](#video-modes)
  - [Mode Selection Logic](#mode-selection-logic)
  - [Text 40-Column](#text-40-column)
  - [Text 80-Column](#text-80-column)
  - [Lo-Res Graphics](#lo-res-graphics)
  - [Hi-Res Graphics](#hi-res-graphics)
  - [Double Lo-Res Graphics](#double-lo-res-graphics)
  - [Double Hi-Res Graphics](#double-hi-res-graphics)
- [Mixed Mode](#mixed-mode)
- [Color Palettes](#color-palettes)
  - [Lo-Res Palette](#lo-res-palette)
  - [Hi-Res Artifact Colors](#hi-res-artifact-colors)
  - [Double Hi-Res Palette](#double-hi-res-palette)
  - [Monochrome Rendering](#monochrome-rendering)
- [Character ROM](#character-rom)
  - [Inverse and Flash](#inverse-and-flash)
  - [Primary Character Set Mapping](#primary-character-set-mapping)
  - [Alternate Character Set](#alternate-character-set)
  - [UK Character Set](#uk-character-set)
- [Screen Address Calculation](#screen-address-calculation)
  - [Text and Lo-Res Addresses](#text-and-lo-res-addresses)
  - [Hi-Res Addresses](#hi-res-addresses)
  - [Row Offset Table](#row-offset-table)
- [Per-Scanline Progressive Rendering](#per-scanline-progressive-rendering)
  - [Horizontal Timing](#horizontal-timing)
  - [Switch Change Log](#switch-change-log)
  - [Video Pipeline Delay](#video-pipeline-delay)
  - [Progressive Rendering Loop](#progressive-rendering-loop)
  - [Scanline-With-Changes Rendering](#scanline-with-changes-rendering)
  - [Scanline Segment Dispatch](#scanline-segment-dispatch)
- [Frame Lifecycle](#frame-lifecycle)
  - [Frame Boundaries](#frame-boundaries)
  - [Flash Counter](#flash-counter)
  - [Force Render](#force-render)
- [Video Soft Switches](#video-soft-switches)
- [Display Pipeline](#display-pipeline)
- [WebGL Shader Pipeline](#webgl-shader-pipeline)
- [Display Settings Integration](#display-settings-integration)
- [See Also](#see-also)

---

## Overview

The `Video` class (`src/core/video/video.cpp`) renders the Apple IIe display into a 560x384 RGBA framebuffer. Rendering is progressive: as the CPU executes instructions, completed scanlines are rendered on-the-fly by `renderUpToCycle()`. At the end of each frame, `renderFrame()` finishes any remaining scanlines.

The renderer supports all six Apple IIe video modes:

| Mode | Resolution | Colors | Memory Used |
|------|-----------|--------|-------------|
| TEXT_40 | 40x24 characters | 2 (FG/BG) | $0400-$07FF main |
| TEXT_80 | 80x24 characters | 2 (FG/BG) | $0400-$07FF main + aux |
| LORES | 40x48 color blocks | 16 | $0400-$07FF main |
| HIRES | 280x192 pixels | 6 (artifact) | $2000-$3FFF main |
| DOUBLE_LORES | 80x48 color blocks | 16 | $0400-$07FF main + aux |
| DOUBLE_HIRES | 560x192 pixels | 16 | $2000-$3FFF main + aux |

All rendering is performed in C++ and exposed to JavaScript through the WASM framebuffer pointer. The JavaScript layer then uploads the framebuffer as a WebGL texture and applies CRT shader effects.

---

## Framebuffer Layout

The framebuffer is a flat RGBA byte array stored in `std::array<uint8_t, FRAMEBUFFER_SIZE>`:

| Constant | Value | Description |
|----------|-------|-------------|
| `SCREEN_WIDTH` | 560 | Pixels per row (280 x 2) |
| `SCREEN_HEIGHT` | 384 | Pixel rows (192 x 2) |
| `FRAMEBUFFER_SIZE` | 860,160 | Total bytes (560 x 384 x 4) |

Each Apple II pixel is doubled in both dimensions. A single Apple II dot at position (x, y) maps to a 2x2 block in the framebuffer. The `setPixel()` helper writes individual framebuffer pixels:

```
offset = (y * SCREEN_WIDTH + x) * 4
framebuffer[offset + 0] = R   (bits 16-23 of color)
framebuffer[offset + 1] = G   (bits 8-15 of color)
framebuffer[offset + 2] = B   (bits 0-7 of color)
framebuffer[offset + 3] = A   (bits 24-31 of color)
```

Colors are stored internally as packed 32-bit `0xAARRGGBB` values and unpacked into RGBA component order at write time.

---

## Video Modes

### Mode Selection Logic

The current video mode is determined by `getCurrentMode()` examining soft switch state:

```
if TEXT        -> col80 ? TEXT_80 : TEXT_40
if HIRES       -> (col80 && !AN3) ? DOUBLE_HIRES : HIRES
else (LORES)   -> (col80 && !AN3) ? DOUBLE_LORES : LORES
```

The key switches involved:

| Switch | Address | Effect |
|--------|---------|--------|
| TEXT | $C050/$C051 | Graphics / Text mode |
| MIXED | $C052/$C053 | Full screen / Mixed (4 text lines at bottom) |
| PAGE2 | $C054/$C055 | Display page 1 / page 2 |
| HIRES | $C056/$C057 | Lo-Res / Hi-Res |
| 80COL | $C00C/$C00D | 40-column / 80-column |
| AN3 | $C05E/$C05F | Annunciator 3 (enables double-width modes when OFF) |
| 80STORE | $C000/$C001 | Redirects PAGE2 to aux memory bank switching |

Double-resolution modes (DOUBLE_LORES, DOUBLE_HIRES) require both `col80` on **and** `an3` off.

### Text 40-Column

Renders 40 columns by 24 rows of characters from the 40-column text page. Each character is 7 ROM pixels wide, doubled to 14 framebuffer pixels. Characters are 8 scanlines tall, doubled to 16 framebuffer rows.

**Memory layout**: Text page 1 at `$0400-$07FF`, page 2 at `$0800-$0BFF`. Page selection respects the `80STORE` switch -- when `80STORE` is on, `PAGE2` is ignored for page selection (it is used for auxiliary memory bank switching instead).

**Rendering path** (`renderText40Scanline`):
1. Compute `textRow` (0-23) and `charLine` (0-7) from the scanline number (`scanline / 8` and `scanline % 8`)
2. For each column (0-39), read the character byte from the text page address
3. Determine inverse/flash status from the top two bits of the character byte
4. Look up the character glyph row in the character ROM via `getCharROMInfo()` and `renderCharacterLine()`
5. Render 7 dots, each doubled to 2 framebuffer pixels horizontally, and output on 2 framebuffer rows vertically

### Text 80-Column

Renders 80 columns by 24 rows using interleaved main and auxiliary memory. Each character is 7 pixels wide (no horizontal doubling), totaling 560 pixels across.

**Memory interleaving**: For each memory column position (0-39), the auxiliary byte provides the even (left) display column and the main byte provides the odd (right) display column. Both are read from the same text page address but from different memory banks.

**Rendering path** (`renderText80Scanline`):
1. For each memory column (0-39), read the aux byte and main byte at the same text page address
2. Aux character renders at display column `col * 2` (even position, 7 pixels)
3. Main character renders at display column `col * 2 + 1` (odd position, 7 pixels)
4. Each character is 7 framebuffer pixels wide -- no horizontal doubling is needed since 80 characters x 7 pixels = 560

### Lo-Res Graphics

Renders 40x48 color blocks using text page memory. Each byte encodes two vertically stacked 4-bit color indices.

**Color block structure**: Each byte in the text page represents two blocks stacked vertically within an 8-scanline text row. The low nibble (bits 0-3) is the top block color (scanlines 0-3 within the row) and the high nibble (bits 4-7) is the bottom block color (scanlines 4-7).

**Rendering path** (`renderLoResScanline`):
1. Compute `textRow` (0-23) and `lineInRow` (0-7) from the scanline
2. Select low nibble if `lineInRow < 4`, high nibble otherwise
3. Look up the 16-color palette via `getLoResColor()`
4. Fill a 14x2 framebuffer pixel block (7 Apple II dots doubled to 14 pixels wide, scanline doubled to 2 rows)

### Hi-Res Graphics

Renders 280x192 pixels with NTSC artifact color. Each byte encodes 7 dots plus a high bit that shifts the color phase.

**Memory layout**: Hi-Res page 1 at `$2000-$3FFF`, page 2 at `$4000-$5FFF`. The address mapping is interleaved -- see [Screen Address Calculation](#screen-address-calculation).

**Byte format**:

| Bit | Purpose |
|-----|---------|
| 0-6 | 7 pixel dots (bit 0 = leftmost) |
| 7 | High bit (shifts NTSC color phase by half a color clock) |

**Rendering path** (`renderHiResScanline`):
1. Build a 280-element `dots[]` array and a corresponding `highBits[]` array for the visible column range
2. For monochrome mode, each dot maps directly to on/off color in a 2x2 pixel block
3. For color mode, each dot's color is determined by NTSC artifact rules:

**NTSC artifact color rules** (evaluated left to right for each dot):

| Condition | Color |
|-----------|-------|
| Dot off, flanked by two single on-dots with matching high bits | Artifact color fill (fringing) |
| Dot off otherwise | Black |
| Dot on with adjacent on-dot (left or right) | White |
| Single dot on, even column, high bit 0 | Violet |
| Single dot on, odd column, high bit 0 | Green |
| Single dot on, even column, high bit 1 | Blue |
| Single dot on, odd column, high bit 1 | Orange |

The artifact fringing case handles the situation where a dot between two isolated on-dots takes on the neighboring dot's color, simulating how NTSC chroma bleeds across adjacent pixels on real hardware.

### Double Lo-Res Graphics

Renders 80x48 color blocks using interleaved main and auxiliary text page memory. Each memory column produces two display columns (aux on the left, main on the right), each 7 framebuffer pixels wide.

**Aux nibble rotation**: Auxiliary memory nibbles require a 1-bit left rotation within 4 bits to compensate for the half-color-clock phase shift between auxiliary and main video memory timing:

```
auxColor = ((auxNibble << 1) & 0x0F) | (auxNibble >> 3)
```

Without this rotation, auxiliary colors would display with incorrect NTSC phase, producing the wrong colors on screen.

**Rendering path** (`renderDoubleLoResScanline`):
1. Read aux and main bytes at the same text page address
2. Extract the appropriate nibble (low nibble if `lineInRow < 4`, high nibble otherwise)
3. Rotate the aux nibble by 1 bit left within 4 bits
4. Render aux color in the left 7 framebuffer pixels, main color in the right 7 pixels
5. Uses the `LORES_COLORS` palette for both halves

### Double Hi-Res Graphics

Renders 560x192 pixels using interleaved main and auxiliary Hi-Res memory. Each pair of bytes (aux + main) provides 14 dots that map to four 4-bit color values.

**Memory interleaving**: For each column address, the aux byte comes first (providing 7 dots), followed by the main byte (7 more dots), giving 14 dots per column pair. This produces 80 bytes per scanline (40 aux + 40 main).

**Byte layout**:

```
Byte 0 (aux col 0):  7 dots -> framebuffer dots 0-6
Byte 1 (main col 0): 7 dots -> framebuffer dots 7-13
Byte 2 (aux col 1):  7 dots -> framebuffer dots 14-20
Byte 3 (main col 1): 7 dots -> framebuffer dots 21-27
...
```

**Color encoding**: Every group of 4 consecutive dots forms a 4-bit color index into the 16-color Double Hi-Res palette. The bit ordering within each group is:

```
colorIndex = (dot[base] << 3) | (dot[base+1] << 2) | (dot[base+2] << 1) | dot[base+3]
```

The 4-dot groups are aligned to absolute dot positions (`(i / 4) * 4`), meaning all dots within a group share the same color. Since 560 dots / 4 dots-per-group = 140 color cells, Double Hi-Res provides an effective 140x192 pixel color display at 16 colors.

**Rendering path** (`renderDoubleHiResScanline`):
1. Read all 80 bytes for the scanline into a linear `line[]` array (interleaved aux/main)
2. Extract 560 individual dots from the 7 data bits of each byte
3. For each dot, compute its aligned 4-dot group base and derive the 4-bit color index
4. Look up the color in the `DHGR_COLORS` palette
5. Each dot maps to a single framebuffer pixel horizontally, doubled vertically to 2 rows

---

## Mixed Mode

When the MIXED switch is on (`$C053`), the bottom 4 text rows (scanlines 160-191) always render as text, regardless of the current graphics mode. The upper portion (scanlines 0-159) renders in the selected graphics mode.

The dispatch logic in `renderScanlineSegment()` checks:

```
if (mixed && scanline >= 160 && !text) {
    render as text (40-col or 80-col based on col80 switch)
    return
}
```

Mixed mode works with all four graphics modes: LORES, HIRES, DOUBLE_LORES, and DOUBLE_HIRES all show text in the bottom 32 scanlines when mixed mode is active.

---

## Color Palettes

### Lo-Res Palette

The 16-color Lo-Res palette (`LORES_COLORS` in `types.hpp`) provides NTSC-accurate colors:

| Index | Name | RGB |
|-------|------|-----|
| 0 | Black | `#000000` |
| 1 | Magenta | `#E31E60` |
| 2 | Dark Blue | `#604EBD` |
| 3 | Purple | `#FF44FD` |
| 4 | Dark Green | `#00A360` |
| 5 | Grey 1 | `#9C9C9C` |
| 6 | Medium Blue | `#14CFFD` |
| 7 | Light Blue | `#D0C3FF` |
| 8 | Brown | `#607203` |
| 9 | Orange | `#FF6A3C` |
| 10 | Grey 2 | `#9C9C9C` |
| 11 | Pink | `#FFA0D0` |
| 12 | Light Green | `#14F53C` |
| 13 | Yellow | `#D0DD8D` |
| 14 | Aqua | `#72FFD0` |
| 15 | White | `#FFFFFF` |

Indices 5 and 10 are both grey but appear at different positions in the NTSC chroma cycle. This palette is also used for Double Lo-Res mode.

### Hi-Res Artifact Colors

Hi-Res mode uses a 6-entry color table (`HIRES_COLORS` in `types.hpp`) that models NTSC artifact coloring:

| Index | Name | Usage |
|-------|------|-------|
| 0 | Black | Off dots |
| 1 | Green (`#14F53C`) | Odd pixels, high bit = 0 |
| 2 | Violet (`#FF44FD`) | Even pixels, high bit = 0 |
| 3 | White (`#FFFFFF`) | Adjacent on-dots |
| 4 | Blue (`#14CFFD`) | Even pixels, high bit = 1 |
| 5 | Orange (`#FF6A3C`) | Odd pixels, high bit = 1 |

The high bit (bit 7) of each byte selects between color group 1 (Violet/Green) and color group 2 (Blue/Orange). Two adjacent on-dots always produce white regardless of group.

### Double Hi-Res Palette

Double Hi-Res uses its own 16-color palette (`DHGR_COLORS`, defined locally in `renderDoubleHiResScanline()`). The ordering differs from Lo-Res due to the 14 MHz dot rate versus 7 MHz, which changes the NTSC phase relationship:

| Index | Color | Lo-Res Equivalent |
|-------|-------|-------------------|
| 0 | Black | LORES[0] |
| 1 | Magenta | LORES[1] |
| 2 | Brown | LORES[8] |
| 3 | Orange | LORES[9] |
| 4 | Dark Green | LORES[4] |
| 5 | Grey | LORES[5] |
| 6 | Light Green | LORES[12] |
| 7 | Yellow | LORES[13] |
| 8 | Dark Blue | LORES[2] |
| 9 | Purple | LORES[3] |
| 10 | Grey | LORES[10] |
| 11 | Light Blue | LORES[7] |
| 12 | Medium Blue | LORES[6] |
| 13 | Pink | LORES[11] |
| 14 | Aqua | LORES[14] |
| 15 | White | LORES[15] |

### Monochrome Rendering

All modes support monochrome rendering via `setMonochrome(true)`. When enabled, `getMonochromeColor()` returns:
- **On pixels**: White (`0xFFFFFFFF`) or green phosphor (`0xFF33FF33`) if `greenPhosphor_` is set
- **Off pixels**: Black (`0xFF000000`)

Lo-Res monochrome treats any non-zero color index as "on." Hi-Res and Double Hi-Res monochrome render individual dots as on/off.

---

## Character ROM

The character ROM is 8 KB total, containing two 4 KB character sets (US and UK). Each character is 8 bytes (one byte per scanline row), with each byte encoding 7 dots. Bit 0 is the leftmost pixel.

### Inverse and Flash

Character display attributes are determined by the top two bits of the character byte stored in text page memory:

| Bits 7-6 | Byte Range | Display |
|----------|------------|---------|
| `00` | $00-$3F | Inverse (foreground and background swapped) |
| `01` | $40-$7F | Flash (toggles between normal and inverse at ~3.75 Hz) |
| `10`-`11` | $80-$FF | Normal |

The `getCharROMInfo()` helper method decodes the character byte and returns a `CharROMInfo` struct containing the ROM offset, whether XOR bit-flipping is needed, and the final inverse state.

### Primary Character Set Mapping

With the primary character set active (ALTCHARSET off), the character ROM index is derived as follows:

| Byte Range | ROM Index | Display Style |
|------------|-----------|---------------|
| $00-$1F | `ch` (0-31) | Inverse |
| $20-$3F | `ch` (32-63) | Inverse |
| $40-$5F | `ch & 0x1F` (0-31) | Flash |
| $60-$7F | `(ch & 0x1F) + 32` (32-63) | Flash |
| $80-$9F | `ch & 0x1F` (0-31) | Normal |
| $A0-$BF | `(ch & 0x1F) + 32` (32-63) | Normal |
| $C0-$DF | `ch & 0x1F` (0-31) | Normal |
| $E0-$FF | `(ch & 0x1F) + 96` (96-127) | Normal |

The ROM offset for each character is `charIndex * 8`.

### Alternate Character Set

When the ALTCHARSET switch is on (`$C00F`), the mapping changes significantly to provide MouseText characters:

| Byte Range | ROM Index | Rendering |
|------------|-----------|-----------|
| $00-$3F | `ch` | Inverse (no XOR) |
| $40-$5F | `ch` | XOR rendering, not inverse (MouseText) |
| $60-$7F | `ch` | XOR rendering (MouseText) |
| $80-$FF | Mapped via ranges | Normal (no XOR, no inverse) |

In the alternate set, flash behavior is disabled. Characters in the `$40-$7F` range display MouseText glyphs instead of flashing. The XOR flag means the ROM data bits are inverted (`rowData ^= 0xFF`) before rendering.

### UK Character Set

Setting `ukCharSet_` to true adds a `0x1000` (4096) byte offset to the character ROM address, selecting the second 4 KB half of the ROM. This models the physical character set switch on UK Apple IIe machines, which replaces certain ASCII characters (notably `#` with the pound sterling symbol).

---

## Screen Address Calculation

### Text and Lo-Res Addresses

Text and Lo-Res modes share the same address calculation. The base address for page 1 is `$0400`:

```
address = $0400 + TEXT_ROW_OFFSETS[row] + col
```

Page 2 adds `$0400` to the address (giving base `$0800`). For 80-column and Double Lo-Res modes, both main and auxiliary memory are read at the same address.

### Hi-Res Addresses

Hi-Res addresses are computed from three components:

```
block = row / 8        (which 8-line group: 0-23)
line  = row % 8        (line within the group: 0-7)
address = $2000 + TEXT_ROW_OFFSETS[block] + line * $400 + col
```

This interleaved layout means consecutive scanlines are **not** at consecutive memory addresses. Lines within an 8-line group are spaced `$400` (1024 bytes) apart. Page 2 uses `$4000` as the base instead of `$2000`.

### Row Offset Table

The `TEXT_ROW_OFFSETS` lookup table provides the base offset for each of the 24 text rows (or 8-line blocks in Hi-Res mode):

| Rows 0-7 | Rows 8-15 | Rows 16-23 |
|----------|-----------|------------|
| `$000` | `$028` | `$050` |
| `$080` | `$0A8` | `$0D0` |
| `$100` | `$128` | `$150` |
| `$180` | `$1A8` | `$1D0` |
| `$200` | `$228` | `$250` |
| `$280` | `$2A8` | `$2D0` |
| `$300` | `$328` | `$350` |
| `$380` | `$3A8` | `$3D0` |

This non-linear layout is a consequence of the original Apple II hardware design, which used a simple counter with specific bit-wiring to generate addresses for both the text and Hi-Res display circuitry.

---

## Per-Scanline Progressive Rendering

### Horizontal Timing

Each of the 262 scanlines per frame takes exactly 65 CPU cycles:

| Cycles | Purpose |
|--------|---------|
| 0-24 | Horizontal blanking (25 cycles) |
| 25-64 | Visible display (40 cycles, one byte column per cycle) |

Only the first 192 scanlines contain visible display data. Scanlines 192-261 are vertical blanking. The constants are defined in `types.hpp`:

| Constant | Value |
|----------|-------|
| `CYCLES_PER_SCANLINE` | 65 |
| `SCANLINES_PER_FRAME` | 262 |
| `CYCLES_PER_FRAME` | 17,030 |

### Switch Change Log

The `Video` class maintains a change log of video switch modifications during each frame. When the MMU detects a write to a video-relevant soft switch, it calls `onVideoSwitchChanged()`, which:

1. Captures the full `VideoSwitchState` from the MMU's current soft switch state
2. Compares against the last logged state (or `frameStartState_` if the log is empty) to avoid redundant entries
3. Records the state snapshot along with its cycle offset from the frame start
4. Stores up to `MAX_SWITCH_CHANGES` (1024) entries per frame; excess changes are silently dropped

The `VideoSwitchState` struct captures exactly the 8 switches that affect rendering:

| Field | Switch | Purpose |
|-------|--------|---------|
| `text` | TEXT | Text vs. graphics mode |
| `mixed` | MIXED | Bottom 4 rows forced to text |
| `page2` | PAGE2 | Display page selection |
| `hires` | HIRES | Hi-Res vs. Lo-Res |
| `col80` | 80COL | 80-column / double-width enable |
| `altCharSet` | ALTCHARSET | Alternate character set (MouseText) |
| `store80` | 80STORE | PAGE2 redirected to bank switch |
| `an3` | AN3 | Double-resolution enable |

### Video Pipeline Delay

When logging a switch change, the cycle offset includes a **+2 cycle adjustment** to model the Apple IIe's two-stage video pipeline:

1. **Phi-0/Phi-1 bus phasing**: The video hardware fetches memory on Phi-0 (first half of each clock cycle) before the CPU writes on Phi-1 (second half). A soft switch change on cycle N misses that cycle's video fetch.

2. **Shift register latching**: The byte fetched on Phi-0 of cycle N+1 is loaded into the shift register and does not produce visible dots until approximately cycle N+2.

Combined: a CPU write on cycle N affects display output at cycle N+2. This delay is essential for accurate emulation of demo effects that rely on precise switch timing.

### Progressive Rendering Loop

`renderUpToCycle(currentCycle)` is called after each CPU instruction executes. It renders all scanlines whose 65 cycles are fully complete:

1. Calculate `completedScanlines = frameCycle / CYCLES_PER_SCANLINE`
2. Target the last fully-elapsed scanline (`completedScanlines - 1`), capping at 191
3. Render each unrendered scanline sequentially via `renderScanlineWithChanges()`
4. Track progress with `lastRenderedScanline_` (initialized to -1 at frame start)

Rendering one scanline behind the current execution point ensures all CPU writes during a scanline are captured before video memory is read for that scanline. This is critical for raster bar effects and other mid-frame tricks that modify video memory or switches just before the beam reaches them.

### Scanline-With-Changes Rendering

`renderScanlineWithChanges()` processes a single scanline in two phases using the switch change log:

**Phase 1 -- HBLANK changes** (cycles 0-24): Consume all switch changes occurring during horizontal blanking by advancing `changeIdx_` through the log and updating `currentRenderState_`. No pixels are rendered during hblank.

**Phase 2 -- Visible area** (cycles 25-64, mapped to columns 0-39): Walk the remaining changes within the visible area. Each change splits the scanline into segments:

```
col = 0
For each change in the visible area:
    changeCol = changeCycle - visibleStartCycle
    Render columns [col, changeCol) with currentRenderState_
    Update currentRenderState_ from the change log entry
    col = changeCol
Render remaining columns [col, 40) with final currentRenderState_
```

This segment-based approach allows mid-scanline mode switches -- a program can change from text to Hi-Res partway across a scanline, and both modes will render correctly on their respective sides of the switch point.

### Scanline Segment Dispatch

`renderScanlineSegment()` dispatches a column range `[startCol, endCol)` on a single scanline to the correct mode-specific renderer based on the current `VideoSwitchState`:

1. If `scanline >= 192` or empty range, return immediately
2. If `mixed` and `scanline >= 160` and not in text mode, render as text (40-col or 80-col)
3. If `text`, dispatch to `renderText40Scanline()` or `renderText80Scanline()`
4. If `hires`, dispatch to `renderDoubleHiResScanline()` (if `col80 && !an3`) or `renderHiResScanline()`
5. Otherwise dispatch to `renderDoubleLoResScanline()` (if `col80 && !an3`) or `renderLoResScanline()`

---

## Frame Lifecycle

### Frame Boundaries

Frame boundaries are managed by the `Emulator` class (see [[Architecture-Overview]]). At each frame boundary:

1. **`renderFrame()`** is called to finalize the current frame:
   - Increment the flash counter and toggle flash state if threshold reached
   - Render any remaining unrendered scanlines (progressive rendering usually handles most by this point)
   - Set `frameDirty_ = true` to signal that the framebuffer is ready for upload

2. **`beginNewFrame(cycleStart)`** resets state for the next frame:
   - Save the frame start cycle (`frameStartCycle_`)
   - Capture the initial video switch state (`frameStartState_`)
   - Clear the switch change log (`switchChangeCount_ = 0`)
   - Reset `lastRenderedScanline_` to -1
   - Reset `changeIdx_` to 0
   - Set `currentRenderState_` to the frame start state

### Flash Counter

Text characters in the flash range ($40-$7F in the primary character set) toggle between normal and inverse display. The flash state toggles every `FLASH_RATE` (16) frames, producing approximately 3.75 Hz blinking at 60 fps.

Flash only applies when using the primary character set. The alternate character set disables flash and displays MouseText glyphs in the $40-$7F range instead.

### Force Render

`forceRenderFrame()` re-renders the entire screen from current memory state in a single pass, ignoring the progressive rendering pipeline and change log. It captures the current video switch state and renders all 192 scanlines with that uniform state. This is used by the debugger for screen refresh after stepping instructions, where the progressive rendering state may be stale.

---

## Video Soft Switches

Video-related soft switches and their addresses:

| Address | Function | Type |
|---------|----------|------|
| $C050 | Graphics mode | Write |
| $C051 | Text mode | Write |
| $C052 | Full-screen (no mixed) | Write |
| $C053 | Mixed mode (4 text lines at bottom) | Write |
| $C054 | Display page 1 | Write |
| $C055 | Display page 2 | Write |
| $C056 | Lo-Res mode | Write |
| $C057 | Hi-Res mode | Write |
| $C05E | Annunciator 3 ON (disables double-res) | Write |
| $C05F | Annunciator 3 OFF (enables double-res) | Write |
| $C00C | 40-column mode | Write |
| $C00D | 80-column mode | Write |
| $C00E | Primary character set | Write |
| $C00F | Alternate character set (MouseText) | Write |
| $C000 | 80STORE off | Write |
| $C001 | 80STORE on | Write |
| $C019 | VBL status (bit 7: 1 = not in VBL) | Read |
| $C01A | TEXT status | Read |
| $C01B | MIXED status | Read |
| $C01C | PAGE2 status | Read |
| $C01D | HIRES status | Read |
| $C01E | ALTCHARSET status | Read |
| $C01F | 80COL status | Read |

See [[Memory-System]] for the full soft switch reference including memory banking switches.

---

## Display Pipeline

After the C++ Video class produces the framebuffer, the JavaScript layer handles display:

```
C++ Video (560x384 RGBA framebuffer)
  --> WASM _getFramebuffer() returns pointer into WASM heap
  --> Worker writes it into a shared framebuffer slot (SharedArrayBuffer)
  --> main thread claims it with Atomics.exchange in pollSharedFrame()
  --> WebGLRenderer.updateTexture(data)
  --> Fragment shader applies CRT effects
  --> Canvas displays final output
```

Frame display is triggered by the audio-driven timing system. When enough audio samples have been generated to represent one frame (~800 samples at 48 kHz / 60 Hz), the `AudioDriver` fires `onFrameReady`, which uploads the latest framebuffer to the WebGL texture. See [[Architecture-Overview]] for details on the audio-driven timing chain.

---

## WebGL Shader Pipeline

The JavaScript renderer (`src/js/display/webgl-renderer.js`) uploads the completed framebuffer to a WebGL texture each frame, then applies a multi-pass shader pipeline:

### Pass 1: CRT Fragment Shader

The main rendering pass applies CRT and analog display effects in a single draw call:

- Screen curvature (barrel distortion)
- Scanlines and shadow mask
- Phosphor glow (bloom)
- Vignette
- Chromatic aberration (RGB offset)
- Flicker and static noise
- Jitter and horizontal sync distortion
- Glowing scan beam line
- Ambient light reflection
- Color bleed (vertical inter-scanline blending)
- NTSC fringing
- Monochrome phosphor tinting (green, amber, or white)
- Screen border and rounded corners

All parameters are passed as WebGL uniforms and update in real time.

### Pass 2: Burn-In Shader

A double-buffered framebuffer pair accumulates bright pixel values and decays them over time. The resulting burn-in texture is sampled by the main CRT shader to blend phosphor persistence into the display.

### Pass 3: Edge Overlay

A final compositing pass draws a subtle highlight along the screen border, simulating light reflecting off CRT glass edges.

### Texture Configuration

- **Source texture**: 560 x 384 RGBA, updated from WASM memory each frame
- **Filtering**: Bilinear by default; nearest-neighbor when Sharp Pixels is enabled
- **Selection overlay**: A separate texture for text selection highlighting, composited in the CRT shader
- **Burn-in textures**: Two framebuffer-sized textures ping-ponged for temporal accumulation

---

## Display Settings Integration

All shader parameters are exposed through the [[Display-Settings]] window. Settings are stored in `localStorage` as JSON and applied at startup. The `WebGLRenderer.setParam(name, value)` method maps setting names to uniform locations, allowing real-time adjustment as the user drags sliders.

---

## Super Hi-Res and the IIgs Raster

A IIgs has two video systems and `$C029` switches between them. In //e mode the Mega II's picture is drawn by the same `Video` class every other machine uses, reading the Mega II's MMU. In Super Hi-Res the VGC draws 320 or 640 pixels a line with **a palette per scanline**, chosen by that line's control byte — which is why it is not one mode but two hundred of them.

**A IIgs's frame is the raster a monitor shows, border included.** The Mega II's line is 65 cycles — 40 of picture, 12 of blanking, 13 of border — and its frame 262 lines, of which 200 are picture and 40 border. What is drawn is the part of that a monitor's bezel does not hide: three cycles either side and twelve lines above and below, which keeps the border's shape and puts it at about the width it has on the glass. Drawing every border cycle made it a fifth of the picture's width, which no monitor of the period showed.

At 16 pixels a cycle the frame is therefore **736x448** (lines doubled), with the 640x400 picture at (48, 24). The //e's 560x384 goes in the same width, **stretched to 640** — eight pixels for every seven dots — because a text screen and a Super Hi-Res screen are the same width on the monitor, and it is centred in the 200 lines. The profile's `text` rectangle says where the text screen landed, so text selection maps a pointer onto a cell through it rather than assuming the text fills the frame, and its `aspect` says the shape the monitor shows the frame at: a //e's own ratio for the 8-bit machines, and 4:3 for the IIgs raster.

**A IIgs's text is drawn, not transmitted.** `$C022` holds the two colours the VGC substitutes for lit and unlit text dots, and the bottom nibble of `$C034` the border. A text line is decoded into those two colours instead of through an NTSC receiver. Monochrome still overrides it, because a monochrome monitor has one phosphor whatever the machine sent.

**`$C029` bit 5 shows double hi-res in black and white.** The System 6 Finder and the 80-column desktop programs draw a 560-dot double hi-res picture with Super Hi-Res off and this bit set, and the VGC shows the dots as they are. Decoding them into colour instead fringed every letter red and green.

## See Also

- [[Architecture-Overview]] -- Two-layer design, audio-driven timing, frame synchronization
- [[CPU-Emulation]] -- 65C02 processor driving the rendering timing
- [[Memory-System]] -- MMU, soft switches, auxiliary memory bank switching
- [[Display-Settings]] -- CRT shader effect configuration
