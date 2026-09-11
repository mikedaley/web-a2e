/*
 * iigs-memory.js - how much fast RAM the IIgs has, and remembering it
 *
 * A ROM 01 shipped with 256K soldered to the board, and almost nobody left it
 * there: a memory expansion card was the first thing most owners fitted, and
 * anything bigger than ProDOS 8 expects to find one. So the size is a choice,
 * the way the machine itself is, and it is remembered the same way.
 *
 * Changing it is destructive — the core rebuilds the machine, because there is
 * no way to grow RAM underneath a running program — so the caller asks first.
 */

/** The sizes offered, smallest first. `stock` marks what a ROM 01 shipped. */
export const IIGS_MEMORY_SIZES = [
  { kb: 256, label: "256K", note: "As shipped" },
  { kb: 512, label: "512K", note: "" },
  { kb: 1024, label: "1M", note: "" },
  { kb: 2048, label: "2M", note: "" },
  { kb: 4096, label: "4M", note: "" },
  { kb: 8192, label: "8M", note: "The most the bus reaches" },
];

export const IIGS_MEMORY_DEFAULT_KB = 1024;

const STORAGE_KEY = "a2e-iigs-memory-kb";

/** The nearest offered size at or below `kb`, never smaller than the first. */
export function nearestMemorySize(kb) {
  const wanted = Number(kb);
  if (!Number.isFinite(wanted)) return IIGS_MEMORY_DEFAULT_KB;
  let best = IIGS_MEMORY_SIZES[0].kb;
  for (const size of IIGS_MEMORY_SIZES) {
    if (size.kb <= wanted) best = size.kb;
  }
  return best;
}

/** How the size is written where there is room for one line. */
export function formatMemorySize(kb) {
  return kb >= 1024 ? `${kb / 1024}M` : `${kb}K`;
}

/**
 * The size remembered from a previous session, or the default.
 *
 * A stored value that is not one of the offered sizes is rounded down to one
 * rather than refused: a size written by a later version, or by hand, should
 * still give a machine that starts.
 */
export function loadRememberedMemoryKB() {
  try {
    const stored = localStorage.getItem(STORAGE_KEY);
    if (!stored) return IIGS_MEMORY_DEFAULT_KB;
    const kb = parseInt(stored, 10);
    return Number.isFinite(kb) && kb > 0
      ? nearestMemorySize(kb)
      : IIGS_MEMORY_DEFAULT_KB;
  } catch {
    return IIGS_MEMORY_DEFAULT_KB; // Private windows are not an error here
  }
}

/** Remember a size for the next session. */
export function rememberMemoryKB(kb) {
  try {
    localStorage.setItem(STORAGE_KEY, String(kb));
  } catch {
    // A preference we could not save is not worth interrupting anyone over.
  }
}

/**
 * Tell the core how much RAM a IIgs should have, and remember it.
 *
 * Returns the size the core settled on, which is what it will report from then
 * on — it rounds to whole 64K banks and will not go outside what the machine
 * could have had. A core without these exports (an older build) is not an
 * error: the caller gets null and leaves the machine alone.
 */
export async function applyMemoryKB(wasmModule, kb) {
  if (!wasmModule?._setIIgsMemoryKB) return null;
  try {
    const ok = await wasmModule._setIIgsMemoryKB(kb);
    if (!ok) return null;
    const settled = await wasmModule._getIIgsMemoryKB();
    rememberMemoryKB(settled);
    return settled;
  } catch (err) {
    console.warn(`Could not set the IIgs memory size to ${kb}K:`, err);
    return null;
  }
}

/** What the core says the machine has now, or null if it cannot say. */
export async function readMemoryKB(wasmModule) {
  if (!wasmModule?._getIIgsMemoryKB) return null;
  try {
    return await wasmModule._getIIgsMemoryKB();
  } catch {
    return null;
  }
}
