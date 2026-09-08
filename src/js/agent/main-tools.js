/*
 * main-tools.js - Main emulator control tools
 *
 * Written by
 *  Shawn Bullock <shawn@agenticexpert.ai>
 */

import { readDiskFileBytes } from "./file-explorer-tools.js";
import { machineDisplay } from "../machine/machine-profile.js";

/**
 * Parse address or length value from hex ($xxxx) or decimal format
 * @param {string|number} value - Value to parse
 * @param {string} paramName - Parameter name for error messages
 * @returns {number} Parsed integer value
 */
function parseHexOrDecimal(value, paramName) {
  if (value === undefined || value === null) {
    throw new Error(`${paramName} parameter is required`);
  }

  // If already a number, use it directly
  if (typeof value === "number") {
    return Math.floor(value);
  }

  // If string, check for hex prefix
  if (typeof value === "string") {
    const trimmed = value.trim();

    // Hex format: $xxxx or 0xXXXX
    if (trimmed.startsWith("$")) {
      const parsed = parseInt(trimmed.substring(1), 16);
      if (isNaN(parsed)) {
        throw new Error(`Invalid hex ${paramName}: ${value}`);
      }
      return parsed;
    }

    if (trimmed.toLowerCase().startsWith("0x")) {
      const parsed = parseInt(trimmed, 16);
      if (isNaN(parsed)) {
        throw new Error(`Invalid hex ${paramName}: ${value}`);
      }
      return parsed;
    }

    // Decimal format
    const parsed = parseInt(trimmed, 10);
    if (isNaN(parsed)) {
      throw new Error(`Invalid decimal ${paramName}: ${value}`);
    }
    return parsed;
  }

  throw new Error(`${paramName} must be a number or string`);
}

