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

/** Every machine the core can run, for a host that wants to offer a choice. */
export async function listMachineProfiles(wasmModule) {
  try {
    const count = await wasmModule._getMachineCount();
    const profiles = [];
    for (let i = 0; i < count; i++) {
      const json = await wasmModule.callString("_getMachineProfileJSONAt", i);
      if (json) profiles.push(JSON.parse(json));
    }
    return profiles;
  } catch (err) {
    console.warn("Could not list machine profiles:", err);
    return [current];
  }
}

/** Test seam: adopt a profile without a core. */
export function setMachineProfileForTesting(profile) {
  current = profile ? Object.freeze(profile) : APPLE_IIE_FALLBACK;
  return current;
}

export { APPLE_IIE_FALLBACK };
