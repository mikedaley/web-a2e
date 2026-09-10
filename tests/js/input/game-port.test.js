import { afterEach, beforeEach, describe, expect, it } from "vitest";

import {
  DEFAULT_GAME_PORT,
  DIGITAL_THRESHOLD,
  GAME_PORT_APPLE,
  GAME_PORT_CODE,
  GAME_PORT_JOYPORT,
  SWITCH,
  describeSwitches,
  isGamePortDevice,
  loadStoredGamePort,
  storeGamePort,
  switchesFromAxes,
  switchesFromDirections,
} from "../../../src/js/input/game-port.js";

/** Minimal stand-in for localStorage. */
function fakeStorage(initial = {}) {
  const data = { ...initial };
  return {
    data,
    getItem: (k) => (k in data ? data[k] : null),
    setItem: (k, v) => {
      data[k] = String(v);
    },
  };
}

beforeEach(() => {
  globalThis.localStorage = fakeStorage();
});

afterEach(() => {
  delete globalThis.localStorage;
});

describe("device selection", () => {
  it("defaults to the Apple joystick", () => {
    expect(DEFAULT_GAME_PORT).toBe(GAME_PORT_APPLE);
    expect(loadStoredGamePort()).toBe(GAME_PORT_APPLE);
  });

  it("round-trips a stored choice", () => {
    storeGamePort(GAME_PORT_JOYPORT);
    expect(loadStoredGamePort()).toBe(GAME_PORT_JOYPORT);
  });

  it("falls back rather than trusting edited storage", () => {
    globalThis.localStorage = fakeStorage({ "a2e-game-port": "trackball" });
    expect(loadStoredGamePort()).toBe(GAME_PORT_APPLE);
    expect(isGamePortDevice("trackball")).toBe(false);
  });

  it("survives a storage that throws", () => {
    globalThis.localStorage = {
      getItem() {
        throw new Error("denied");
      },
      setItem() {
        throw new Error("denied");
      },
    };
    expect(loadStoredGamePort()).toBe(GAME_PORT_APPLE);
    expect(() => storeGamePort(GAME_PORT_JOYPORT)).not.toThrow();
  });

  it("maps each device to the code the core expects", () => {
    expect(GAME_PORT_CODE[GAME_PORT_APPLE]).toBe(0);
    expect(GAME_PORT_CODE[GAME_PORT_JOYPORT]).toBe(1);
  });
});

describe("switch masks", () => {
  it("sets one bit per closed switch", () => {
    expect(switchesFromDirections({ up: true, fire: true })).toBe(
      SWITCH.UP | SWITCH.FIRE,
    );
  });

  it("allows diagonals", () => {
    expect(switchesFromDirections({ up: true, right: true })).toBe(
      SWITCH.UP | SWITCH.RIGHT,
    );
  });

  it("drops an opposing pair the gate could never close", () => {
    expect(switchesFromDirections({ left: true, right: true })).toBe(0);
    expect(switchesFromDirections({ up: true, down: true, fire: true })).toBe(
      SWITCH.FIRE,
    );
  });

  it("reports nothing held for an empty stick", () => {
    expect(switchesFromDirections()).toBe(0);
  });
});

describe("analog to digital", () => {
  it("stays centred inside the threshold", () => {
    expect(switchesFromAxes(DIGITAL_THRESHOLD - 0.01, 0)).toBe(0);
  });

  it("closes a switch at the threshold", () => {
    expect(switchesFromAxes(DIGITAL_THRESHOLD, 0)).toBe(SWITCH.RIGHT);
    expect(switchesFromAxes(-DIGITAL_THRESHOLD, 0)).toBe(SWITCH.LEFT);
  });

  it("treats negative Y as up, matching the Gamepad API", () => {
    expect(switchesFromAxes(0, -1)).toBe(SWITCH.UP);
    expect(switchesFromAxes(0, 1)).toBe(SWITCH.DOWN);
  });

  it("carries the fire button through", () => {
    expect(switchesFromAxes(-1, -1, true)).toBe(
      SWITCH.LEFT | SWITCH.UP | SWITCH.FIRE,
    );
  });
});

describe("describeSwitches", () => {
  it("names an idle stick", () => {
    expect(describeSwitches(0)).toBe("Centred");
  });

  it("lists what is held", () => {
    expect(describeSwitches(SWITCH.UP | SWITCH.RIGHT | SWITCH.FIRE)).toBe(
      "Up + Right + Fire",
    );
  });
});
