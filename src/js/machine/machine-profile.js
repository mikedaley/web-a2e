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
  logotype: "//e",
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
  // What the processor has to show. A //e's is an 8-bit CPU with 16-bit
  // addresses and no banks, which is the fallback because it is the only
  // machine that existed when this module was written.
  processor: Object.freeze({
    addressBits: 16,
    registerBits: 8,
    hasBanks: false,
    hasDirectPage: false,
    hasModes: false,
    flags: "NV-BDIZC",
    nativeFlags: "",
  }),
  display: Object.freeze({
    dotsPerLine: 560,
    width: 560,
    height: 384,
    lineDoubling: 2,
    framebufferSize: 560 * 384 * 4,
    // Where the text screen lands in the frame; a //e's fills it.
    text: Object.freeze({ left: 0, top: 0, width: 560, height: 384 }),
    // The shape the frame is shown at, which is the frame's own on a //e.
    aspect: Object.freeze({ width: 560, height: 384 }),
  }),
  caps: Object.freeze({
    hasAuxRam: true,
    has80Column: true,
    hasDoubleHires: true,
    hasLanguageCard: true,
    hasAltCharSet: true,
    hasUkCharSet: true,
    hasLowercase: true,
    hasInternalSlotRom: true,
    hasExpansionSlots: true,
    hasOpenAppleKeys: true,
    hasIOUDisable: true,
    inhibitsBurstInText: true,
  }),
  slots: Object.freeze([]),
});

let current = APPLE_IIE_FALLBACK;

/*
 * The machine the user last chose, remembered across sessions.
 *
 * Stored as the profile's key rather than its numeric id: ids are an
 * implementation detail of the core's registry and could be renumbered, while
 * a key names a machine for good. An unrecognised or unrunnable key is ignored
 * rather than honoured, so a build without the II+ ROMs, or a stored value
 * from a later version, quietly falls back to the machine that does work.
 */
const MACHINE_STORAGE_KEY = "a2e-machine";

/** The machine key remembered from a previous session, or null. */
export function loadRememberedMachine() {
  try {
    const key = localStorage.getItem(MACHINE_STORAGE_KEY);
    return key && key.trim() ? key.trim() : null;
  } catch {
    return null; // Private windows and blocked site data are not an error here
  }
}

/** Remember a machine for the next session. */
export function rememberMachine(key) {
  try {
    if (key) localStorage.setItem(MACHINE_STORAGE_KEY, key);
  } catch {
    // A preference we could not save is not worth interrupting anyone over.
  }
}

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

const FALLBACK_PROCESSOR = APPLE_IIE_FALLBACK.processor;

/** The machine currently being emulated. Never null. */
export function getMachineProfile() {
  return current;
}

/** Shorthand for the framebuffer geometry, which is what most callers want. */
export function machineDisplay() {
  return current.display;
}

/**
 * The rectangle of the frame the text screen occupies. A //e's is the whole
 * frame; a IIgs's sits inside a border. A profile without one (an older core)
 * is taken to fill the frame.
 */
/**
 * The shape the frame is shown at, width over height. A //e's frame is shown
 * at its own ratio; a IIgs's raster, border and all, at a monitor's 4:3. A
 * profile without one (an older core) is shown at the frame's ratio.
 */
export function machineAspect() {
  const d = current.display;
  return d.aspect ? d.aspect.width / d.aspect.height : d.width / d.height;
}

/**
 * Tell the stylesheet, for the layouts that size the screen in CSS.
 */
export function applyMachineAspectToDocument() {
  const d = current.display;
  const a = d.aspect || { width: d.width, height: d.height };
  document.documentElement.style.setProperty("--screen-aspect", `${a.width} / ${a.height}`);
}

export function machineTextArea() {
  const d = current.display;
  return d.text || { left: 0, top: 0, width: d.width, height: d.height };
}

/**
 * What the machine's processor has to show: how wide an address is, how wide
 * a register is, whether there are banks, a direct page and a second mode,
 * and what its status flags are called.
 *
 * A debug view reads this rather than assuming a 6502. A profile from an
 * older core that does not describe its processor is taken to be a //e's,
 * which is what it would have been.
 */
export function machineProcessor() {
  return current.processor || FALLBACK_PROCESSOR;
}

/** How many hex digits an address needs: four, or six where there are banks. */
export function machineAddressDigits() {
  return machineProcessor().addressBits > 16 ? 6 : 4;
}

/**
 * An address as this machine writes one.
 *
 * A //e's is four hex digits. A machine with banks gets the bank, a slash and
 * the offset — "00/FF69" — which is how its own monitor and its diagnostics
 * write one, and is the form the core's disassembler emits.
 */
export function formatMachineAddress(address) {
  const value = address >>> 0;
  if (machineProcessor().addressBits <= 16) {
    return (value & 0xffff).toString(16).toUpperCase().padStart(4, "0");
  }
  const bank = (value >>> 16) & 0xff;
  const offset = value & 0xffff;
  return (
    bank.toString(16).toUpperCase().padStart(2, "0") +
    "/" +
    offset.toString(16).toUpperCase().padStart(4, "0")
  );
}

/** The highest address the machine has, for validating what a user typed. */
export function machineAddressMask() {
  return machineProcessor().addressBits > 16 ? 0xffffff : 0xffff;
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
    if (typeof document !== "undefined") applyMachineAspectToDocument();
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
    const profile = await loadMachineProfile(wasmModule);
    if (profile) rememberMachine(profile.key);
    return profile;
  } catch (err) {
    console.warn(`Could not switch to machine "${key}":`, err);
    return null;
  }
}

/**
 * Start the session on the machine the user last chose.
 *
 * Called once, before anything sizes itself to the picture. A remembered
 * machine that the core does not know, or cannot run for want of its ROMs, is
 * ignored: the session stays on whichever machine the core built by default,
 * which is always one that works.
 */
export async function restoreRememberedMachine(wasmModule) {
  const key = loadRememberedMachine();
  if (!key || key === current.key) return current;

  try {
    const runnable = await callWithString(wasmModule, "_isMachineRunnable", key);
    if (!runnable) {
      console.info(
        `Remembered machine "${key}" cannot be started here; staying on ` +
          `${current.name}.`,
      );
      return current;
    }
  } catch {
    return current;
  }

  return (await switchMachine(wasmModule, key)) || current;
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
