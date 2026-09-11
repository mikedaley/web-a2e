/*
 * printer-buses.test.js - Which machines can reach a printer, and over what
 *
 * The printer is one device on the end of one of two buses: a Centronics
 * parallel card, or a serial line. What provides the serial line differs by
 * machine — a //e takes a Super Serial Card, a //c has the port soldered on —
 * and the manager has to recognise both or the printer silently eats bytes.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { describe, it, expect } from "vitest";
import { PrinterManager } from "../../../src/js/printer/printer-manager.js";

// The manager reaches the core through a proxy and the speakers through an
// audio context; neither is touched by the slot logic under test.
const makeManager = () => new PrinterManager(null, null);

describe("printer bus availability", () => {
  it("finds no interface in a machine with neither card", () => {
    const printer = makeManager();
    printer.updateSlots({ 6: "disk2" });

    expect(printer.hasInterface()).toBe(false);
    expect(printer.availableModelIds().size).toBe(0);
  });

  it("drives the ImageWriters over a Super Serial Card", () => {
    const printer = makeManager();
    printer.updateSlots({ 2: "ssc", 6: "disk2" });

    expect(printer.hasInterface()).toBe(true);
    expect([...printer.availableModelIds()].sort()).toEqual([
      "imagewriter-i",
      "imagewriter-ii",
    ]);
  });

  it("drives them over a //c's built-in printer port too", () => {
    // Slot 1 on a //c is the printer port: the same 6551 an SSC carries,
    // soldered to the board, with the same ImageWriter on the end of it.
    const printer = makeManager();
    printer.updateSlots({ 1: "serial1", 2: "serial2", 6: "iwm" });

    expect(printer.hasInterface()).toBe(true);
    expect([...printer.availableModelIds()].sort()).toEqual([
      "imagewriter-i",
      "imagewriter-ii",
    ]);
  });

  it("does not offer the modem port as a printer bus", () => {
    // Port 2 is for a modem. A //c that somehow had only that port has nowhere
    // to print, and saying otherwise would gate bytes into nothing.
    const printer = makeManager();
    printer.updateSlots({ 2: "serial2" });

    expect(printer.hasInterface()).toBe(false);
  });

  it("drives the Epsons over the parallel card, alongside a serial port", () => {
    const printer = makeManager();
    printer.updateSlots({ 1: "parallel", 2: "ssc" });

    expect([...printer.availableModelIds()].sort()).toEqual([
      "apple-dmp",
      "epson-fx80",
      "imagewriter-i",
      "imagewriter-ii",
    ]);
  });
});
