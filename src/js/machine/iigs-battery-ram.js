/*
 * iigs-battery-ram.js - keeping the IIgs's 256 bytes of settings alive
 *
 * A IIgs has a battery, and behind it 256 bytes holding everything the Control
 * Panel sets: which slot to boot from, how fast to come up, the screen and
 * border colours, the printer port, the volume. The battery is the reason
 * those settings mean anything — a machine that forgot them on every start
 * would make the Control Panel pointless.
 *
 * Without something like this the firmware finds its checksum wrong on every
 * start and writes its own defaults back over the lot, which is exactly what a
 * real machine with a dead battery does.
 *
 * **The bytes are stored and restored as they are, checksum included.** The
 * firmware validates that checksum before trusting the contents, and its
 * algorithm is not one this project has worked out. It never needs to be: as
 * long as nothing here alters what the firmware itself wrote, the checksum
 * that comes back is the one that went out. GSSquared keeps its battery RAM in
 * a file the same way, and also does not compute the checksum.
 */

const STORAGE_KEY = "a2e-iigs-battery-ram";

// How often to ask the core whether anything has changed. The question is one
// boolean, and the answer is almost always no: the firmware writes the whole
// of it once on a start it does not trust, and after that only the Control
// Panel touches it. Two seconds is often enough to catch a setting before the
// tab is closed without polling for the sake of it.
const POLL_MS = 2000;

let timer = null;

/** Read what was kept, or null if there is nothing or it is unusable. */
export function loadBatteryRam() {
  try {
    const hex = localStorage.getItem(STORAGE_KEY);
    if (!hex || hex.length % 2 !== 0) return null;
    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < bytes.length; i++) {
      const byte = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
      if (Number.isNaN(byte)) return null;
      bytes[i] = byte;
    }
    return bytes;
  } catch {
    // Private windows and cleared site data both land here; a machine that
    // starts with the firmware's defaults is the right fallback.
    return null;
  }
}

function storeBatteryRam(bytes) {
  try {
    let hex = "";
    for (const byte of bytes) hex += byte.toString(16).padStart(2, "0");
    localStorage.setItem(STORAGE_KEY, hex);
  } catch (error) {
    console.warn("could not keep the IIgs's battery RAM:", error);
  }
}

/**
 * Put what was kept back into the machine.
 *
 * Must happen before the machine runs, because the firmware reads battery RAM
 * on the way up and rewrites it the moment it does not trust what it finds.
 * Returns true if something was restored.
 */
export async function restoreBatteryRam(wasmModule) {
  if (!wasmModule?._setBatteryRam) return false;
  const size = await wasmModule._getBatteryRamSize();
  if (!size) return false; // Not a machine that has any

  const bytes = loadBatteryRam();
  if (!bytes || bytes.length !== size) return false;

  const pointer = await wasmModule._malloc(size);
  if (!pointer) return false;
  try {
    await wasmModule.heapWrite(pointer, bytes);
    // Awaited, so the free below cannot reach the Worker first.
    await wasmModule._setBatteryRam(pointer, size);
  } finally {
    wasmModule._free(pointer);
  }
  return true;
}

/** Read the machine's battery RAM out and keep it. */
export async function saveBatteryRam(wasmModule) {
  if (!wasmModule?._getBatteryRam) return;
  const size = await wasmModule._getBatteryRamSize();
  if (!size) return;
  const pointer = await wasmModule._getBatteryRam();
  if (!pointer) return;
  const bytes = await wasmModule.heapRead(pointer, size);
  storeBatteryRam(bytes);
}

/**
 * Keep it up to date while the machine runs.
 *
 * Polls the core's own "has anything written to it" flag, which is cheaper
 * than reading 256 bytes to compare them, and only reads the bytes when the
 * answer is yes.
 */
export function watchBatteryRam(wasmModule) {
  stopWatchingBatteryRam();
  timer = setInterval(async () => {
    try {
      if (!wasmModule?._batteryRamChanged) return;
      if (!(await wasmModule._batteryRamChanged())) return;
      await saveBatteryRam(wasmModule);
    } catch (error) {
      console.warn("IIgs battery RAM watch failed:", error);
    }
  }, POLL_MS);

  // A tab being closed or hidden is the moment most likely to lose a setting
  // the poll has not caught yet.
  if (typeof document !== "undefined" && !watchBatteryRam._listening) {
    watchBatteryRam._listening = true;
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState === "hidden") {
        saveBatteryRam(wasmModule).catch(() => {});
      }
    });
  }
}

export function stopWatchingBatteryRam() {
  if (timer !== null) {
    clearInterval(timer);
    timer = null;
  }
}
