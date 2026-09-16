import { describe, expect, it } from "vitest";
import { menuAvailability } from "../../../src/js/ui/machine-availability.js";

const iie = { family: "apple2", caps: { hasExpansionSlots: true } };
const iic = { family: "apple2", caps: { hasExpansionSlots: false } };
const iigs = { family: "apple2gs", caps: { hasExpansionSlots: true } };

describe("menuAvailability", () => {
  it("offers a //e what its cards provide", () => {
    const a = menuAvailability(iie, {
      3: "80col",
      4: "mockingboard",
      6: "disk2",
    });
    expect(a.slots).toBe(true);
    expect(a.speed).toBe(true);
    expect(a.mockingboard).toBe(true);
    expect(a.hardDrives).toBe(false);
    expect(a.serialPort).toBe(false);
    expect(a.printer).toBe(false);
    expect(a.mouseCard).toBe(false);
  });

  it("follows the cards as they change", () => {
    const a = menuAvailability(iie, {
      1: "parallel",
      2: "ssc",
      4: "mouse",
      7: "smartport",
    });
    expect(a.printer).toBe(true);
    expect(a.serialPort).toBe(true);
    expect(a.mouseCard).toBe(true);
    expect(a.hardDrives).toBe(true);
    expect(a.mockingboard).toBe(false);
  });

  it("gives a //c its ports but no slots, no card windows", () => {
    const a = menuAvailability(iic, {
      1: "serial1",
      2: "serial2",
      3: "80col",
      4: "mouse",
      6: "iwm",
    });
    expect(a.slots).toBe(false);
    expect(a.serialPort).toBe(true);
    expect(a.printer).toBe(true);
    expect(a.mouseCard).toBe(false);
    expect(a.mockingboard).toBe(false);
    expect(a.hardDrives).toBe(false);
  });

  it("gives a IIgs its built-in SmartPort and ports, a slots window, no speed", () => {
    const a = menuAvailability(iigs, { 1: "serial1", 2: "serial2" });
    // A IIgs has seven real sockets, and each one also has a built-in device
    // assigned to it; the window offers both the socket and the Control
    // Panel's setting that says which of the two answers.
    expect(a.slots).toBe(true);
    expect(a.speed).toBe(false);
    expect(a.hardDrives).toBe(true);
    expect(a.serialPort).toBe(true);
    expect(a.printer).toBe(true);
    expect(a.mockingboard).toBe(false);
    expect(a.basic).toBe(false);
    expect(a.assembler).toBe(false);
  });

  it("offers the //e its BASIC and assembler tools", () => {
    const a = menuAvailability(iie, {});
    expect(a.basic).toBe(true);
    expect(a.assembler).toBe(true);
  });

  it("copes with no profile and no cards", () => {
    const a = menuAvailability(null, null);
    expect(a.slots).toBe(true);
    expect(a.printer).toBe(false);
  });
});
