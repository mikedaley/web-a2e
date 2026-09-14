/*
 * state-header.js - The header every machine's save state begins with
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/*
 * Every machine writes the same twelve bytes first: the magic, the format
 * version, and the id of the machine that wrote it. The host reads the id
 * without knowing the rest of the layout, which is how it can tell a //e's
 * state from a IIgs's before handing either to the core — and switch to the
 * right machine rather than have the core refuse.
 */

// "A2ES" in little-endian
export const STATE_MAGIC = 0x53324541;
export const STATE_HEADER_SIZE = 12;

/**
 * @param {Uint8Array} bytes
 * @returns {{version:number, machineId:number}|null} null unless the magic is right
 */
export function parseStateHeader(bytes) {
  if (!bytes || bytes.length < STATE_HEADER_SIZE) return null;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint32(0, true) !== STATE_MAGIC) return null;
  return {
    version: view.getUint32(4, true),
    machineId: view.getUint32(8, true),
  };
}
