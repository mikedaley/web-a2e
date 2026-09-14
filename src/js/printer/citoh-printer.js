/*
 * citoh-printer.js - C. Itoh 8510 command-set printer core
 *
 * Shared parent for the Apple dot-matrix printers built on the C. Itoh 8510
 * command set: the Apple DMP, ImageWriter I, and ImageWriter II. It owns the
 * full ESC parser, render state, custom-character loader, graphics, and the
 * 8510 correspondence base face (IW2_STANDARD_FIXED / IW2_STANDARD_PROP). The
 * draft / NLQ font tiers are null stubs here — only the ImageWriter II adds
 * those ROMs, so the II becomes an additive subclass instead of the literal
 * parent of the I and the DMP.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 *  Shawn Bullock <shawn@agenticexpert.ai>
 */

import { PrinterBase } from "./printer-base.js";
import { IW2_STANDARD_FIXED, IW2_STANDARD_FIXED_LOCALES } from "./imagewriter-ii-rom-standard-fixed.js";
import { IW2_STANDARD_PROP, IW2_STANDARD_PROP_LOCALES } from "./imagewriter-ii-rom-standard-prop.js";

const S_NORMAL       = 0; // normal character output
const S_ESC          = 1; // consumed ESC, waiting for command byte
const S_PARAM1       = 2; // consuming one parameter byte then back to normal
const S_IMG_COUNT    = 3; // graphics: consuming ASCII-decimal byte/group count
const S_IMG_DATA     = 4; // graphics: consuming image data bytes
const S_CUSTOM_KEY   = 5; // custom char load: waiting for KEY byte or CTRL-D
const S_CUSTOM_WIDTH = 6; // custom char load: waiting for WIDTH CODE byte
const S_CUSTOM_DATA  = 7; // custom char load: consuming column data bytes
const S_NUM          = 8; // collecting an ASCII-decimal numeric parameter
const S_REPEAT_CHAR  = 9; // ESC R: waiting for the char byte to repeat
const S_VREPEAT      = 10; // ESC V: waiting for the column byte to repeat
const S_TAB_SET      = 11; // ESC ( : collecting a comma-separated tab-column list to SET
const S_TAB_CLEAR    = 12; // ESC ) : collecting a comma-separated tab-column list to CLEAR
const S_SWITCH       = 13; // ESC D/Z : collecting the two software-switch pattern bytes

// Internal dot scale is owned by PrinterBase (this.dpi, default 480 = LCM of
// 80/120/160). The DPI-derived dot pitches live on the instance, recomputed from
// this.dpi in _recomputeUnits(): this._dotW (120-dpi draft column), this._dotV
// (72-dpi vertical pitch), this._platenDots (8" printable width).

// The eight horizontal pitches (Table 8-2). Each sets BOTH the text advance
// (cpi — the fixed pitches advance a constant cell; the two proportional
// pitches advance per-glyph from the corr-prop ROM widths, see _emitChar) AND
// the graphics dot density used by every ESC G/S/g/V/F command. Keeping the two
// in one table guarantees graphics density always tracks the pitch, exactly as
// the manual requires. propPica/propElite keep a nominal cpi here only for
// column-addressed commands (ESC L margin).
const CPI = {
  extended: 9, pica: 10, elite: 12, semicond: 13.4,
  condensed: 15, ultra: 17, propPica: 10, propElite: 12,
};

// ESC G/S/g/V graphics: horizontal dot density (dots/inch) by the pitch in
// force (Table 8-2). Each graphics data byte is one 8-dot-high column at this
// density; ESC F head placement counts dot columns in these same units.
const GFX_DENSITY = {
  extended: 72, pica: 80, elite: 96, semicond: 107,
  condensed: 120, ultra: 136, propPica: 144, propElite: 160,
};

// Printable carriage width: 8 inches → this._platenDots (set in _recomputeUnits).
// Head auto-wraps (CR+LF) at this margin, just like real hardware.

// ESC K colour index → ribbon band (Table 8-6). The colour ribbon's four
// physical bands are black, yellow, magenta (purplish-red) and cyan
// (greenish-blue); 4-6 are the secondaries the printer makes AUTOMATICALLY by
// overprinting two bands (orange = yellow+magenta, green = yellow+cyan,
// purple = magenta+cyan). Other printer manuals colloquially call magenta
// "red" and the magenta+cyan purple "blue" — the manual flags this itself.
const COLORS = ["black", "yellow", "magenta", "cyan", "orange", "green", "purple"];

export class CItohPrinter extends PrinterBase {
  constructor() {
    super();
    this._customMaxWidth = 8;
    this._customChars    = new Map();
    // Automatic Line Feed DIP (SW2-1). A hardware switch, so it survives a
    // software reset (ESC c) — set once, kept until the operator flips it.
    this._autoLF         = true;
    // Software select (CTRL-Q/CTRL-S, Table 6-6). Only acts when SWA-5 enables
    // it (ESC Z CTRL-P CTRL-@); a DIP-like switch, so it survives a software
    // reset. While deselected the printer ignores all data until a CTRL-Q
    // reselects it. Default: select disabled (so a stray DC3/XOFF is inert).
    this._swSelectEnabled = false;
    this._deselected      = false;
    // Include/ignore the 8th data bit (ESC Z/D CTRL-@ SPACE, Group B 0x20; Tables
    // 6-4 / 7-3). Power-on = ignore: strip bit 7 so Applesoft's high-bit ASCII
    // prints as normal characters. ESC Z includes it (high-ASCII selects the ESC *
    // custom high set / true 8-bit data). DIP-like: survives a software reset,
    // exactly like _swSelectEnabled above.
    this._includeEighth   = false;
    // Paper-out sensor (ESC O/o, Table 5-13). DIP-like, default on. On real hw it
    // deselects the printer when ~7/144" of paper remains; our virtual paper is
    // endless and no SheetFeeder is modelled, so the flag never trips — but the
    // O/o byte must still be consumed so it doesn't print as a literal letter.
    this._paperOutSensor  = true;
    this._resetRenderState();
    this._resetParserState();
  }

