import { describe, expect, it } from "vitest";
import {
  STATE_MAGIC,
  parseStateHeader,
} from "../../../src/js/state/state-header.js";

function header(version, machineId) {
  const bytes = new Uint8Array(16);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, STATE_MAGIC, true);
  view.setUint32(4, version, true);
  view.setUint32(8, machineId, true);
  return bytes;
}

describe("parseStateHeader", () => {
  it("reads the version and the machine that wrote the state", () => {
    expect(parseStateHeader(header(9, 0))).toEqual({
      version: 9,
      machineId: 0,
    });
    expect(parseStateHeader(header(1, 3))).toEqual({
      version: 1,
      machineId: 3,
    });
  });

  it("reads from a view into a larger buffer", () => {
    const outer = new Uint8Array(32);
    outer.set(header(9, 2), 8);
    expect(parseStateHeader(outer.subarray(8))).toEqual({
      version: 9,
      machineId: 2,
    });
  });

  it("rejects the wrong magic and anything too short", () => {
    const bad = header(9, 0);
    bad[0] = 0;
    expect(parseStateHeader(bad)).toBeNull();
    expect(parseStateHeader(new Uint8Array(4))).toBeNull();
    expect(parseStateHeader(null)).toBeNull();
  });
});
