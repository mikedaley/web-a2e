/*
 * hayes-modem.js - Hayes-compatible modem emulator for SSC serial connections
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

const MODE_COMMAND = 0;
const MODE_DATA = 1;

const DEFAULT_S_REGISTERS = {
  0: 0,   // Auto-answer ring count (0 = disabled)
  1: 0,   // Ring counter
  2: 43,  // Escape character (+)
  3: 13,  // Carriage return character
  4: 10,  // Line feed character
  5: 8,   // Backspace character
  6: 2,   // Wait for dial tone (seconds)
  7: 30,  // Wait for carrier (seconds)
  12: 50, // Escape guard time (1/50 second units = 1 second)
};

export class HayesModem {
  constructor(wasmModule, serialManager) {
    this.wasmModule = wasmModule;
    this.serialManager = serialManager;
    this.mode = MODE_COMMAND;
    this.echo = true;
    this.verbal = true;
    this.commandBuffer = "";
    this.sRegisters = { ...DEFAULT_S_REGISTERS };

    // +++ escape detection
    this.plusCount = 0;
    this.lastDataTime = 0;
    this.plusTimer = null;
    this.guardTime = 1000; // ms

    // Callbacks
    this.onStatusChange = null;

    // Wire SerialManager to deliver received bytes through us
    this.serialManager.onReceive = (byte) => this.processRxByte(byte);
  }

  /**
   * Called by WASM tx callback — byte coming from the Apple II SSC
   */
  processTxByte(byte) {
    if (this.mode === MODE_DATA) {
      this.handleDataTx(byte);
      return;
    }

    const ch = String.fromCharCode(byte & 0x7F);

    // Echo back if enabled
    if (this.echo) {
      this.wasmModule._serialReceive(byte);
    }

    if (byte === 13) { // CR
      this.executeCommand(this.commandBuffer.trim());
      this.commandBuffer = "";
    } else if (byte === 8 || byte === 127) { // BS or DEL
      this.commandBuffer = this.commandBuffer.slice(0, -1);
    } else if (byte >= 32) {
      this.commandBuffer += ch;
    }
  }

  /**
   * Handle byte from Apple II while in data mode
   */
  handleDataTx(byte) {
    const now = Date.now();
    const escChar = this.sRegisters[2]; // typically '+'

    if (byte === escChar) {
      if (this.plusCount === 0 && (now - this.lastDataTime) >= this.guardTime) {
        this.plusCount = 1;
      } else if (this.plusCount > 0 && this.plusCount < 3) {
        this.plusCount++;
      } else {
        this.plusCount = 0;
        this.serialManager.sendByte(byte);
        this.lastDataTime = now;
        return;
      }

      if (this.plusCount === 3) {
        // Wait guard time then enter command mode
        if (this.plusTimer) clearTimeout(this.plusTimer);
        this.plusTimer = setTimeout(() => {
          this.mode = MODE_COMMAND;
          this.plusCount = 0;
          this.sendResponse("OK");
          if (this.onStatusChange) this.onStatusChange("command");
        }, this.guardTime);
      }
      return;
    }

    // Non-escape char: flush any pending plus chars as data
    if (this.plusCount > 0) {
      if (this.plusTimer) {
        clearTimeout(this.plusTimer);
        this.plusTimer = null;
      }
      for (let i = 0; i < this.plusCount; i++) {
        this.serialManager.sendByte(escChar);
      }
      this.plusCount = 0;
    }

    this.serialManager.sendByte(byte);
    this.lastDataTime = now;
  }

  /**
   * Byte received from remote host via SerialManager
   */
  processRxByte(byte) {
    if (this.mode === MODE_DATA) {
      this.wasmModule._serialReceive(byte);
    }
    // In command mode, ignore incoming data from remote
  }

  /**
   * Parse and execute an AT command line
   */
  executeCommand(line) {
    const upper = line.toUpperCase();

    // Must start with AT (or be just "A/")
    if (upper === "A/") {
      // Repeat last command — not implemented, just OK
      this.sendResponse("OK");
      return;
    }

    if (!upper.startsWith("AT")) {
      this.sendResponse("ERROR");
      return;
    }

    const cmd = upper.substring(2);

    // Empty AT
    if (cmd.length === 0) {
      this.sendResponse("OK");
      return;
    }

    // Parse command characters
    let i = 0;
    while (i < cmd.length) {
      const remaining = cmd.substring(i);

      // ATZ — reset
      if (remaining.startsWith("Z")) {
        this.reset();
        this.sendResponse("OK");
        return;
      }

      // AT&F — factory defaults
      if (remaining.startsWith("&F")) {
        this.reset();
        this.sendResponse("OK");
        return;
      }

      // ATE0 / ATE1 — echo
      if (remaining.startsWith("E0")) {
        this.echo = false;
        i += 2;
        continue;
      }
      if (remaining.startsWith("E1") || remaining.startsWith("E")) {
        this.echo = true;
        i += remaining.startsWith("E1") ? 2 : 1;
        continue;
      }

      // ATV0 / ATV1 — verbal/numeric
      if (remaining.startsWith("V0")) {
        this.verbal = false;
        i += 2;
        continue;
      }
      if (remaining.startsWith("V1") || remaining.startsWith("V")) {
        this.verbal = true;
        i += remaining.startsWith("V1") ? 2 : 1;
        continue;
      }

      // ATH — hang up
      if (remaining.startsWith("H0") || remaining.startsWith("H")) {
        this.hangup();
        return;
      }

      // ATDT / ATDP — dial
      if (remaining.startsWith("DT") || remaining.startsWith("DP")) {
        const target = line.substring(line.toUpperCase().indexOf(remaining.startsWith("DT") ? "DT" : "DP") + 4);
        this.dial(target.trim());
        return;
      }

      // ATS registers
      if (remaining.startsWith("S")) {
        const sMatch = remaining.match(/^S(\d+)(=(\d+)|\?)/);
        if (sMatch) {
          const reg = parseInt(sMatch[1]);
          if (sMatch[2] === "?") {
            this.sendResponse(String(this.sRegisters[reg] || 0));
            i += sMatch[0].length;
            continue;
          } else {
            this.sRegisters[reg] = parseInt(sMatch[3]);
            i += sMatch[0].length;
            continue;
          }
        }
        i++;
        continue;
      }

      // Skip unknown characters (permissive)
      i++;
    }

    this.sendResponse("OK");
  }

  /**
   * Send a response string back to the SSC
   */
  sendResponse(text) {
    const cr = this.sRegisters[3];
    const lf = this.sRegisters[4];

    // Map verbal to numeric codes if verbal mode is off
    if (!this.verbal) {
      const codes = {
        "OK": "0",
        "CONNECT": "1",
        "RING": "2",
        "NO CARRIER": "3",
        "ERROR": "4",
        "CONNECT 2400": "10",
        "NO DIALTONE": "6",
        "BUSY": "7",
      };
      text = codes[text] || text;
    }

    // Send CR/LF + text + CR/LF
    this.wasmModule._serialReceive(cr);
    this.wasmModule._serialReceive(lf);
    for (let i = 0; i < text.length; i++) {
      this.wasmModule._serialReceive(text.charCodeAt(i));
    }
    this.wasmModule._serialReceive(cr);
    this.wasmModule._serialReceive(lf);
  }

  /**
   * Dial a host:port
   */
  dial(target) {
    let host, port;

    if (target.includes(":")) {
      const parts = target.split(":");
      host = parts[0];
      port = parseInt(parts[1]) || 23;
    } else {
      host = target;
      port = 23;
    }

    if (!host) {
      this.sendResponse("ERROR");
      return;
    }

    // Save original callbacks so we can restore after connect attempt
    const origStatus = this.serialManager.onStatusChange;
    const origError = this.serialManager.onError;

    this.serialManager.onStatusChange = (status) => {
      if (status === "connected") {
        this.mode = MODE_DATA;
        this.lastDataTime = Date.now();
        this.sendResponse("CONNECT 2400");
        if (this.onStatusChange) this.onStatusChange("connected", host, port);
      } else if (status === "disconnected") {
        if (this.mode === MODE_DATA) {
          this.mode = MODE_COMMAND;
          this.sendResponse("NO CARRIER");
          if (this.onStatusChange) this.onStatusChange("disconnected");
        }
      }

      // Also forward to any external listener
      if (origStatus) origStatus(status);
    };

    this.serialManager.onError = (err) => {
      this.sendResponse("NO CARRIER");
      if (this.onStatusChange) this.onStatusChange("error");
      if (origError) origError(err);
    };

    this.serialManager.connectProxy(host, port);
  }

  /**
   * Hang up the connection
   */
  hangup() {
    if (this.serialManager.isConnected()) {
      this.serialManager.disconnect();
    }
    this.mode = MODE_COMMAND;
    this.sendResponse("NO CARRIER");
    if (this.onStatusChange) this.onStatusChange("disconnected");
  }

  /**
   * Reset modem to defaults
   */
  reset() {
    if (this.serialManager.isConnected()) {
      this.serialManager.disconnect();
    }
    this.mode = MODE_COMMAND;
    this.echo = true;
    this.verbal = true;
    this.commandBuffer = "";
    this.sRegisters = { ...DEFAULT_S_REGISTERS };
    this.plusCount = 0;
    this.lastDataTime = 0;
    if (this.plusTimer) {
      clearTimeout(this.plusTimer);
      this.plusTimer = null;
    }
  }

  /**
   * Check if modem is in data mode (connected)
   */
  isConnected() {
    return this.mode === MODE_DATA && this.serialManager.isConnected();
  }
}