  // Automatic Line Feed (DIP SW2-1). ON: a CR also advances the paper one line
  // (and a following LF is swallowed) — the way Applesoft, which emits CR only,
  // expects to print readable text. OFF: a CR returns the head WITHOUT feeding,
  // so multi-pass colour graphics overprint on the same band register correctly;
  // the paper advances only on an explicit LF (how DazzleDraw / Print Shop drive
  // colour). Default ON.
  setAutoLineFeed(on) { this._autoLF = !!on; }
  getAutoLineFeed()   { return this._autoLF; }

  // ---- Paper-geometry capability (paper-sizes.md) ----
  // The C.Itoh family is center-referenced with both sprockets moving symmetric
  // (base defaults); the tractor feed is built-in & adjustable (no add-on regime
  // like the FX-80). Range is the paper BODY (strips off) shared by the DMP and
  // IW-I: overall tractor stock 4.5"–10" → body 3.5"–9.0" after the ½"/side
  // sprocket strips. The IW-II, with a tighter 3.5" pin-to-pin, overrides the floor.
  paperWidthRange() { return { min: 3.5, max: 9.0 }; }

  // Operator panel: Auto-LF (inherited) + power-on character pitch. The ESC
  // pitch codes (N/E/e/q/Q/n/p/P) still override this live while printing.
  static get SETTINGS() {
    return [...super.SETTINGS];
  }

  _resetParserState() {
    this._state          = S_NORMAL;
    this._imgCount       = 0;
    this._imgDotW        = this.dpi / 80;
    this._gfxDigitsLeft  = 0;
    this._gfxCountAcc    = 0;
    this._gfxMul         = 1;
    this._customKey      = 0;
    this._customWireTop  = true;
    this._customDataLeft = 0;
    this._customDataBuf  = [];
    this._paramCmd       = 0;
    this._numCmd         = 0;   // ASCII-decimal param collector (ESC H/T/L/F/V/R)
    this._numLeft        = 0;
    this._numAcc         = 0;
    this._tabListAcc     = [];  // ESC (/) — columns parsed so far from the list
    this._tabListCur     = 0;   // ESC (/) — current digit accumulator
    this._tabListHasDig  = false; // ESC (/) — saw a digit since the last delimiter
    this._swPos          = 0;   // ESC D/Z — switch pattern bytes consumed so far
    this._swA            = 0;   // ESC D/Z — first (Group A) pattern byte
    this._swIsD          = false; // ESC D/Z — true = ESC D (close/on), false = ESC Z (open/off)
  }

  _resetRenderState() {
    this._pitch          = this._defaultPitch ?? "pica";
    this._bold           = false;
    this._underline      = false;
    this._halfHeight     = false;     // ESC w/W (Table 4-10)
    this._script         = "none";    // ESC x/y/z: 'none' | 'super' | 'sub' (Table 4-11)
    this._doubleWidth    = false;     // CTRL-N/CTRL-O (Table 4-7)
    // A proportional power-on pitch (DMP DIP SW2-5 → default propElite) IS
    // proportional mode, exactly as if ESC P had been sent — pitch and the
    // proportional flag must never disagree.
    this._proportional   = this._pitch === "propPica" || this._pitch === "propElite";
    this._propSpacing    = 0;         // ESC s n — extra inter-char dot gap, 0-9 (Table A-11)
    this._pendingDotSpace = 0;        // ESC 1-6 — one-shot dot spaces before next char (Table 4-6)
    this._crBeforeLF     = true;      // ESC l — insert CR before LF/FF (Table A-16, default on)
    this._feedDir        = 1;         // ESC f/r — paper feed direction: +1 forward (default), -1 reverse (Table 5-11)
    this._color          = "black";
    this._customFont     = "none";    // ESC '/* — render downloaded glyphs: 'none' | 'low' | 'high'
    this._leftMargin     = 0;         // ESC L — left margin, internal dots
    this.head.leftMargin = 0;
    this._lineHeight     = this.dpi / 6;   // 6 lpi default
    this._quality        = "draft";   // power-on font (Table 4-1): draft | corr | nlq
    this._xDot           = 0;
    this._lastAdvance    = 0;         // escapement of the last glyph emitted — CTRL-H backspace steps this (proportional/custom aware)
    this._yDot           = this._homeYDot();   // power-on head rest, a hair below sheet top
    this._unidirectional = false;     // power-on default is bidirectional (ESC <)
    this._mouseText      = false;     // ESC &/$ — map MouseText into low ASCII $40-$5F
    this._htabCols       = [];        // sorted integer column stops (ESC (/)/0, HT)
  }

  reset() {
    // Custom chars survive software reset per spec Ch.6
    super.reset();             // carriage/head kinematics back to home
    this._resetParserState();
    this._resetRenderState();
    // Carriage returns to draft speed automatically — the head pulls cps fresh.
  }

  // Derive the DPI-dependent dot pitches from the current internal scale.
  // dotW = 120-dpi draft column, dotV = 72-dpi vertical pitch, platen =
  // carriageWidthInch() wide (8" — base capability, single source).
  _recomputeUnits() {
    this._dotW       = this.dpi / 120;
    this._dotV       = this.dpi / 72;
    this._platenDots = this.dpi * this.carriageWidthInch();
  }

  // Head: pica (10 cpi) advances dpi/10 dots → carriage velocity (dpi/10) × cps
  // (48 dots at the default 480 dpi).
  _carriagePicaDots() { return this.dpi / 10; }

