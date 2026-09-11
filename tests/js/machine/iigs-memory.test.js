/*
 * iigs-memory.test.js - how much memory the IIgs has
 *
 * Two things could quietly undo this: a remembered size that is not one the
 * menu offers leaving the machine unstartable, and the core's answer not being
 * what gets remembered — which would put the menu out of step with the machine
 * the moment the core rounded a size to whole banks.
 */

import { beforeEach, describe, expect, it, vi } from "vitest";
import {
  IIGS_MEMORY_DEFAULT_KB,
  IIGS_MEMORY_SIZES,
  applyMemoryKB,
  formatMemorySize,
  loadRememberedMemoryKB,
  nearestMemorySize,
  readMemoryKB,
  rememberMemoryKB,
} from "../../../src/js/machine/iigs-memory.js";

/** Minimal stand-in for localStorage; these tests run in plain node. */
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

describe("the sizes offered", () => {
  it("start at what a ROM 01 shipped with and end at what the bus reaches", () => {
    expect(IIGS_MEMORY_SIZES[0].kb).toBe(256);
    expect(IIGS_MEMORY_SIZES[IIGS_MEMORY_SIZES.length - 1].kb).toBe(8192);
  });

  it("are whole numbers of 64K banks, in order", () => {
    let previous = 0;
    for (const size of IIGS_MEMORY_SIZES) {
      expect(size.kb % 64).toBe(0);
      expect(size.kb).toBeGreaterThan(previous);
      previous = size.kb;
    }
  });

  it("default to something GS/OS could use, not to what the board had", () => {
    // 256K is what a ROM 01 shipped with and almost nobody left it there.
    expect(IIGS_MEMORY_DEFAULT_KB).toBeGreaterThan(256);
  });
});

describe("nearestMemorySize", () => {
  it("rounds down to an offered size rather than refusing", () => {
    expect(nearestMemorySize(1500)).toBe(1024);
    expect(nearestMemorySize(8192)).toBe(8192);
    expect(nearestMemorySize(999999)).toBe(8192);
  });

  it("never goes below the smallest a machine could have", () => {
    expect(nearestMemorySize(64)).toBe(256);
    expect(nearestMemorySize(0)).toBe(256);
  });

  it("falls back to the default for something that is not a number", () => {
    expect(nearestMemorySize("banana")).toBe(IIGS_MEMORY_DEFAULT_KB);
  });
});

describe("formatMemorySize", () => {
  it("writes megabytes as megabytes", () => {
    expect(formatMemorySize(256)).toBe("256K");
    expect(formatMemorySize(1024)).toBe("1M");
    expect(formatMemorySize(8192)).toBe("8M");
  });
});

describe("what is remembered", () => {
  it("is the default when nothing has been stored", () => {
    expect(loadRememberedMemoryKB()).toBe(IIGS_MEMORY_DEFAULT_KB);
  });

  it("survives a round trip", () => {
    rememberMemoryKB(2048);
    expect(loadRememberedMemoryKB()).toBe(2048);
  });

  it("rounds a stored size the menu does not offer down to one it does", () => {
    // A value written by a later version, or by hand, must still give a
    // machine that starts rather than one the menu cannot describe.
    globalThis.localStorage.setItem("a2e-iigs-memory-kb", "1536");
    expect(loadRememberedMemoryKB()).toBe(1024);
  });

  it("ignores a stored value that is not a size at all", () => {
    globalThis.localStorage.setItem("a2e-iigs-memory-kb", "lots");
    expect(loadRememberedMemoryKB()).toBe(IIGS_MEMORY_DEFAULT_KB);
  });

  it("is not an error when storage is unavailable", () => {
    globalThis.localStorage = {
      getItem() {
        throw new Error("blocked");
      },
      setItem() {
        throw new Error("blocked");
      },
    };
    expect(loadRememberedMemoryKB()).toBe(IIGS_MEMORY_DEFAULT_KB);
    expect(() => rememberMemoryKB(1024)).not.toThrow();
  });
});

describe("applyMemoryKB", () => {
  it("remembers what the core settled on, not what was asked for", async () => {
    // The core rounds to whole banks and clamps to what a machine could have,
    // so the size it reports back is the one the menu must show. Remembering
    // the request instead puts the two out of step at the next reload.
    const wasmModule = {
      _setIIgsMemoryKB: vi.fn().mockResolvedValue(true),
      _getIIgsMemoryKB: vi.fn().mockResolvedValue(1024),
    };

    const settled = await applyMemoryKB(wasmModule, 1000);

    expect(wasmModule._setIIgsMemoryKB).toHaveBeenCalledWith(1000);
    expect(settled).toBe(1024);
    expect(loadRememberedMemoryKB()).toBe(1024);
  });

  it("remembers nothing when the core refuses", async () => {
    const wasmModule = {
      _setIIgsMemoryKB: vi.fn().mockResolvedValue(false),
      _getIIgsMemoryKB: vi.fn(),
    };

    expect(await applyMemoryKB(wasmModule, 4096)).toBeNull();
    expect(wasmModule._getIIgsMemoryKB).not.toHaveBeenCalled();
    expect(loadRememberedMemoryKB()).toBe(IIGS_MEMORY_DEFAULT_KB);
  });

  it("leaves an older core that has no such export alone", async () => {
    expect(await applyMemoryKB({}, 1024)).toBeNull();
    expect(await readMemoryKB({})).toBeNull();
  });
});