export const mainTools = {
  /**
   * Power control
   */
  emulatorPower: async (args) => {
    const { action = "toggle" } = args;

    const emulator = window.emulator;
    if (!emulator) {
      throw new Error("Emulator not available");
    }

    if (action === "on" && !emulator.running) {
      await emulator.start();
    } else if (action === "off" && emulator.running) {
      await emulator.stop();
    } else if (action === "toggle") {
      if (emulator.running) {
        await emulator.stop();
      } else {
        await emulator.start();
      }
    }

    return {
      success: true,
      running: emulator.running,
      message: `Emulator is now ${emulator.running ? "running" : "stopped"}`,
    };
  },

  /**
   * Ctrl-Reset (warm reset)
   */
  emulatorCtrlReset: async (args) => {
    const wasmModule = window.emulator?.wasmModule;
    if (!wasmModule) {
      throw new Error("Emulator not available");
    }

    wasmModule._warmReset();

    return {
      success: true,
      message: "Ctrl-Reset executed (warm reset)",
    };
  },

  /**
   * Reboot (cold reset)
   */
  emulatorReboot: async (args) => {
    const wasmModule = window.emulator?.wasmModule;
    if (!wasmModule) {
      throw new Error("Emulator not available");
    }

    wasmModule._reset();

    return {
      success: true,
      message: "Reboot executed (cold reset)",
    };
  },

  /**
   * Load binary data into memory at a specific address
   */
  directLoadBinaryAt: async (args) => {
    const { address, contentBase64 } = args;

    if (!contentBase64) {
      throw new Error("contentBase64 parameter is required");
    }

    const wasmModule = window.emulator?.wasmModule;
    if (!wasmModule) {
      throw new Error("WASM module not available");
    }

    // Parse address (supports $xxxx hex or decimal)
    const addr = parseHexOrDecimal(address, "address");

    // Decode base64 to binary
    const binaryString = atob(contentBase64);
    const bytes = new Uint8Array(binaryString.length);
    for (let i = 0; i < binaryString.length; i++) {
      bytes[i] = binaryString.charCodeAt(i);
    }

    // Pause emulator while writing to ensure clean state
    const wasPaused = await wasmModule._isPaused();
    wasmModule._setPaused(true);

    // Write bytes using writeMemory (like assembler does)
    for (let i = 0; i < bytes.length; i++) {
      wasmModule._writeMemory((addr + i) & 0xffff, bytes[i]);
    }

    // Restore paused state
    wasmModule._setPaused(wasPaused);

    const addrHex = "$" + addr.toString(16).toUpperCase().padStart(4, "0");
    const endAddr = (addr + bytes.length - 1) & 0xffff;
    const endHex = "$" + endAddr.toString(16).toUpperCase().padStart(4, "0");

    return {
      success: true,
      address: addr,
      addressHex: addrHex,
      size: bytes.length,
      endAddress: endAddr,
      endAddressHex: endHex,
      message: `Loaded ${bytes.length} bytes to ${addrHex}-${endHex}`,
    };
  },

  /**
   * Save binary data from memory range to base64
   */
  directSaveBinaryRangeTo: async (args) => {
    const { address, length } = args;

    const wasmModule = window.emulator?.wasmModule;
    if (!wasmModule) {
      throw new Error("WASM module not available");
    }

    // Parse address and length (supports $xxxx hex or decimal)
    const addr = parseHexOrDecimal(address, "address");
    const len = parseHexOrDecimal(length, "length");

    if (len <= 0) {
      throw new Error("length must be > 0");
    }

    // Read bytes using peekMemory (no side effects)
    const bytes = new Uint8Array(len);
    for (let i = 0; i < len; i++) {
      bytes[i] = await wasmModule._peekMemory((addr + i) & 0xffff);
    }

    // Encode to base64
    let binaryString = "";
    for (let i = 0; i < bytes.length; i++) {
      binaryString += String.fromCharCode(bytes[i]);
    }
    const contentBase64 = btoa(binaryString);

    const addrHex = "$" + addr.toString(16).toUpperCase().padStart(4, "0");
    const lengthHex = "$" + len.toString(16).toUpperCase().padStart(4, "0");
    const endAddr = (addr + len - 1) & 0xffff;
    const endHex = "$" + endAddr.toString(16).toUpperCase().padStart(4, "0");

    return {
      success: true,
      address: addr,
      addressHex: addrHex,
      length: len,
      lengthHex: lengthHex,
      endAddress: endAddr,
      endAddressHex: endHex,
      contentBase64: contentBase64,
      message: `Read ${len} bytes (${lengthHex}) from ${addrHex}-${endHex}`,
    };
  },

  /**
   * Load a binary straight into emulator memory at an address WITHOUT routing
   * the bytes through the LLM context (no base64 in the conversation). Two
   * sources, pick one:
   *   - sandbox file: pass `path` (e.g. "[t]/bin/pic.bin") — read server-side
   *     via the MCP load_file tool, decoded in the browser.
   *   - disk file: pass `filename` (+ optional `drive`, default 1) — read from
   *     the DOS 3.3 / ProDOS disk image currently mounted in that drive (1=Drive 1, 2=Drive 2).
   * Prefer this over load_file/getDiskFileContent + directLoadBinaryAt: those
   * return base64 to the LLM, this never does. Optional `offset`/`length` slice
   * the file (e.g. skip a header) before writing.
   */
  directLoadFileAt: async (args) => {
    const { address, path, filename, drive = 1, offset = 0, length } = args;
    const driveIndex = drive - 1;

    const wasmModule = window.emulator?.wasmModule;
    if (!wasmModule) {
      throw new Error("WASM module not available");
    }
    if (!path && !filename) {
      throw new Error(
        "either path (sandbox file) or filename (disk file) is required",
      );
    }

    const addr = parseHexOrDecimal(address, "address");

    // Fetch bytes from the chosen source — never through LLM context.
    let bytes;
    let source;
    if (path) {
      const agentManager = window.emulator?.agentManager;
      if (!agentManager) {
        throw new Error("Agent manager not available");
      }
      const result = await agentManager.callMCPTool("load_file", {
        path,
        binary: true,
      });
      if (!result || !result.success) {
        throw new Error(result?.error || `Failed to load file: ${path}`);
      }
      const binaryString = atob(result.contentBase64);
      bytes = new Uint8Array(binaryString.length);
      for (let i = 0; i < binaryString.length; i++) {
        bytes[i] = binaryString.charCodeAt(i);
      }
      source = path;
    } else {
      const disk = await readDiskFileBytes(wasmModule, driveIndex, filename);
      bytes = disk.bytes;
      source = `${filename} (drive ${drive}, ${disk.format})`;
    }

    // Optional slice (offset/length) before writing
    const off = parseHexOrDecimal(offset, "offset");
    let slice = off > 0 ? bytes.subarray(off) : bytes;
    if (length !== undefined) {
      const len = parseHexOrDecimal(length, "length");
      slice = slice.subarray(0, len);
    }

    // Pause while writing for clean state
    const wasPaused = await wasmModule._isPaused();
    wasmModule._setPaused(true);
    for (let i = 0; i < slice.length; i++) {
      wasmModule._writeMemory((addr + i) & 0xffff, slice[i]);
    }
    wasmModule._setPaused(wasPaused);

    const hex = (a) =>
      "$" + (a & 0xffff).toString(16).toUpperCase().padStart(4, "0");
    const endAddr = (addr + slice.length - 1) & 0xffff;

    return {
      success: true,
      source,
      address: addr,
      addressHex: hex(addr),
      size: slice.length,
      endAddress: endAddr,
      endAddressHex: hex(endAddr),
      message: `Loaded ${slice.length} bytes from ${source} to ${hex(addr)}-${hex(endAddr)}`,
    };
  },

  /**
   * Copy a block of emulator memory from one address to another, entirely on
   * the Apple side — no base64, no host round-trip. Reads follow the current
   * RAMRD bank, writes the current RAMWRT bank (set soft switches first if you
   * need a specific main/aux bank). The whole source block is buffered before
   * writing, so overlapping ranges copy correctly in either direction.
   */
  directMemoryCopy: async (args) => {
    const { src, dst, length } = args;

    const wasmModule = window.emulator?.wasmModule;
    if (!wasmModule) {
      throw new Error("WASM module not available");
    }

    const srcAddr = parseHexOrDecimal(src, "src");
    const dstAddr = parseHexOrDecimal(dst, "dst");
    const len = parseHexOrDecimal(length, "length");
    if (len <= 0) {
      throw new Error("length must be > 0");
    }

    const wasPaused = await wasmModule._isPaused();
    wasmModule._setPaused(true);

    // Buffer the source first (peek = no side effects), then write.
    const buf = new Uint8Array(len);
    for (let i = 0; i < len; i++) {
      buf[i] = await wasmModule._peekMemory((srcAddr + i) & 0xffff);
    }
    for (let i = 0; i < len; i++) {
      wasmModule._writeMemory((dstAddr + i) & 0xffff, buf[i]);
    }

    wasmModule._setPaused(wasPaused);

    const hex = (a) =>
      "$" + (a & 0xffff).toString(16).toUpperCase().padStart(4, "0");
    return {
      success: true,
      src: srcAddr,
      srcHex: hex(srcAddr),
      dst: dstAddr,
      dstHex: hex(dstAddr),
      length: len,
      message: `Copied ${len} bytes ${hex(srcAddr)} -> ${hex(dstAddr)}`,
    };
  },

  /**
   * Fill a block of emulator memory with a constant byte. Apple-side, no
   * base64. Writes follow the current RAMWRT bank. `value` defaults to 0.
   */
  directMemoryFill: async (args) => {
    const { address, length, value = 0 } = args;

    const wasmModule = window.emulator?.wasmModule;
    if (!wasmModule) {
      throw new Error("WASM module not available");
    }

    const addr = parseHexOrDecimal(address, "address");
    const len = parseHexOrDecimal(length, "length");
    const val = parseHexOrDecimal(value, "value") & 0xff;
    if (len <= 0) {
      throw new Error("length must be > 0");
    }

    const wasPaused = await wasmModule._isPaused();
    wasmModule._setPaused(true);
    for (let i = 0; i < len; i++) {
      wasmModule._writeMemory((addr + i) & 0xffff, val);
    }
    wasmModule._setPaused(wasPaused);

    const hex = (a) =>
      "$" + (a & 0xffff).toString(16).toUpperCase().padStart(4, "0");
    const valHex = "$" + val.toString(16).toUpperCase().padStart(2, "0");
    return {
      success: true,
      address: addr,
      addressHex: hex(addr),
      length: len,
      value: val,
      message: `Filled ${len} bytes at ${hex(addr)} with ${valHex}`,
    };
  },

  /**
   * Capture the current screen as a base64 PNG image
   */
  captureScreenshot: async () => {
    const emulator = window.emulator;
    if (!emulator?.wasmModule) {
      throw new Error("Emulator not available");
    }

    const imageBase64 = emulator.captureScreenshot();
    const { width, height } = machineDisplay();

    return {
      success: true,
      imageBase64,
      width,
      height,
      message: `Screen captured as ${width}x${height} PNG`,
    };
  },

  /**
   * Read text from the Apple //e screen
   * Parameters: startRow, startCol, endRow, endCol (all optional, default full screen)
   */
  captureScreenText: async (params = {}) => {
    const emulator = window.emulator;
    if (!emulator?.wasmModule) {
      throw new Error("Emulator not available");
    }

    const startRow = params.startRow ?? 0;
    const startCol = params.startCol ?? 0;
    const endRow = params.endRow ?? 23;
    const endCol = params.endCol ?? 79;

    const ptr = await emulator.wasmModule._readScreenText(startRow, startCol, endRow, endCol);
    const text = await emulator.wasmModule.UTF8ToString(ptr);

    return {
      success: true,
      text,
      startRow,
      startCol,
      endRow,
      endCol,
      message: `Screen text captured from (${startRow},${startCol}) to (${endRow},${endCol})`,
    };
  },

  /**
   * Type text into the emulator as if typed at the keyboard. Plain text types
   * literally and newlines act as Return. Special keys use {token} syntax:
   * {left} {right} {up} {down} {esc} {enter}/{return} {tab} {del} {backspace}
   * {space}, plus Ctrl combos like {ctrl-c} or {^c}, and raw codes by value
   * like {chr:4} / {chr:$1b} (CHR$(N)). Use {{ for a literal '{'.
   */
  typeKeyboard: async (params = {}) => {
    const { text } = params;

    if (typeof text !== "string" || text.length === 0) {
      throw new Error("text parameter is required");
    }

    const inputHandler = window.emulator?.inputHandler;
    if (!inputHandler) {
      throw new Error("Input handler not available");
    }

    return new Promise((resolve) => {
      inputHandler.queueTextInput(text, {
        parseTokens: true,
        onComplete: () => {
          resolve({
            success: true,
            length: text.length,
            message: `Typed ${text.length} characters`,
          });
        },
      });
    });
  },
};