  receiveByte(byte) {
    // Strip the Apple II high bit unless ESC Z enabled 8-bit mode, in which case
    // the full byte is data: high-ASCII selects the custom high set / 8-bit graphics.
    const ch = this._includeEighth ? byte : (byte & 0x7F);

    // Software-deselected (CTRL-S while SWA-5 enabled): swallow every byte until
    // a CTRL-Q (DC1) reselects the printer (Table 6-6). Mirrors the real DTR
    // freeze — nothing prints, no state advances, in any parser mode.
    if (this._deselected) {
      if (ch === 0x11) this._deselected = false;
      return;
    }

    switch (this._state) {
      case S_NORMAL: {
        // Apple/SSC terminate each line with CR+LF, so a literal printer would
        // feed twice — a blank line between rows. A real ImageWriter treats CR+LF
        // as one line ending: coalesce an LF that arrives immediately after a CR
        // (feed once). A standalone LF still feeds normally.
        //
        // "Immediately" means with no INK in between, not no bytes: an escape
        // sequence that prints nothing leaves the pairing armed. The GS/OS
        // ImageWriter driver writes `CR, ESC T 16, LF` before every 8-dot
        // graphics band — the escape sets the distance for the very LF that
        // follows it — so a pairing that any ESC byte broke fed twice per band
        // and left a 1/8" white stripe through every line of a printed page.
        // The arm is dropped by anything that lays ink down (_inked) and by a
        // feed, so `CR, ESC G <data>, LF` still feeds as it should.
        const wasCR = this._lastCR;
        if (ch !== 0x1B) this._lastCR = false;
        if (ch === 0x1B) {
          this._state = S_ESC;
        } else if (ch === 0x0E) {
          this._doubleWidth = true;   // CTRL-N — double-width on
        } else if (ch === 0x0F) {
          this._doubleWidth = false;  // CTRL-O — double-width off
        } else if (ch === 0x08) {
          // CTRL-H backspace: step head back over the LAST glyph emitted, no paper
          // feed — using that glyph's actual escapement, so a proportional or a
          // downloaded custom char steps back its true width, not a fixed cell.
          // Falls back to one cell before anything prints. Clamped at the left
          // margin. Reprinting here overstrikes the prior glyph (strikethrough).
          const backStep = this._lastAdvance || this._charAdvance();
          this._xDot = Math.max(this._leftMargin, this._xDot - backStep);
          this.emit("backspace");
        } else if (ch === 0x09) {
          this._horizontalTab();   // CTRL-I / HT — advance to next tab stop
        } else if (ch === 0x0C) {
          this.formFeed();   // slew to next top-of-form (shared with panel)
        } else if (ch === 0x0D) {
          // CR returns the head to the left margin. Whether it also feeds paper
          // is the Automatic Line Feed DIP (SW2-1). Auto-LF ON: feed one line
          // and arm CR+LF coalescing (a trailing LF is swallowed) — what plain
          // text (Applesoft sends CR only) needs. Auto-LF OFF: return only, no
          // feed, so colour overprint passes stack on the same band.
          this._xDot = this._leftMargin;
          if (this._autoLF) {
            this._yDot += this._feedDir * this._lineHeight;
            this._lastCR = true;   // arm CR+LF coalescing
            this.emit("newline");
          } else {
            this.emit("carriagereturn");   // head home, no paper feed
          }
        } else if (ch === 0x0A) {
          if (!(this._autoLF && wasCR)) {  // LF paired with an auto-LF CR is swallowed; otherwise it feeds
            // ESC l 0 (default): a CR is inserted before the LF, returning the
            // head to the left margin. ESC l 1 suppresses it (LF feeds only).
            if (this._crBeforeLF) this._xDot = this._leftMargin;
            this._yDot += this._feedDir * this._lineHeight;
            this.emit("linefeed");
          }
        } else if (ch === 0x18) {
          // CTRL-X — erase the current line from the print buffer (Table A-19):
          // drop the strikes accumulated since the last line terminator and put
          // the logical column back at the left margin. No paper motion.
          this._lineBuf = [];
          this._xDot = this._leftMargin;
        } else if (ch === 0x13) {
          // CTRL-S (DC3) — deselect the printer, but ONLY when SWA-5 has enabled
          // software select (ESC Z …). Default off, so a stray DC3/XOFF used as
          // flow control in the data stream stays inert (Table 6-6).
          if (this._swSelectEnabled) this._deselected = true;
        } else if (ch === 0x1F) {
          // CTRL-_ n — feed blank lines OR EVFU vertical tab (Table 5-11). The
          // follower byte n is consumed next: a count ($31-$3F = 1-15) feeds that
          // many blank lines; a tab letter (A-F) is an EVFU drop. Reuses the
          // one-param-byte machinery; the digit/letter split happens in S_PARAM1.
          this._paramCmd = 0x1F;
          this._state    = S_PARAM1;
        } else if ((ch >= 0x20 && ch < 0x7F) || (this._includeEighth && ch >= 0xA0)) {
          // ESC & maps the 32 MouseText glyphs into low ASCII $40-$5F (Table 4-2):
          // each lives at code+$80 in the ROM ($C0-$DF). Outside that window, or
          // after ESC $, codes print as standard ASCII. In 8-bit mode (ESC Z) the
          // high-ASCII band $A0-$FF passes straight through as its own code — the
          // ESC * custom high set (or the high-ASCII ROM face).
          const code = (this._mouseText && ch >= 0x40 && ch <= 0x5F) ? ch + 0x80 : ch;
          this._emitChar(code);
        }
        break;
      }

      case S_ESC:
        this._state = S_NORMAL;
        this._handleEsc(ch);
        break;

      case S_PARAM1:
        switch (this._paramCmd) {
          case 0x4B: {       // ESC K n — color select (Table A-18), n = ASCII '0'-'6'
            const k = (ch >= 0x30 && ch <= 0x36) ? ch - 0x30 : (ch & 0x07);
            this._color = COLORS[k] ?? "black";   // non-black halves carriage speed (pulled at next move)
            break;
          }
          case 0x61: {       // ESC a n — font select (Table 4-1): 0=corr 1=draft 2=NLQ
            const sel = ch & 0x0F;  // tolerate ASCII '0'/'1'/'2' or raw 0/1/2
            if      (sel === 0) this._quality = "corr";
            else if (sel === 1) this._quality = "draft";
            else if (sel === 2) this._quality = "nlq";
            break;
          }
          case 0x73: {       // ESC s n — proportional inter-char dot spacing (Table A-11)
            const n = (ch >= 0x30 && ch <= 0x39) ? ch - 0x30 : (ch & 0x0F);
            this._propSpacing = Math.max(0, Math.min(9, n));
            break;
          }
          case 0x6C:         // ESC l n — CR insertion before LF/FF (Table A-16)
            this._crBeforeLF = (ch === 0x30);  // '0' = insert CR (default); '1' = no insert
            break;
          case 0x1F:         // CTRL-_ n — feed blank lines (digit) / EVFU tab (letter)
            if (ch >= 0x31 && ch <= 0x3F) {     // n = $31-$3F → 1-15 blank lines (Table 5-11)
              const lines = ch - 0x30;
              if (this._crBeforeLF) this._xDot = this._leftMargin;
              this._yDot += this._feedDir * lines * this._lineHeight;
              this.emit("linefeed");
            } else {
              this._verticalFormTab(ch);        // CTRL-_ A-F → EVFU drop (DMP); no-op in the base
            }
            break;
        }
        this._state = S_NORMAL;
        break;

      case S_IMG_COUNT: {
        // Count digits are ASCII numerals; leading zeros may be sent as spaces.
        const d = (ch === 0x20) ? 0 : (ch - 0x30);
        if (d >= 0 && d <= 9) this._gfxCountAcc = this._gfxCountAcc * 10 + d;
        if (--this._gfxDigitsLeft <= 0) {
          this._imgCount = this._gfxCountAcc * this._gfxMul;
          this._state = this._imgCount > 0 ? S_IMG_DATA : S_NORMAL;
        }
        break;
      }

      case S_IMG_DATA:
        // Graphics column data uses all 8 bits (bit 7 = bottom dot), so feed the
        // raw byte — NOT the high-bit-stripped `ch` used for character codes.
        this._inked();
        this.emit("printDots", {
          byte:  byte,
          xDot:  this._xDot,
          yDot:  this._yDot,
          dotW:  this._imgDotW,
          dotH:  this._dotV,
          color: this._inkColor(this._color),
        });
        this._xDot += this._imgDotW;
        if (--this._imgCount <= 0) this._state = S_NORMAL;
        break;

      case S_CUSTOM_KEY:
        if (ch === 0x04) {
          this._state = S_NORMAL;
        } else {
          this._customKey = ch;
          this._state     = S_CUSTOM_WIDTH;
        }
        break;

      case S_CUSTOM_WIDTH: {
        let cols, wireTop;
        if (ch >= 0x41 && ch <= 0x50) {
          cols = ch - 0x40; wireTop = true;
        } else if (ch >= 0x61 && ch <= 0x70) {
          cols = ch - 0x60; wireTop = false;
        } else {
          this._state = S_NORMAL;
          break;
        }
        this._customWireTop  = wireTop;
        this._customDataLeft = Math.min(cols, this._customMaxWidth);
        this._customDataBuf  = [];
        this._state          = S_CUSTOM_DATA;
        break;
      }

      case S_CUSTOM_DATA:
        this._customDataBuf.push(byte);  // 8-bit column data (bit 7 = bottom dot)
        if (--this._customDataLeft <= 0) {
          this._customChars.set(this._customKey, {
            wireTop: this._customWireTop,
            data: new Uint8Array(this._customDataBuf),
          });
          this._state = S_CUSTOM_KEY;
        }
        break;

      case S_NUM: {
        // ASCII-decimal parameter (leading zeros may be spaces, $20 → 0).
        const d = (ch === 0x20) ? 0 : (ch - 0x30);
        if (d >= 0 && d <= 9) this._numAcc = this._numAcc * 10 + d;
        if (--this._numLeft <= 0) this._dispatchNum();
        break;
      }

      case S_REPEAT_CHAR:
        // ESC R nnn c — print char c, _numAcc times, at the current pitch.
        for (let i = 0; i < this._numAcc; i++) this._emitChar(ch);
        this._state = S_NORMAL;
        break;

      case S_VREPEAT:
        // ESC V nnnn c — print column byte c (8-bit) as _numAcc graphics columns.
        this._inked();
        for (let i = 0; i < this._numAcc; i++) {
          this.emit("printDots", {
            byte:  byte,
            xDot:  this._xDot,
            yDot:  this._yDot,
            dotW:  this._gfxDotW(),
            dotH:  this._dotV,
            color: this._inkColor(this._color),
          });
          this._xDot += this._gfxDotW();
        }
        this._state = S_NORMAL;
        break;

      case S_TAB_SET:
      case S_TAB_CLEAR:
        // ESC ( / ESC ) — collect a list of ASCII-decimal column numbers
        // separated by any non-digit delimiter (comma), terminated by '.'
        // (0x2E). On the terminator, SET adds the listed stops, CLEAR removes
        // them; then back to normal text.
        if (ch >= 0x30 && ch <= 0x39) {
          this._tabListCur = this._tabListCur * 10 + (ch - 0x30);
          this._tabListHasDig = true;
        } else if (ch === 0x2E) {                 // '.' terminator
          if (this._tabListHasDig) this._tabListAcc.push(this._tabListCur);
          if (this._state === S_TAB_SET) this._setHTabs(this._tabListAcc);
          else                           this._clearHTabs(this._tabListAcc);
          this._state = S_NORMAL;
        } else {                                  // delimiter (comma, space, …)
          if (this._tabListHasDig) this._tabListAcc.push(this._tabListCur);
          this._tabListCur    = 0;
          this._tabListHasDig = false;
        }
        break;

      case S_SWITCH:
        // ESC D/Z take two raw pattern bytes; bit 0x80 (auto-LF) is significant,
        // so consume the raw `byte`, not the high-bit-stripped `ch`.
        if (this._swPos === 0) {
          this._swA   = byte;
          this._swPos = 1;
        } else {
          this._applySoftSwitch(this._swA, byte, this._swIsD);
          this._state = S_NORMAL;
        }
        break;
    }
  }

