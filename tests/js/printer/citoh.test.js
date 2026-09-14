/*
 * citoh.test.js - Golden tests for the C.Itoh-family printers
 *                 (Apple DMP, ImageWriter I, ImageWriter II)
 *
 * All three share CItohPrinter's command parser and differ in ROM font, ribbon
 * support and default metrics, so the suite runs the shared behaviour across
 * every model and then pins each model's own characteristics.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { describe, it, expect } from "vitest";
import { AppleDMP } from "../../../src/js/printer/apple-dmp.js";
import { ImageWriterI } from "../../../src/js/printer/imagewriter-i.js";
import { ImageWriterII } from "../../../src/js/printer/imagewriter-ii.js";
import { bytes, capture, summarise, CR, LF, FF, ESC } from "./harness.js";
import { scrollPagesNeeded } from "../../../src/js/printer/printer-window.js";

const MODELS = [
  ["AppleDMP", () => new AppleDMP()],
  ["ImageWriterI", () => new ImageWriterI()],
  ["ImageWriterII", () => new ImageWriterII()],
];

function run(make, data) {
  return summarise(capture(make(), bytes(...data)));
}

describe.each(MODELS)("%s — shared C.Itoh behaviour", (name, make) => {
  it("prints a plain line terminated by CR LF", () => {
    expect(run(make, ["HELLO", CR, LF])).toMatchSnapshot();
  });

  it("strips the Apple II high bit from character codes", () => {
    expect(run(make, [0xc8, 0xc9, CR, LF])).toEqual(run(make, ["HI", CR, LF]));
  });

  it("ESC ! turns bold on and ESC \" turns it off", () => {
    expect(run(make, [ESC, "!", "A", ESC, '"', "B", CR, LF])).toMatchSnapshot();
  });

  it("ESC X starts underline and ESC Y stops it", () => {
    expect(run(make, [ESC, "X", "A", ESC, "Y", "B", CR, LF])).toMatchSnapshot();
  });

  it("ESC x / ESC y / ESC z select superscript, subscript and normal", () => {
    expect(run(make, [ESC, "x", "A", ESC, "y", "B", ESC, "z", "C", CR, LF]))
      .toMatchSnapshot();
  });

  it("pitch commands change the per-character advance", () => {
    // ESC N pica (10 cpi) must advance further than ESC q condensed (15 cpi).
    const advance = (data) => {
      const cols = capture(make(), bytes(...data))
        .filter((e) => e.name === "printChar")
        .map((e) => e.data.xDot);
      return cols[1] - cols[0];
    };
    expect(advance([ESC, "N", "AB", CR, LF]))
      .toBeGreaterThan(advance([ESC, "q", "AB", CR, LF]));
  });

  it("ESC A and ESC B select 6 and 8 lines per inch", () => {
    expect(run(make, [ESC, "A", "A", CR, LF, ESC, "B", "B", CR, LF]))
      .toMatchSnapshot();
  });

  it("ESC > selects unidirectional printing, which slews the head home", () => {
    // Bidirectional (the default) flips travel instead of returning, so the
    // feed sound differs — 'return' vs 'line'.
    const sounds = (data) =>
      capture(make(), bytes(...data))
        .filter((e) => e.name === "feed")
        .map((e) => e.data.sound);
    expect(sounds([ESC, ">", "AB", CR, LF])).toMatchSnapshot();
    expect(sounds([ESC, "<", "AB", CR, LF])).toMatchSnapshot();
  });

  it("form feed ejects the page", () => {
    expect(run(make, ["A", CR, LF, FF])).toMatchSnapshot();
  });

  it("ESC M selects the NLQ font, changing the cell row count", () => {
    // NLQ cells are 18 rows against 9 for draft — a reimplementation that keeps
    // one cell height would pass the text tests and fail here.
    const rows = (data) =>
      capture(make(), bytes(...data))
        .filter((e) => e.name === "printChar")
        .map((e) => e.data.rows);
    expect(rows([ESC, "M", "A", CR, LF])).toMatchSnapshot();
  });
});

describe("model identity and metrics", () => {
  it.each(MODELS)("%s reports stable name, id and dpi", (name, make) => {
    const p = make();
    expect({
      name: p.getName(),
      id: p.getId(),
      dpi: p.dpi,
      cps: p.getCharsPerSecond(),
      colorRibbon: p.supportsColorRibbon(),
    }).toMatchSnapshot();
  });
});

describe("ImageWriter II — colour ribbon", () => {
  it("supports a colour ribbon where the earlier models do not", () => {
    expect(new ImageWriterII().supportsColorRibbon()).toBe(true);
    expect(new ImageWriterI().supportsColorRibbon()).toBe(false);
    expect(new AppleDMP().supportsColorRibbon()).toBe(false);
  });

  it("ESC K n selects the ribbon colour band when a colour cart is loaded", () => {
    // ESC K n takes ASCII '0'-'6'. The ribbon is the physical gate: with the
    // default b/w cart every band maps to black, so the cart must be swapped
    // first or this asserts nothing.
    const colorOf = (ribbon, n) => {
      const p = new ImageWriterII();
      p.setRibbon(ribbon);
      return capture(p, bytes(ESC, "K", n, "A", CR, LF))
        .filter((e) => e.name === "printChar")
        .map((e) => e.data.color);
    };

    // Each selectable band must resolve to a distinct ink on a colour cart.
    const bands = ["0", "1", "2", "3", "4", "5", "6"].map((n) => colorOf("color", n));
    expect(bands.flat()).toMatchSnapshot();

    // A black cart overrides the selection — colour data still prints black.
    expect(colorOf("bw", "1")).toEqual(["black"]);
    expect(colorOf("color", "1")).not.toEqual(["black"]);
  });
});

describe("graphics bands from the GS/OS ImageWriter driver", () => {
  // The driver rasterises a page into 8-dot bands and writes, per band:
  //
  //     CR, ESC T 16, LF, (ESC F <col>, ESC G <n> <data>)...
  //
  // 16/144" is exactly eight dots at the head's 1/72" pitch, so the bands abut
  // and the page comes out solid. The escape between the CR and the LF is what
  // makes this worth a test: with the Automatic Line Feed switch on — the
  // default, and what plain Apple II text needs — the CR feeds a line, and if
  // that also broke the CR+LF pairing the LF fed a second one. Every line of a
  // real GS/OS print came out sliced in half by a 1/8" white stripe.
  //
  // Measured against the real thing: the byte stream this uses was captured
  // from System 6.0.4 printing a document through ImageWriter/Printer v4.2.
  const band = (col) => [CR, ESC, "T16", LF, ESC, "F", " 282", ESC, "G", "0002", col, col];

  const bandTops = (autoLF) => {
    const printer = new ImageWriterII();
    printer.setAutoLineFeed(autoLF);
    const tops = [];
    printer.setEventSink((e) => {
      if (e.name === "printDots" && tops.at(-1) !== e.data.yDot) tops.push(e.data.yDot);
    });
    for (const byte of bytes(band(0xff), band(0xff), band(0xff))) {
      printer.receiveByte(byte);
    }
    printer.flushLine();
    return tops;
  };

  const bandHeight = 8 * (new ImageWriterII().dpi / 72); // eight dots at 1/72"

  it("steps exactly one band per line with automatic line feed on", () => {
    const tops = bandTops(true);
    expect(tops).toHaveLength(3);
    expect(tops[1] - tops[0]).toBeCloseTo(bandHeight, 4);
    expect(tops[2] - tops[1]).toBeCloseTo(bandHeight, 4);
  });

  it("steps exactly one band per line with automatic line feed off", () => {
    const tops = bandTops(false);
    expect(tops).toHaveLength(3);
    expect(tops[1] - tops[0]).toBeCloseTo(bandHeight, 4);
    expect(tops[2] - tops[1]).toBeCloseTo(bandHeight, 4);
  });

  it("still feeds when ink lands between the CR and the LF", () => {
    // Only a non-printing escape keeps the pairing armed. A CR, then something
    // printed, then an LF is two line endings and has to feed.
    const printer = new ImageWriterII();
    printer.setAutoLineFeed(true);
    let feeds = 0;
    printer.on("newline", () => feeds++);
    printer.on("linefeed", () => feeds++);
    for (const byte of bytes(CR, "A", LF)) printer.receiveByte(byte);
    expect(feeds).toBe(2);
  });
});

describe("the live paper window", () => {
  // A canvas cannot hold an arbitrarily long print, so the paper scrolls through
  // a window a few pages wide and what leaves is kept in the page store. This is
  // the arithmetic that decides when to scroll and by how much; the rest of the
  // path needs a canvas, and is checked in a browser.
  const PAGE = 1320; // logical px for an 11" form at 120 px/inch

  it("does not scroll while the ink is inside the window", () => {
    expect(scrollPagesNeeded(0, 3, PAGE)).toBe(0);
    expect(scrollPagesNeeded(PAGE * 2 + 10, 3, PAGE)).toBe(0);
  });

  it("scrolls by whole pages once the ink would land past the window", () => {
    expect(scrollPagesNeeded(PAGE * 3, 3, PAGE)).toBe(1);
    expect(scrollPagesNeeded(PAGE * 4 + 5, 3, PAGE)).toBe(2);
    // A one-page window is the constrained case: every new page scrolls.
    expect(scrollPagesNeeded(PAGE, 1, PAGE)).toBe(1);
    expect(scrollPagesNeeded(PAGE * 9, 1, PAGE)).toBe(9);
  });

  it("answers zero rather than NaN for nonsense", () => {
    expect(scrollPagesNeeded(NaN, 3, PAGE)).toBe(0);
    expect(scrollPagesNeeded(Infinity, 3, PAGE)).toBe(0);
    expect(scrollPagesNeeded(PAGE * 5, 3, 0)).toBe(0);
    expect(scrollPagesNeeded(-100, 3, PAGE)).toBe(0);
  });
});
