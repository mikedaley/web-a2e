/*
 * slot-storage.test.js - Slot configuration is remembered per machine
 *
 * The thing worth guarding is that two machines cannot share one layout. They
 * do not agree about which slots exist, so a shared configuration puts cards
 * in slots the other machine does not have.
 */

import { afterEach, beforeEach, describe, expect, it } from "vitest";

import {
  defaultSlotConfig,
  loadSlotConfig,
  saveSlotConfig,
  slotConfigOrDefaults,
} from "../../../src/js/machine/slot-storage.js";
import { setMachineProfileForTesting } from "../../../src/js/machine/machine-profile.js";

/** Minimal stand-in for localStorage; these tests run in plain node. */
function fakeStorage(initial = {}) {
  const data = { ...initial };
  return {
    data,
    getItem: (k) => (k in data ? data[k] : null),
    setItem: (k, v) => {
      data[k] = String(v);
    },
    removeItem: (k) => {
      delete data[k];
    },
  };
}

const IIE = Object.freeze({
  key: "apple2e",
  name: "Apple //e Enhanced",
  firstSlot: 1,
  lastSlot: 7,
  slots: [
    { slot: 1, fixedCard: null, defaultCard: null },
    { slot: 2, fixedCard: null, defaultCard: null },
    { slot: 3, fixedCard: "80col", defaultCard: "80col" },
    { slot: 4, fixedCard: null, defaultCard: "mockingboard" },
    { slot: 5, fixedCard: null, defaultCard: "thunderclock" },
    { slot: 6, fixedCard: null, defaultCard: "disk2" },
    { slot: 7, fixedCard: null, defaultCard: "smartport" },
  ],
});

const IIPLUS = Object.freeze({
  key: "apple2plus",
  name: "Apple II Plus",
  firstSlot: 0,
  lastSlot: 7,
  slots: [
    { slot: 0, fixedCard: "languagecard", defaultCard: "languagecard" },
    { slot: 1, fixedCard: null, defaultCard: null },
    { slot: 2, fixedCard: null, defaultCard: null },
    { slot: 3, fixedCard: null, defaultCard: null },
    { slot: 4, fixedCard: null, defaultCard: null },
    { slot: 5, fixedCard: null, defaultCard: null },
    { slot: 6, fixedCard: null, defaultCard: "disk2" },
    { slot: 7, fixedCard: null, defaultCard: null },
  ],
});

beforeEach(() => {
  globalThis.localStorage = fakeStorage();
  setMachineProfileForTesting(IIE);
});

afterEach(() => {
  delete globalThis.localStorage;
  setMachineProfileForTesting(null);
});

describe("a machine's default slots", () => {
  it("come from the machine's own profile", () => {
    expect(defaultSlotConfig(IIE)).toEqual({
      4: "mockingboard",
      5: "thunderclock",
      6: "disk2",
      7: "smartport",
    });

    // A II+ ships with a Disk II and nothing else.
    expect(defaultSlotConfig(IIPLUS)).toEqual({ 6: "disk2" });
  });

  it("leave out slots the user cannot change", () => {
    // The //e's 80-column card and the II+'s language card are fitted, not
    // chosen, so they never appear in a configuration the user can edit.
    expect(defaultSlotConfig(IIE)[3]).toBeUndefined();
    expect(defaultSlotConfig(IIPLUS)[0]).toBeUndefined();
  });
});

describe("remembering slots per machine", () => {
  it("has nothing saved for a machine that was never configured", () => {
    expect(loadSlotConfig(IIE)).toBeNull();
    expect(loadSlotConfig(IIPLUS)).toBeNull();
  });

  it("falls back to the machine's defaults, not the other machine's", () => {
    expect(slotConfigOrDefaults(IIPLUS)).toEqual({ 6: "disk2" });
  });

  it("keeps the two machines' layouts apart", () => {
    saveSlotConfig({ 4: "mouse", 6: "disk2" }, IIE);
    saveSlotConfig({ 2: "ssc", 6: "disk2" }, IIPLUS);

    expect(loadSlotConfig(IIE)).toEqual({ 4: "mouse", 6: "disk2" });
    expect(loadSlotConfig(IIPLUS)).toEqual({ 2: "ssc", 6: "disk2" });
  });

  it("does not let one machine's changes disturb the other", () => {
    saveSlotConfig({ 4: "mockingboard" }, IIE);
    saveSlotConfig({ 4: "empty" }, IIPLUS);
    expect(loadSlotConfig(IIE)).toEqual({ 4: "mockingboard" });
  });

  it("uses the machine in force when none is named", () => {
    setMachineProfileForTesting(IIPLUS);
    saveSlotConfig({ 6: "disk2", 7: "smartport" });
    expect(loadSlotConfig(IIPLUS)).toEqual({ 6: "disk2", 7: "smartport" });
    expect(loadSlotConfig(IIE)).toBeNull();
  });

  it("keeps an emptied machine empty rather than restoring its defaults", () => {
    // "Never configured" and "deliberately stripped" are different states, and
    // a fallback that could not tell them apart would put the cards back.
    saveSlotConfig({}, IIPLUS);
    expect(loadSlotConfig(IIPLUS)).toEqual({});
    expect(slotConfigOrDefaults(IIPLUS)).toEqual({});
  });
});

describe("the configuration saved before machines were a thing", () => {
  it("is adopted by the //e, which is the only machine that wrote it", () => {
    globalThis.localStorage = fakeStorage({
      "a2e-slot-config": JSON.stringify({ 4: "mouse", 6: "disk2" }),
    });

    expect(loadSlotConfig(IIE)).toEqual({ 4: "mouse", 6: "disk2" });
  });

  it("is copied under the //e's own key, so it is read once", () => {
    globalThis.localStorage = fakeStorage({
      "a2e-slot-config": JSON.stringify({ 5: "thunderclock" }),
    });

    loadSlotConfig(IIE);
    expect(
      JSON.parse(localStorage.data["a2e-slot-config:apple2e"]),
    ).toEqual({ 5: "thunderclock" });
  });

  it("is not handed to a machine that cannot have written it", () => {
    globalThis.localStorage = fakeStorage({
      "a2e-slot-config": JSON.stringify({ 4: "mockingboard", 7: "smartport" }),
    });

    // A II+ has no SmartPort card fitted by default and a different slot map;
    // inheriting a //e's layout is exactly the bug this replaced.
    expect(loadSlotConfig(IIPLUS)).toBeNull();
    expect(slotConfigOrDefaults(IIPLUS)).toEqual({ 6: "disk2" });
  });

  it("is not consulted once the machine has a layout of its own", () => {
    globalThis.localStorage = fakeStorage({
      "a2e-slot-config": JSON.stringify({ 4: "mouse" }),
      "a2e-slot-config:apple2e": JSON.stringify({ 4: "mockingboard" }),
    });

    expect(loadSlotConfig(IIE)).toEqual({ 4: "mockingboard" });
  });
});

describe("when storage is unavailable", () => {
  it("reports nothing saved rather than throwing", () => {
    globalThis.localStorage = {
      getItem() {
        throw new Error("site data blocked");
      },
      setItem() {
        throw new Error("site data blocked");
      },
    };

    expect(loadSlotConfig(IIE)).toBeNull();
    expect(() => saveSlotConfig({ 6: "disk2" }, IIE)).not.toThrow();
    // And a machine still comes up with its own defaults.
    expect(slotConfigOrDefaults(IIPLUS)).toEqual({ 6: "disk2" });
  });
});
