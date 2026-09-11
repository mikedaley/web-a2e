/*
 * shared-buffers.js - SharedArrayBuffer layouts, allocation, constants
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

// --- Framebuffer ---
//
// This is the *allocation* size, not a description of the machine. A
// SharedArrayBuffer cannot be resized after it is handed to the Worker and the
// AudioWorklet, so the slot is sized once, up front, and has to be at least as
// large as the framebuffer of any machine the core can run.
//
// The biggest is the IIgs's: 640x400, which is a Super Hi-Res screen. The //e,
// the II Plus and the //c all draw 560x384 into a slot that is larger than
// they need, which costs 164KB of address space each and saves the transport
// falling back to postMessage on the one machine that would not fit. main.js
// still checks the fit at startup rather than letting a frame write past the
// end of the slot.
export const FB_WIDTH = 640;
export const FB_HEIGHT = 400;
export const FB_BYTES = FB_WIDTH * FB_HEIGHT * 4; // 1,024,000 bytes RGBA
// Two frames are allocated and written alternately: the Worker fills the half
// the renderer is not reading, so a frame can never be torn by a write landing
// mid-upload. Which half holds the newest complete frame is published in the
// control block as CTRL_FRAME_INDEX.
export const FB_SLOTS = 2;
export const FB_TOTAL_BYTES = FB_BYTES * FB_SLOTS;

// --- Audio ring buffer (Phase 2) ---
// Layout: [writePos:i32][readPos:i32][ringData:f32[RING_SIZE]]
export const AUDIO_HEADER_BYTES = 8;               // 2 x Int32 (writePos, readPos)
export const AUDIO_RING_FRAMES = 16384;             // stereo frames
export const AUDIO_RING_FLOATS = AUDIO_RING_FRAMES * 2; // interleaved L/R
export const AUDIO_RING_BYTES = AUDIO_RING_FLOATS * 4;
export const AUDIO_BUFFER_TOTAL = AUDIO_HEADER_BYTES + AUDIO_RING_BYTES;
export const AUDIO_WRITE_POS_OFFSET = 0;            // byte offset of writePos Int32
export const AUDIO_READ_POS_OFFSET = 4;             // byte offset of readPos Int32
export const AUDIO_DATA_OFFSET = AUDIO_HEADER_BYTES; // byte offset of ring data

// --- Control/status block (Phase 3) ---
// All Int32 values, indexed by Int32 offset
export const CTRL_FRAME_READY = 0;
export const CTRL_IS_PAUSED = 1;
export const CTRL_PC = 2;
export const CTRL_A = 3;
export const CTRL_X = 4;
export const CTRL_Y = 5;
export const CTRL_SP = 6;
export const CTRL_P = 7;
export const CTRL_BEAM_SCANLINE = 8;
export const CTRL_BEAM_HPOS = 9;
export const CTRL_BEAM_COLUMN = 10;
export const CTRL_FRAME_CYCLE = 11;
export const CTRL_BP_HIT = 12;
export const CTRL_BP_ADDR = 13;
export const CTRL_TOTAL_CYCLES_LO = 14;
export const CTRL_TOTAL_CYCLES_HI = 15;
export const CTRL_FRAME_INDEX = 16;                 // which framebuffer half holds the newest frame
export const CTRL_BLOCK_INTS = 64;                  // 256 bytes
export const CTRL_BLOCK_BYTES = CTRL_BLOCK_INTS * 4;

/**
 * Check if SharedArrayBuffer is available (requires COOP/COEP headers)
 */
export function isSharedArrayBufferAvailable() {
  try {
    return typeof SharedArrayBuffer !== 'undefined' &&
           typeof Atomics !== 'undefined';
  } catch (e) {
    return false;
  }
}

/**
 * Allocate shared buffers for Phase 2+3
 */
export function allocateSharedBuffers() {
  if (!isSharedArrayBufferAvailable()) {
    return null;
  }
  return {
    audio: new SharedArrayBuffer(AUDIO_BUFFER_TOTAL),
    framebuffer: new SharedArrayBuffer(FB_TOTAL_BYTES),
    control: new SharedArrayBuffer(CTRL_BLOCK_BYTES),
  };
}
