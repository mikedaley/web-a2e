/*
 * machine-profile.test.js - The host's view of the machine
 *
 * The point of this module is that the host stops knowing 560x384. These tests
 * cover the two things that could quietly undo that: a fetch failure leaving
 * callers with nothing, and a fetched profile not actually reaching them.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import {
  APPLE_IIE_FALLBACK,
  getMachineProfile,
  hasSystemRom,
  listMachineProfiles,
  loadMachineProfile,
  loadRememberedMachine,
  rememberMachine,
  restoreRememberedMachine,
  machineDisplay,
  machineTiming,
  setMachineProfileForTesting,
  switchMachine,
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

// A core that behaves like the real one: exports taking a `const char *` see a
// pointer, never a JavaScript string. Recording what was written to the heap is
// how these tests catch a key that was never marshalled — passing the string
// straight through does not throw, it silently looks up the wrong thing.
/** Minimal stand-in for localStorage; these tests run in plain node. */
function fakeStorage(initial = {}) {
  const data = { ...initial };
  return {
    data,
    getItem: (k) => (k in data ? data[k] : null),
    setItem: (k, v) => {
      data[k] = String(v);
    },
    clear: () => {
      for (const k of Object.keys(data)) delete data[k];
    },
  };
}

beforeEach(() => {
  globalThis.localStorage = fakeStorage();
});

afterEach(() => {
  delete globalThis.localStorage;
});

