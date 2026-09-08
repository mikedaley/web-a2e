/*
 * machine-profile.js - The host's view of which machine the core is emulating
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/*
 * The core describes the machine it is modelling (src/core/machine/machine_profile.hpp)
 * and this module is where the host reads that description.
 *
 * Before this existed the host simply knew: 560x384 appeared as a literal in
 * the screenshot path, the text-selection overlay, the printer's screen dump
 * and the agent tools, and each of them was independently correct only because
 * there was one machine. A second machine with a different picture would have
 * had to find all of them.
 *
 * The profile is fetched once, after the WASM module is up and before anything
 * draws. Until then the //e values below stand in, because the host builds a
 * renderer and an overlay before it can ask.
 */

// The //e, as a fallback for the window between page load and the first fetch,
// and for any caller that runs without a core attached (tests, the printer
// window opened on its own). It mirrors APPLE_IIE_PROFILE; the core remains
// the authority, and a fetched profile replaces this wholesale.
const APPLE_IIE_FALLBACK = Object.freeze({
  id: 0,
  key: "apple2e",
  name: "Apple //e Enhanced",
  shortName: "//e",
  cpu: "65C02",
  timing: Object.freeze({
    cpuClockHz: 1023000,
    cyclesPerScanline: 65,
    hblankCycles: 25,
    visibleColumns: 40,
    scanlinesPerFrame: 262,
    visibleScanlines: 192,
    mixedModeTextScanline: 160,
    cyclesPerFrame: 17030,
  }),
  memory: Object.freeze({
    mainRamSize: 65536,
    auxRamSize: 65536,
    romSize: 16384,
    charRomSize: 8192,
  }),
  display: Object.freeze({
    dotsPerLine: 560,
    width: 560,
    height: 384,
    lineDoubling: 2,
    framebufferSize: 560 * 384 * 4,
  }),
  caps: Object.freeze({
    hasAuxRam: true,
    has80Column: true,
    hasDoubleHires: true,
    hasLanguageCard: true,
    hasAltCharSet: true,
    hasOpenAppleKeys: true,
    hasIOUDisable: true,
    inhibitsBurstInText: true,
  }),
  slots: Object.freeze([]),
});

let current = APPLE_IIE_FALLBACK;

/*
 * Call a core export that takes a `const char *`.
 *
 * A JavaScript string is not a pointer. Handing one straight to a WASM export
 * passes whatever the number coercion produces, which the core reads as an
 * address — so the call does not fail, it silently looks up the wrong thing.
 * That is exactly how an existing machine first reported itself unrunnable and
 * every switch was refused. The string has to be copied into the core's heap
 * and the pointer freed afterwards.
 */
async function callWithString(wasmModule, fn, text) {
  const bytes = text.length + 1;
  const ptr = await wasmModule._malloc(bytes);
  try {
    await wasmModule.stringToUTF8(text, ptr, bytes);
    return await wasmModule[fn](ptr);
  } finally {
    wasmModule._free(ptr); // fire-and-forget, as everywhere else
  }
}

/** The machine currently being emulated. Never null. */
export function getMachineProfile() {
  return current;
}

/** Shorthand for the framebuffer geometry, which is what most callers want. */
export function machineDisplay() {
  return current.display;
}

/** Shorthand for the machine's cycle timing. */
export function machineTiming() {
  return current.timing;
}

/**
 * Ask the core which machine it is running and adopt the answer.
 *
 * One round trip: the Worker services RPCs on the thread that runs the
 * emulation, so the whole profile arrives as a single JSON string rather than
 * a field at a time.
 *
 * A failure here is not fatal. The //e fallback is a correct description of the
 * only machine that exists today, so a host that could not ask still draws the
 * right picture; it just would not follow a future machine.
 */
export async function loadMachineProfile(wasmModule) {
  try {
    const json = await wasmModule.callString("_getMachineProfileJSON");
    if (!json) return current;

    const parsed = JSON.parse(json);
    if (!parsed || !parsed.display || !parsed.display.width) return current;

    current = Object.freeze(parsed);
  } catch (err) {
    console.warn("Could not read the machine profile from the core:", err);
  }
  return current;
}

/**
 * Every machine the core knows about, for a host that wants to offer a choice.
 *
 * Each entry carries a `runnable` flag. A machine can be fully described and
 * still have no ROM to run: the II+ ROM set is optional at build time, and
 * without it the machine is real in every respect except that it would never
 * reach a prompt. A chooser that ignored this would offer a blank screen.
 */
export async function listMachineProfiles(wasmModule) {
  try {
    const count = await wasmModule._getMachineCount();
    const profiles = [];
    for (let i = 0; i < count; i++) {
      const json = await wasmModule.callString("_getMachineProfileJSONAt", i);
      if (!json) continue;
      const profile = JSON.parse(json);
      profile.runnable = !!(await callWithString(
        wasmModule, "_isMachineRunnable", profile.key));
      profiles.push(profile);
    }
    return profiles;
  } catch (err) {
    console.warn("Could not list machine profiles:", err);
    return [current];
  }
}

/**
 * Switch the core to a different machine.
 *
 * There is no way to convert a running machine into another one — the RAM, the
 * cards and the save state are all shaped to the machine that made them — so
 * the core destroys the emulator and builds the new one from scratch. Inserted
 * media and host state do not survive, exactly as they would not across a page
 * reload, so the caller is responsible for putting them back.
 *
 * Returns the new profile, or null if the key names no machine or the core
 * refused.
 */
export async function switchMachine(wasmModule, key) {
  try {
    const ok = await callWithString(wasmModule, "_setMachine", key);
    if (!ok) return null;
    return await loadMachineProfile(wasmModule);
  } catch (err) {
    console.warn(`Could not switch to machine "${key}":`, err);
    return null;
  }
}

/** Whether the running machine has the ROM it needs to start. */
export async function hasSystemRom(wasmModule) {
  try {
    return !!(await wasmModule._hasSystemROM());
  } catch {
    return false;
  }
}

/** Test seam: adopt a profile without a core. */
export function setMachineProfileForTesting(profile) {
  current = profile ? Object.freeze(profile) : APPLE_IIE_FALLBACK;
  return current;
}

export { APPLE_IIE_FALLBACK };
