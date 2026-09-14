# Printers

The emulator includes four virtual dot-matrix printers, rendered onto simulated fanfold paper. Anything an Apple II could print -- a BASIC listing, a ProDOS catalog, an AppleWorks document, banner software -- produces a page you can read, save or print for real.

Open **View > Printer...** for the printer and its paper, and **View > Print Browser...** to review past output.

---

## Table of Contents

- [Supported Printers](#supported-printers)
- [Connecting a Printer](#connecting-a-printer)
- [Printing from the Apple](#printing-from-the-apple)
- [The Print Browser](#the-print-browser)
- [Editing the Printer Fonts](#editing-the-printer-fonts)
- [How It Works](#how-it-works)

---

## Supported Printers

| Printer | Interface | Notes |
|---------|-----------|-------|
| **Epson FX-80** | Parallel Card | The de-facto standard dot-matrix printer; ESC/P command set |
| **Apple DMP** | Parallel Card | Apple's Dot Matrix Printer |
| **ImageWriter I** | Serial (SSC, or a //c's printer port) | Apple's serial dot-matrix printer |
| **ImageWriter II** | Serial (SSC, or a //c's printer port) | Adds draft, standard and NLQ print qualities |

Each printer emulates its own character ROM, so the glyph shapes, character spacing and print quality modes are those of the machine being imitated rather than a generic font. The ImageWriter II, for instance, carries separate ROMs for draft, standard and near-letter-quality output in both fixed and proportional spacing.

## Connecting a Printer

A printer needs the right interface card installed:

- **Epson FX-80** and **Apple DMP** need a **Parallel Card** in slot 1 or 2.
- **ImageWriter I** and **ImageWriter II** need a serial port: a **Super Serial Card** in slot 1 or 2, or — on a //c or a IIgs — the printer port, which is already there and cannot be removed.

Install the card from **View > Expansion Slots** (see [[Expansion-Slots]]), then choose the printer model in the Printer window.

Printer sounds can be toggled independently; they are wired to the same main volume control as the speaker and drives.

## Printing from the Apple

Printing works exactly as on real hardware -- redirect output to the slot holding the interface card:

```
PR#1            REM send output to slot 1
CATALOG         REM this now goes to the printer
PR#0            REM back to the screen
```

From Applesoft you can also `PRINT CHR$(4);"PR#1"` inside a program. Software with its own printer setup (AppleWorks, Print Shop) should be pointed at whichever slot holds the card.

## The Print Browser

The Print Browser collects completed pages so you can go back through a session's output. Pages can be saved as images, and the paper rendering is what gets exported -- what you see is what you get.

It is also where the earlier pages of a long print live. A browser will not host
a canvas long enough for a whole multi-page job, so the paper on screen is a
window a few pages deep and it scrolls as printing goes on; each page is written
to the Print Browser as it leaves the top. Nothing is lost -- the PDF button and
the multi-page PNG export both cover the whole job, not just the pages still on
the paper.

## Editing the Printer Fonts

The glyph banks the models render from can be authored in a standalone editor at **`/printers/rom-editor.html`**.

- Draws characters dot by dot on the model's own glyph grid.
- Handles the alternate-language code points each printer swapped in per locale.
- Imports and exports either as a ROM module or as ASCII dot art.
- Traces over a scan of a manual's character chart, so a font can be rebuilt from the page it was printed on.

It is one plain page with no build step and no imports, so it opens straight off disk as readily as from the emulator. The printer ROM modules in the source tree remain the authority — the editor's built-in defaults are generated from them, and a check in `npm run check` fails if the two drift apart.

## How It Works

The interface card receives bytes from the Apple exactly as the real hardware would: the Parallel Card takes Centronics-style strobed bytes, and the Super Serial Card runs them through an emulated **ACIA 6551**, so baud rate and framing behave as they should.

Those bytes reach a printer emulation in `src/js/printer/`, which interprets the control codes of the selected model -- ESC/P for the Epson, Apple's own escape sequences for the DMP and ImageWriters -- and rasterises the result through the printer's character ROM onto the paper canvas.

Because the printer is driven by the byte stream rather than by intercepting BASIC, anything that talks to the card prints correctly, including software that does its own graphics by sending column-addressed bit patterns.

### Graphics bands, and the Automatic Line Feed switch

A GS/OS print is entirely graphics. The ImageWriter driver rasterises the page
and sends it as 8-dot bands, writing `CR`, `ESC T 16`, `LF` before each one:
16/144 of an inch is exactly eight dots at the head's 1/72" pitch, so the bands
abut and the page comes out solid.

The **Automatic Line Feed** switch interacts with that. It is on by default,
because plain Apple II text printing needs it — Applesoft sends a bare `CR` and
expects the paper to move — and the emulation treats a `CR`+`LF` pair as one
line ending so text that sends both is not double spaced. What the driver puts
between its `CR` and its `LF` is an escape that sets the distance for that very
feed, so the pairing has to survive a control sequence that prints nothing.
Before it did, every band fed twice and each printed line came out sliced in
half by a 1/8" white stripe. Anything that lays ink down still ends the pairing,
so `CR`, a character, `LF` feeds twice as it should.

On a real ImageWriter the same stream behaves the same way, which is why Apple's
instructions for GS/OS say to set the Automatic Line Feed DIP (SW2-1) **off**.
Turning the switch off in the Printer window is still the exact setting: it also
gives the driver's top margin back, which the pairing shortens by one feed.

The printer emulation is covered by characterization tests in `tests/js/`, which capture the event stream from `PrinterBase.setEventSink()` -- so a change in how a control code is interpreted shows up as a test diff rather than as a subtly wrong page.

---

## See Also

- [[Expansion-Slots]] -- installing the Parallel or Super Serial Card
- [[Architecture-Overview]] -- where printer emulation sits in the JavaScript layer