  // Dispatch an ESC command byte. Split out from receiveByte so subclasses
  // (the monochrome ImageWriter I) can override individual codes without
  // duplicating the whole parser.
  _handleEsc(ch) {
    switch (ch) {
      // Character pitch. ESC p/P are the proportional pitches; selecting one
      // forces the correspondence font (proportional isn't a draft/NLQ feature).
      case 0x6E: this._pitch = "extended";  this._proportional = false; break;  // ESC n — extended (9 cpi, 72 dpi gfx)
      case 0x4E: this._pitch = "pica";      this._proportional = false; break;  // ESC N — pica (10 cpi, 80 dpi gfx)
      case 0x45: this._pitch = "elite";     this._proportional = false; break;  // ESC E — elite (12 cpi, 96 dpi gfx)
      case 0x65: this._pitch = "semicond";  this._proportional = false; break;  // ESC e — semicondensed (13.4 cpi, 107 dpi gfx)
      case 0x71: this._pitch = "condensed"; this._proportional = false; break;  // ESC q — condensed (15 cpi, 120 dpi gfx)
      case 0x51: this._pitch = "ultra";     this._proportional = false; break;  // ESC Q — ultracondensed (17 cpi, 136 dpi gfx)
      case 0x70: this._pitch = "propPica";  this._proportional = true; break;  // ESC p — proportional-pica (144 dpi gfx)
      case 0x50: this._pitch = "propElite"; this._proportional = true; break;  // ESC P — proportional-elite (160 dpi gfx)

      // Print quality (Table 4-1). ESC m/ESC M are Apple Scribe aliases.
      case 0x6D: this._quality = "corr"; break;  // ESC m — correspondence font
      case 0x4D: this._quality = "nlq";  break;  // ESC M — NLQ font

      // Print style. Bold/half-height/super/subscript are draft-incompatible
      // (Tables 4-1/4-10/4-11): toggling them can shift the effective font and
      // therefore the carriage speed — the head pulls cps fresh at the next
      // motion, so no explicit re-arm is needed.
      case 0x21: this._bold      = true;  break;  // ESC ! — bold on
      case 0x22: this._bold      = false; break;  // ESC " — bold off
      case 0x58: this._underline = true;  break;  // ESC X — start underline
      case 0x59: this._underline = false; break;  // ESC Y — stop underline (Table 4-8)
      case 0x77: this._halfHeight = true;  break;  // ESC w — start half-height
      case 0x57: this._halfHeight = false; break;  // ESC W — stop half-height
      case 0x78: this._script = "super"; break;  // ESC x — start superscript
      case 0x79: this._script = "sub";   break;  // ESC y — start subscript
      case 0x7A: this._script = "none";  break;  // ESC z — stop super/subscript

      // Print-head motion (Table 5-5). Persists until cancelled or reset.
      case 0x3E: this._unidirectional = true;  break;  // ESC > — unidirectional
      case 0x3C: this._unidirectional = false; break;  // ESC < — bidirectional (default)

      // Character-set selection (Table 4-2). ESC & temporarily maps the 32
      // MouseText glyphs into low ASCII $40-$5F; ESC $ restores standard
      // ASCII. (8th-bit ESC Z/D mode is unmodelled — Applesoft sets bit 7 on
      // every byte, so the manual recommends ESC & for BASIC.)
      case 0x26: this._mouseText = true;  break;  // ESC & — map MouseText to $40-$5F
      case 0x24: this._mouseText = false; this._customFont = "none"; break;  // ESC $ — standard ASCII + normal font (default)

      // Downloaded-character font select (Table A-9). ESC '/* turn on the
      // custom glyph set (low/high ASCII); ESC $ (above) turns it back off.
      case 0x27: this._customFont = "low";  break;  // ESC ' — custom font, low ASCII
      case 0x2A: this._customFont = "high"; break;  // ESC * — custom font, high ASCII

      // Line spacing (Table A-15). These are IW-II native: ESC A/B are fixed
      // 6/8 lpi (no parameter), ESC T takes a 2-digit n/144 inch distance.
      case 0x41: this._lineHeight = this.dpi / 6; break;  // ESC A — 6 lpi
      case 0x42: this._lineHeight = this.dpi / 8; break;  // ESC B — 8 lpi
      case 0x54: this._beginNum(0x54, 2); break;     // ESC T nn — n/144 inch

      // Line feed direction (Table 5-11). Persists until the opposite command or
      // reset; applies to every subsequent line feed (CR/LF and CTRL-_).
      case 0x66: this._feedDir =  1; break;  // ESC f — forward feeding (default)
      case 0x72: this._feedDir = -1; break;  // ESC r — reverse feeding

      // Paper-out sensor (Table 5-13). Inert here — endless virtual paper, no
      // SheetFeeder — but consumed so the O/o byte never prints as a letter.
      case 0x4F: this._paperOutSensor = false; break;  // ESC O — sensor off
      case 0x6F: this._paperOutSensor = true;  break;  // ESC o — sensor on (default)

      // Page formatting (Table A-13).
      case 0x48: this._beginNum(0x48, 4); break;  // ESC H nnnn — page length n/144 inch
      case 0x4C: this._beginNum(0x4C, 3); break;  // ESC L nnn — left margin at column nnn

      // Horizontal tabs (DMP §9 / IW Table A-14 — identical byte format across
      // the whole C. Itoh family, so the machinery lives in the shared parent).
      // ESC ( a,b,…n.  set stops · ESC ) a,b,…n.  clear listed stops ·
      // ESC 0  clear all · ESC u nnn  add one stop · CTRL-I (HT)  advance to next.
      case 0x28:                                  // ESC ( — set tab list
        this._tabListAcc = []; this._tabListCur = 0; this._tabListHasDig = false;
        this._state = S_TAB_SET;
        break;
      case 0x29:                                  // ESC ) — clear tab list
        this._tabListAcc = []; this._tabListCur = 0; this._tabListHasDig = false;
        this._state = S_TAB_CLEAR;
        break;
      case 0x30: this._htabCols = []; break;       // ESC 0 — clear all tabs

      // ESC 1-6 (Table 4-6, the manual's "ESC m") — insert (ch-'0') extra dot
      // spaces before the NEXT character only; one-shot, but several in a row
      // accumulate. Consumed in _emitChar. (The Apple DMP overrides 0x31-0x36 as
      // a persistent proportional gap and returns before reaching here.)
      case 0x31: case 0x32: case 0x33:
      case 0x34: case 0x35: case 0x36:
        this._pendingDotSpace += ch - 0x30;
        break;
      case 0x75: this._beginNum(0x75, 3); break;  // ESC u nnn — add one tab stop at column nnn

      // Head placement / repeat (Tables A-14, A-19, 8-1).
      case 0x46: this._beginNum(0x46, 4); break;  // ESC F nnnn — head to dot column nnnn
      case 0x52: this._beginNum(0x52, 3); break;  // ESC R nnn c — repeat char c nnn times
      case 0x56: this._beginNum(0x56, 4); break;  // ESC V nnnn c — repeat column byte c

      // Software switches (Tables A-4/A-5). ESC D closes (turns ON) the switches
      // picked out by the two pattern bytes that follow; ESC Z opens (OFF) them.
      // Both consume exactly two raw bytes — see _applySoftSwitch.
      case 0x44: this._swIsD = true;  this._swPos = 0; this._state = S_SWITCH; break;  // ESC D a b
      case 0x5A: this._swIsD = false; this._swPos = 0; this._state = S_SWITCH; break;  // ESC Z a b

      // Set top-of-form to the current head position (Table A-15).
      case 0x76: this.setTopOfForm(); break;  // ESC v

      // Commands consuming one parameter byte
      case 0x61: // ESC a — font select
      case 0x4B: // ESC K — color select
      case 0x6C: // ESC l n — CR-before-LF/FF insertion (0 insert / 1 no-insert)
      case 0x73: // ESC s n — proportional inter-char dot spacing (0-9)
        this._paramCmd = ch;
        this._state    = S_PARAM1;
        break;

      // Bit-image graphics (Table 8-1). Density follows the current pitch.
      case 0x47:          // ESC G nnnn — nnnn = 4-digit ASCII byte count
      case 0x53:          // ESC S nnnn — identical to ESC G
        this._gfxDigitsLeft = 4; this._gfxCountAcc = 0; this._gfxMul = 1;
        this._imgDotW = this._gfxDotW();
        this._state   = S_IMG_COUNT;
        break;
      case 0x67:          // ESC g nnn — nnn = 3-digit ASCII count of 8-byte groups
        this._gfxDigitsLeft = 3; this._gfxCountAcc = 0; this._gfxMul = 8;
        this._imgDotW = this._gfxDotW();
        this._state   = S_IMG_COUNT;
        break;

      // Custom character width / clear
      case 0x2D: this._customMaxWidth = 8;  this._customChars.clear(); break;  // ESC -
      case 0x2B: this._customMaxWidth = 16; this._customChars.clear(); break;  // ESC +

      // Custom character load
      case 0x49: this._state = S_CUSTOM_KEY; break;  // ESC I

      // Software reset (render state resets; custom chars survive)
      case 0x63: this._resetRenderState(); this._resetParserState(); break;  // ESC c
    }
  }