function fakeWasm(profile) {
  const heap = new Map();
  let nextPtr = 0x1000;

  return {
    heap,
    callString: vi.fn(async () => JSON.stringify(profile)),
    _getMachineCount: vi.fn(async () => 1),
    _malloc: vi.fn(async (size) => {
      const ptr = nextPtr;
      nextPtr += size;
      return ptr;
    }),
    _free: vi.fn(),
    stringToUTF8: vi.fn(async (text, ptr) => heap.set(ptr, text)),
    _isMachineRunnable: vi.fn(async (ptr) => (heap.has(ptr) ? 1 : 0)),
    _hasSystemROM: vi.fn(async () => 1),
    _setMachine: vi.fn(async (ptr) => (heap.has(ptr) ? 1 : 0)),
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

  it("lists the machines the core knows about", async () => {
    const wasm = fakeWasm(OTHER_MACHINE);
    const all = await listMachineProfiles(wasm);

    expect(all).toHaveLength(1);
    expect(all[0].key).toBe("test-machine");
    expect(wasm.callString).toHaveBeenCalledWith("_getMachineProfileJSONAt", 0);
  });

  it("marks a listed machine that has no ROM as not runnable", async () => {
    // A machine can be fully described and still be unable to start: the II+
    // ROM set is optional at build time. A chooser that ignored this would
    // offer a machine that never reaches a prompt.
    const wasm = fakeWasm(OTHER_MACHINE);
    wasm._isMachineRunnable = vi.fn(async () => 0);

    const all = await listMachineProfiles(wasm);
    expect(all[0].runnable).toBe(false);
  });

  it("passes a machine key as a pointer, not a JavaScript string", async () => {
    // A string handed straight to a WASM export is coerced to a number and
    // read as an address, so the call succeeds against the wrong memory. Both
    // the runnable check and the switch have to copy the key into the heap.
    const wasm = fakeWasm(OTHER_MACHINE);
    await listMachineProfiles(wasm);
    await switchMachine(wasm, "test-machine");

    for (const call of [
      ...wasm._isMachineRunnable.mock.calls,
      ...wasm._setMachine.mock.calls,
    ]) {
      expect(typeof call[0]).toBe("number");
      expect(wasm.heap.get(call[0])).toBe("test-machine");
    }

    // And the pointer is released each time.
    expect(wasm._free).toHaveBeenCalledTimes(2);
  });

  it("adopts the new machine after a switch", async () => {
    const wasm = fakeWasm(OTHER_MACHINE);
    const result = await switchMachine(wasm, "test-machine");

    const ptr = wasm._setMachine.mock.calls[0][0];
    expect(wasm.heap.get(ptr)).toBe("test-machine");
    expect(result.key).toBe("test-machine");
    // The switch must re-read the profile, not assume it worked.
    expect(machineDisplay().width).toBe(640);
  });

  it("reports a refused switch rather than pretending it happened", async () => {
    const wasm = fakeWasm(OTHER_MACHINE);
    wasm._setMachine = vi.fn(async () => 0);

    expect(await switchMachine(wasm, "nonexistent")).toBeNull();
    // And the machine in force is unchanged.
    expect(machineDisplay().width).toBe(560);
  });

  it("reports no ROM when the core says the machine cannot start", async () => {
    const wasm = fakeWasm(OTHER_MACHINE);
    expect(await hasSystemRom(wasm)).toBe(true);

    wasm._hasSystemROM = vi.fn(async () => 0);
    expect(await hasSystemRom(wasm)).toBe(false);

    // A worker that has gone away is not a running machine either.
    expect(await hasSystemRom({})).toBe(false);
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

describe("remembering the machine between sessions", () => {
  beforeEach(() => {
    setMachineProfileForTesting(null);
  });

  it("remembers nothing until a machine is chosen", () => {
    expect(loadRememberedMachine()).toBeNull();
  });

  it("stores the machine by key, not by id", () => {
    // Ids are an implementation detail of the core's registry and could be
    // renumbered; a key names a machine for good.
    rememberMachine("apple2plus");
    expect(loadRememberedMachine()).toBe("apple2plus");
    expect(localStorage.getItem("a2e-machine")).toBe("apple2plus");
  });

  it("records the machine whenever one is switched to", async () => {
    const wasm = fakeWasm(OTHER_MACHINE);
    await switchMachine(wasm, "test-machine");
    expect(loadRememberedMachine()).toBe("test-machine");
  });

  it("does not record a switch the core refused", async () => {
    const wasm = fakeWasm(OTHER_MACHINE);
    wasm._setMachine = vi.fn(async () => 0);

    await switchMachine(wasm, "nonexistent");
    expect(loadRememberedMachine()).toBeNull();
  });

  it("starts the session on the remembered machine", async () => {
    rememberMachine("test-machine");
    const wasm = fakeWasm(OTHER_MACHINE);

    const profile = await restoreRememberedMachine(wasm);

    expect(profile.key).toBe("test-machine");
    expect(machineDisplay().width).toBe(640);
  });

  it("stays put when nothing is remembered", async () => {
    const wasm = fakeWasm(OTHER_MACHINE);
    const profile = await restoreRememberedMachine(wasm);

    expect(profile).toBe(APPLE_IIE_FALLBACK);
    expect(wasm._setMachine).not.toHaveBeenCalled();
  });

  it("ignores a remembered machine whose ROMs are not in this build", async () => {
    // A build without the II+ ROMs must not start on the II+ and show a blank
    // screen; it falls back to the machine that does work.
    rememberMachine("test-machine");
    const wasm = fakeWasm(OTHER_MACHINE);
    wasm._isMachineRunnable = vi.fn(async () => 0);

    const profile = await restoreRememberedMachine(wasm);

    expect(profile).toBe(APPLE_IIE_FALLBACK);
    expect(wasm._setMachine).not.toHaveBeenCalled();
  });

  it("ignores a remembered machine the core does not know", async () => {
    rememberMachine("apple2gs");
    const wasm = fakeWasm(OTHER_MACHINE);
    wasm._isMachineRunnable = vi.fn(async () => 0);

    expect(await restoreRememberedMachine(wasm)).toBe(APPLE_IIE_FALLBACK);
  });

  it("does not switch when the remembered machine is already running", async () => {
    rememberMachine("apple2e");
    const wasm = fakeWasm(OTHER_MACHINE);

    await restoreRememberedMachine(wasm);
    expect(wasm._setMachine).not.toHaveBeenCalled();
  });

  it("survives storage being unavailable", () => {
    // Private windows and blocked site data throw on access. A preference we
    // cannot save is not worth breaking startup over.
    globalThis.localStorage = {
      getItem() {
        throw new Error("site data blocked");
      },
      setItem() {
        throw new Error("site data blocked");
      },
    };

    expect(() => rememberMachine("apple2plus")).not.toThrow();
    expect(loadRememberedMachine()).toBeNull();
  });
});
