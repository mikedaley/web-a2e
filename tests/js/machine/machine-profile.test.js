/*
 * machine-profile.test.js - The host's view of the machine
 *
 * The point of this module is that the host stops knowing 560x384. These tests
 * cover the two things that could quietly undo that: a fetch failure leaving
 * callers with nothing, and a fetched profile not actually reaching them.
 */

import { describe, it, expect, beforeEach, vi } from "vitest";
import {
  APPLE_IIE_FALLBACK,
  getMachineProfile,
  listMachineProfiles,
  loadMachineProfile,
  machineDisplay,
  machineTiming,
  setMachineProfileForTesting,
} from "../../../src/js/machine/machine-profile.js";

// A machine that is deliberately not a //e, so a test cannot pass by accident
// on a value that happens to match the fallback.
const OTHER_MACHINE = {
  id: 1,
  key: "test-machine",
  name: "Test Machine",
  shortName: "test",
  cpu: "65C02",
  timing: { cpuClockHz: 2800000, cyclesPerScanline: 65, cyclesPerFrame: 17030 },
  memory: { mainRamSize: 131072 },
  display: { width: 640, height: 400, framebufferSize: 640 * 400 * 4 },
  caps: { inhibitsBurstInText: false },
  slots: [],
};

function fakeWasm(profile) {
  return {
    callString: vi.fn(async () => JSON.stringify(profile)),
    _getMachineCount: vi.fn(async () => 1),
  };
}

describe("machine profile", () => {
  beforeEach(() => {
    setMachineProfileForTesting(null);
  });

  it("describes a //e before the core has been asked", () => {
    // The host builds a renderer and a selection overlay before it can ask, so
    // there has to be a correct answer available immediately.
    expect(getMachineProfile()).toBe(APPLE_IIE_FALLBACK);
    expect(machineDisplay().width).toBe(560);
    expect(machineDisplay().height).toBe(384);
    expect(machineDisplay().framebufferSize).toBe(560 * 384 * 4);
    expect(machineTiming().cyclesPerFrame).toBe(17030);
  });

  it("adopts the profile the core reports", async () => {
    const wasm = fakeWasm(OTHER_MACHINE);
    const loaded = await loadMachineProfile(wasm);

    expect(wasm.callString).toHaveBeenCalledWith("_getMachineProfileJSON");
    expect(loaded.key).toBe("test-machine");

    // And the accessors the rest of the host uses follow it.
    expect(machineDisplay().width).toBe(640);
    expect(machineDisplay().height).toBe(400);
    expect(machineTiming().cpuClockHz).toBe(2800000);
  });

  it("fetches the whole profile in one round trip", async () => {
    // The Worker services RPCs on the thread that runs the emulation, so a
    // field-at-a-time fetch would steal emulation time for no reason.
    const wasm = fakeWasm(OTHER_MACHINE);
    await loadMachineProfile(wasm);
    expect(wasm.callString).toHaveBeenCalledTimes(1);
  });

  it("keeps the last good profile when the core cannot be reached", async () => {
    const failing = {
      callString: vi.fn(async () => {
        throw new Error("worker gone");
      }),
    };

    const result = await loadMachineProfile(failing);

    // A host that could not ask still draws a correct picture rather than
    // being left with no dimensions at all.
    expect(result).toBe(APPLE_IIE_FALLBACK);
    expect(machineDisplay().width).toBe(560);
  });

  it("rejects a malformed profile rather than adopting it", async () => {
    for (const bad of ["", "null", "{}", '{"display":{}}']) {
      setMachineProfileForTesting(null);
      const wasm = { callString: vi.fn(async () => bad) };
      const result = await loadMachineProfile(wasm);

      // A profile with no picture size would leave every canvas at zero.
      expect(result).toBe(APPLE_IIE_FALLBACK);
      expect(machineDisplay().width).toBe(560);
    }
  });

  it("survives invalid JSON from the core", async () => {
    const wasm = { callString: vi.fn(async () => "{not json") };
    const result = await loadMachineProfile(wasm);
    expect(result).toBe(APPLE_IIE_FALLBACK);
  });

  it("lists the machines the core can run", async () => {
    const wasm = fakeWasm(OTHER_MACHINE);
    const all = await listMachineProfiles(wasm);

    expect(all).toHaveLength(1);
    expect(all[0].key).toBe("test-machine");
    expect(wasm.callString).toHaveBeenCalledWith("_getMachineProfileJSONAt", 0);
  });

  it("falls back to the current machine when listing fails", async () => {
    const failing = {
      _getMachineCount: vi.fn(async () => {
        throw new Error("worker gone");
      }),
    };
    const all = await listMachineProfiles(failing);
    expect(all).toEqual([APPLE_IIE_FALLBACK]);
  });

  it("hands out a frozen profile so a caller cannot edit the machine", async () => {
    await loadMachineProfile(fakeWasm(OTHER_MACHINE));
    const profile = getMachineProfile();
    expect(Object.isFrozen(profile)).toBe(true);
  });
});