  // Apply an ESC D/Z software-switch pattern (Tables A-4/A-5). `a` is the Group A
  // byte, `b` the Group B byte; `isD` is true for ESC D (close = switch ON), false
  // for ESC Z (open = OFF). Only the switches with a modelled effect act —
  // slash-zero (Group B, 0x01), auto-LF-after-CR (Group A, 0x80), software-select
  // (Group A, 0x10), 8th-data-bit (Group B, 0x20) and print-commands/end-of-line
  // (Group A, 0x40); the rest are consumed as documented no-ops so their pattern
  // bytes never print as text.
  _applySoftSwitch(a, b, isD) {
    if (b & 0x01) this.setSlashedZero(isD);   // zeros slashed (ESC D) / unslashed (ESC Z)
    if (a & 0x80) this.setAutoLineFeed(isD);  // add LF after CR (ESC D) / none (ESC Z)
    // SWA-5: software select responds only when this switch is OPEN, so ESC Z
    // (open) enables CTRL-Q/CTRL-S and ESC D (close) disables them — inverted
    // from the bits above (Table 6-6 / Chapter 3).
    if (a & 0x10) this._swSelectEnabled = !isD;
    // 8th data bit (Group B 0x20, Table 6-4): ESC Z (open) INCLUDES it, ESC D
    // (close) IGNORES it — the power-on default. Gates the high-bit strip in
    // receiveByte so high-ASCII reaches the custom high set / 8-bit graphics.
    if (b & 0x20) this._includeEighth = !isD;
    // SWA-7 "print commands" (IW-II Table 3-2; DMP SW1-7 "end-of-line"): closed
    // (ESC D, power-on default) LF/FF terminate the line — carriage returns, same
    // effect as ESC l 0. Open (ESC Z) makes CR the only end-of-line, so LF is a
    // pure paper feed. The DMP has no ESC l; its manual spells this ESC Z H R
    // ('H' = A-7 plus unused A-4, 'R' touches only unused Group B bits).
    if (a & 0x40) this._crBeforeLF = isD;
  }

  // Begin collecting an ASCII-decimal parameter of `digits` characters for
  // command `cmd`; _dispatchNum() applies it once the digits are in.
  _beginNum(cmd, digits) {
    this._numCmd  = cmd;
    this._numLeft = digits;
    this._numAcc  = 0;
    this._state   = S_NUM;
  }

  _dispatchNum() {
    const n = this._numAcc;
    switch (this._numCmd) {
      case 0x54: this._lineHeight = (n / 144) * this.dpi; this._state = S_NORMAL; break;  // ESC T — line distance n/144"
      case 0x48: this.paper.setFormDots(Math.round((n / 144) * this.dpi)); this._state = S_NORMAL; break;  // ESC H — page length n/144"
      case 0x4C: // ESC L — left margin at column n (current pitch)
        this._leftMargin = Math.round(n * (this.dpi / CPI[this._pitch]));
        this.head.leftMargin = this._leftMargin;
        if (this._xDot < this._leftMargin) this._xDot = this._leftMargin;
        this._state = S_NORMAL;
        break;
      case 0x46: // ESC F — head to dot column n from the left margin. Dot columns
                 // are counted at the active pitch's graphics density (Table 8-2),
                 // not a fixed unit, so placement tracks the pitch like graphics.
        this._xDot = this._leftMargin + Math.round(n * this._gfxDotW());
        this._state = S_NORMAL;
        break;
      case 0x75: this._setHTabs([n]); this._state = S_NORMAL; break;  // ESC u — add one tab stop at column n
      case 0x52: this._state = S_REPEAT_CHAR; break;  // ESC R — char byte follows
      case 0x56: this._state = S_VREPEAT;     break;  // ESC V — column byte follows
      default:   this._state = S_NORMAL;
    }
  }

  // ---- Horizontal tabs (ESC ( / ) / 0 / u, CTRL-I) ----
  // Stops are stored as a sorted, de-duplicated array of integer COLUMN numbers.
  // A column is one character cell at the active fixed pitch (dpi / CPI), the
  // same unit ESC L uses for the left margin, so tabs track the pitch.

  _setHTabs(cols) {
    const s = new Set(this._htabCols);
    for (const c of cols) if (Number.isFinite(c) && c > 0) s.add(c | 0);
    this._htabCols = Array.from(s).sort((a, b) => a - b);
  }

  _clearHTabs(cols) {
    const drop = new Set(cols.map((c) => c | 0));
    this._htabCols = this._htabCols.filter((c) => !drop.has(c));
  }

  // Dots-per-column at the active pitch (matches ESC L / column-addressed cmds).
  _colDots() { return this.dpi / CPI[this._pitch]; }

  // CTRL-I / HT: advance the head to the first tab stop whose column is strictly
  // greater than the head's current column, measured from the left margin. With
  // no stop beyond the current column (none set, or past the last stop) this is a
  // no-op — the DMP/IW manuals don't define a fall-through default, so the head
  // simply stays put. Never moves left of the left margin.
  _horizontalTab() {
    if (!this._htabCols.length) return;
    const colDots = this._colDots();
    const curCol  = Math.round((this._xDot - this._leftMargin) / colDots);
    const next    = this._htabCols.find((c) => c > curCol);
    if (next === undefined) return;               // past the last stop → no-op
    this._xDot = this._leftMargin + Math.round(next * colDots);
  }

  // CTRL-_ followed by a tab letter (A-F) is an EVFU vertical-tab drop on the
  // Apple DMP — advance the paper to the next line bearing that tab. The base
  // 8510 has no Electronic Vertical Form Unit, so the command is a consumed
  // no-op here; AppleDMP overrides this to walk its per-line tab table.
  _verticalFormTab(_ch) { /* no EVFU in the base 8510 */ }

  // Char-cell advance in internal dots at the current pitch. Double-width
  // (CTRL-N) doubles the cell. Used by glyph emit AND backspace step-back so
  // an overstrike lands exactly back on the previous character.
  _charAdvance() {
    return Math.round(this.dpi / CPI[this._pitch]) * (this._doubleWidth ? 2 : 1);
  }

  // Ink has hit the paper, so a CR that came before it is no longer part of a
  // line ending: a following LF is a real feed. See the CR+LF note above.
  _inked() { this._lastCR = false; }

  _emitChar(code) {
    this._inked();
    // ESC 1-6 (Table 4-6) queued extra dot spaces are inserted before this glyph
    // at the active column density, then cleared (one-shot).
    if (this._pendingDotSpace) {
      this._xDot += this._pendingDotSpace * this._gfxDotW();
      this._pendingDotSpace = 0;
    }

    // Proportional pitch (ESC p/P) advances per-glyph from the corr-prop ROM
    // width instead of a fixed cell, and spaces the dot columns at the
    // proportional density (144/160 dpi). The ROM glyph already carries its own
    // trailing blank column(s) — the built-in 1-dot gap — and ESC s n adds n
    // more dot columns of inter-char space (Table A-11). In NLQ quality the
    // proportional face has its OWN dense bank (18 rows, 160 dpi columns); other
    // qualities use the correspondence proportional widths. A downloaded custom
    // font, or a code with no proportional glyph, falls back to the fixed cell.
    const nlqActive = this._customFont === "none" && this._effectiveQuality() === "nlq";
    let propCols = null, propIsNlq = false;
    if (this._proportional && this._customFont === "none") {
      if (nlqActive) {
        propCols = this.getNLQPropChar(code);
        if (propCols) propIsNlq = true;
        else          propCols = this.getCorrPropChar(code);   // codes the NLQ-prop bank lacks
      } else {
        propCols = this.getCorrPropChar(code);
      }
    }

    const xs = this._doubleWidth ? 2 : 1;
    // A downloaded custom glyph (ESC '/* font active, code defined) advances by
    // its WIDTH CODE — the column count you downloaded, trailing blank columns
    // included (IW-II Tech Ref ch.7: "the width you specify is the escapement").
    // Its columns strike at the pitch's graphics density like ESC G data, NOT the
    // fixed 120-dpi draft cell. An undefined code falls through to the ROM face
    // and keeps the fixed-cell advance below.
    const customActive = this._customFont !== "none" && this.getCustomChar(code) != null;
    let cols, dotW, adv;
    if (propCols) {
      cols = propCols;
      dotW = this._gfxDotW();   // proportional column pitch (144 dpi pica / 160 elite)
      // NLQ-prop columns sit at 160 dpi (vs the 120-dpi corr-prop column cell), so
      // its advance is scaled down to match the tighter column step the renderer
      // draws for an NLQ cell. Corr-prop keeps the 1:1 factor (no change).
      const advScale = propIsNlq ? (120 / 160) : 1;
      adv  = Math.round((cols.length * xs + this._propSpacing) * dotW * advScale);
    } else if (customActive) {
      // Width code = escapement: advance cols.length graphics columns at the pitch
      // density (pica 16-col glyph = 16/80" = 0.2" = two pica cells). dotW stays
      // the 120-raster unit so the head-origin mapping (ESC F, text) is untouched.
      cols = this.getGlyph(code);
      dotW = this._dotW;
      adv  = Math.round(cols.length * xs * this._gfxDotW());
    } else {
      cols = this.getGlyph(code);
      dotW = this._dotW;
      adv  = this._charAdvance();
    }

    // NLQ fixed cell is 16 columns of finer dots in the SAME character cell as
    // draft/corr — 160 dpi horizontal, 144 dpi vertical (2x the 120/72 dpi draft
    // grid). Standard-ASCII/alt-language cells are 18 rows tall (bottom 4 =
    // descenders, Table C-9); MouseText ($C0-$DF) has no descenders and is a
    // 16x16 cell (Table C-10). Tag the payload so the renderer plots the taller,
    // denser grid at half pitch. A code the NLQ ROM lacks falls back to a corr
    // glyph, which keeps the standard 9-row geometry. The NLQ *proportional* face
    // (propIsNlq) prints on the same dense cell: 18 rows, 160x144 dpi, no MouseText.
    const nlqCell = !propCols && this._customFont === "none"
      && this._effectiveQuality() === "nlq" && this.getNLQChar(code) != null;
    const nlqMouse = nlqCell && code >= 0xC0 && code <= 0xDF;
    const nlqDense = nlqCell || propIsNlq;
    const rows     = propIsNlq ? 18 : (nlqCell ? (nlqMouse ? 16 : 18) : 9);
    const hDensity = customActive ? (GFX_DENSITY[this._pitch] ?? 120)
                   : nlqDense ? 160 : 120;
    const vDensity = nlqDense ? 144 : 72;

    // Auto-wrap at the right platen margin: real ImageWriter issues an
    // automatic CR+LF rather than printing past the edge.
    if (this._xDot + adv > this._platenDots) {
      this._xDot = this._leftMargin;
      this._yDot += this._lineHeight;
      this.emit("newline");
    }

    this.emit("printChar", {
      cols,
      xDot:        this._xDot,
      yDot:        this._yDot,
      dotW,
      dotH:        this._dotV,
      rows,                            // dot rows in the cell (9 draft/corr, 18 NLQ)
      hDensity,                        // column dot density (dpi) — sets canvas col pitch
      vDensity,                        // row dot density (dpi) — sets canvas row pitch
      color:       this._inkColor(this._color),
      bold:        this._bold,
      underline:   this._underline,
      halfHeight:  this._halfHeight,
      script:      this._script,       // 'none' | 'super' | 'sub'
      doubleWidth: this._doubleWidth,
    });
    // Plain-text event for text-mode listeners
    this.emit("text", String.fromCharCode(code));
    this._xDot += adv;
    this._lastAdvance = adv;   // remembered so CTRL-H backspace can step this glyph's true width
  }

  // ESC G/S/g column width (internal dots) at the current pitch's graphics density.
  _gfxDotW() { return this.dpi / (GFX_DENSITY[this._pitch] ?? 80); }

  // Returns custom char definition or null
  getCustomChar(code) {
    return this._customChars.get(code) ?? null;
  }

  // Table 4-1: bold/double-width/half-height/proportional do not exist in the
  // draft font. Selecting any forces the correspondence font for as long as it's
  // active; clearing them all reverts to the selected font. (Super/subscript are
  // handled separately in _effectiveQuality because they also override NLQ.)
  _draftIncompatibleActive() {
    return this._bold || this._halfHeight || this._doubleWidth || this._proportional;
  }

  // The font actually used to print, after the draft-incompatibility rule.
  // `_quality` is what the host selected (ESC a/m/M); this is what fires.
  _effectiveQuality() {
    // Super/subscript (Table 4-11) force correspondence from draft OR NLQ.
    if (this._script !== "none" && (this._quality === "draft" || this._quality === "nlq"))
      return "corr";
    if (this._quality === "draft" && this._draftIncompatibleActive()) return "corr";
    return this._quality;
  }

  // Active-font glyph for the current print quality. A downloaded custom glyph
  // (ESC '/* font active) overrides the ROM. Otherwise draft uses the 12-column
  // draft ROM, NLQ the 16-column x 16/18-row NLQ ROM, and correspondence the
  // 8-column corr ROM. NLQ falls back to corr only for codes the NLQ ROM lacks.
  // The variable-width proportional face is selected in _emitChar (it also drives
  // the advance), so this returns the fixed-cell glyph used by the non-prop path.
  getGlyph(code, locale = "US") {
    if (this._customFont !== "none") {
      const custom = this._customGlyph(code);
      if (custom) return custom;
    }
    const q = this._effectiveQuality();
    let cols;
    if      (q === "draft") cols = this.getDraftChar(code, locale);
    else if (q === "nlq")   cols = this.getNLQChar(code, locale) ?? this.getCorrChar(code, locale);
    else                    cols = this.getCorrChar(code, locale);
    // Slashed-zero switch (ESC D/Z, Group B): strike a slash through the '0'
    // glyph. Reuses the base helper; the shared ROM array is never mutated.
    if (this._slashedZero && code === 0x30 && cols) cols = this._slashZeroCols(cols);
    return cols;
  }

  // A downloaded glyph as ROM-format columns (bit 0 = wire 1 … bit 8 = wire 9).
  // Custom column bytes share the graphics mapping (bit 0 = top dot), so they
  // land on bits 0-7 directly; lowercase width codes (wireTop=false) reference
  // wires 2-9, a one-wire downward shift.
  _customGlyph(code) {
    const cc = this._customChars.get(code);
    if (!cc) return null;
    const shift = cc.wireTop ? 0 : 1;
    return Array.from(cc.data, (b) => (b & 0xFF) << shift);
  }

  // Draft ROM hook (9-bit columns). Null in the 8510 base — only the
  // ImageWriter II adds a draft tier; the I and DMP print one corr face.
  getDraftChar(code, locale = "US") {
    return null;
  }

  // NLQ ROM hook (16 columns, up to 18-bit each). Null in the 8510 base —
  // only the ImageWriter II adds an NLQ tier.
  getNLQChar(code, locale = "US") {
    return null;
  }

  // Correspondence ROM column data (8 columns, 9-bit each) — the 8510 base
  // face shared by every model. Returns null for an unauthored code.
  getCorrChar(code, locale = "US") {
    if (locale !== "US") {
      const override = IW2_STANDARD_FIXED_LOCALES[locale]?.[code];
      if (override) return override;
    }
    return IW2_STANDARD_FIXED[code] ?? null;
  }

  // Correspondence *proportional* column data (variable column count, 9-bit
  // each, trailing blank column(s) built in) — the 8510 base proportional
  // face. Drives both the rendered shape and the per-glyph advance.
  getCorrPropChar(code, locale = "US") {
    if (locale !== "US") {
      const override = IW2_STANDARD_PROP_LOCALES[locale]?.[code];
      if (override) return override;
    }
    return IW2_STANDARD_PROP[code] ?? null;
  }

  // NLQ *proportional* ROM hook (variable column count, up to 18-bit each).
  // Null in the 8510 base — only the ImageWriter II adds an NLQ-prop bank.
  getNLQPropChar(code, locale = "US") {
    return null;
  }

  // Print rate follows the effective font (Table 4-1): draft 250, correspondence
  // 180, NLQ 45 cps. Boldface and colour overprint each add a hammer pass, so
  // the carriage runs at half speed while either is active (Appendix D). The
  // head pulls this fresh on every motion (VirtualHead.velocity), so a font /
  // bold / colour change retunes the carriage at the next move with no re-arm.
  getCharsPerSecond() {
    let cps;
    switch (this._effectiveQuality()) {
      case "nlq":  cps = 45;  break;
      case "corr": cps = 180; break;
      default:     cps = 250; break;   // draft (and pre-init undefined)
    }
    if (this._bold || (this._color && this._color !== "black")) cps = Math.round(cps / 2);
    return cps;
  }

  // True only after ESC > ; default (and after ESC < / reset) is bidirectional.
  isUnidirectional() { return this._unidirectional; }
}
