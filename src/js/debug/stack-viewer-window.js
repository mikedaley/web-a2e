/*
 * stack-viewer-window.js - Stack viewer debug window with live stack contents
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import {
  machineProcessor,
  formatMachineAddress,
} from "../machine/machine-profile.js";

export class StackViewerWindow extends BaseWindow {
  constructor(wasmModule) {
    super({
      id: "stack-viewer",
      title: "Stack Viewer",
      defaultWidth: 280,
      defaultHeight: 400,
      minWidth: 280,
      minHeight: 250,
      maxWidth: 280,
    });
    this.wasmModule = wasmModule;
    this.previousSP = 0xff;
    // The most stack this shows at once. A 6502's stack is one page and can
    // never be deeper; a 65816's pointer is sixteen bits and can sit anywhere
    // in bank zero, so the view is bounded rather than running to a base
    // address that the program is free to have moved.
    this.MAX_ENTRIES = 256;
    this.returnAddresses = new Set(); // Track likely return addresses
  }

  renderContent() {
    return `
      <div class="stack-info">
        <span class="stack-sp-label">SP:</span>
        <span class="stack-sp-value">$FF</span>
        <span class="stack-depth-label">Depth:</span>
        <span class="stack-depth-value">0</span>
      </div>
      <div class="stack-depth-bar">
        <div class="stack-depth-fill"></div>
      </div>
      <div class="stack-call-stack" id="call-stack"></div>
      <div class="stack-header">
        <span class="stack-col-addr">Addr</span>
        <span class="stack-col-value">Value</span>
        <span class="stack-col-info">Info</span>
      </div>
      <div class="stack-content"></div>
    `;
  }

  onContentRendered() {
    this.spValueSpan = this.contentElement.querySelector(".stack-sp-value");
    this.depthValueSpan =
      this.contentElement.querySelector(".stack-depth-value");
    this.depthFill = this.contentElement.querySelector(".stack-depth-fill");
    this.contentDiv = this.contentElement.querySelector(".stack-content");
  }

  /**
   * Return address arithmetic, with no I/O.
   *
   * 6502 return addresses are pushed as addr-1 (JSR pushes PC+2, which points
   * at the last byte of the JSR).
   */
  returnAddressOf(lowByte, highByte) {
    // Both processors push the address of the call's last byte, so the return
    // is one past it; a 65816's wraps inside its bank rather than overflowing.
    return (((highByte << 8) | lowByte) + 1) & 0xffff;
  }

  /**
   * The top of the stack: the highest address a push would have used.
   *
   * A 6502's stack is page one and nothing can move it, so the top is $01FF.
   * A 65816's pointer is sixteen bits and a program puts its stack where it
   * likes, so the top is taken as the top of the page the pointer is in —
   * which is $01FF for the firmware and anything else for a program that
   * relocated it. There is no way to know where a stack "began"; this is the
   * closest honest answer.
   */
  stackTop(sp) {
    if (!machineProcessor().hasBanks) return 0x01ff;
    return (sp & 0xff00) | 0xff;
  }

  /**
   * Pull the mnemonic out of an already-fetched disassembly line.
   *
   * This used to be an async method that disassembled one address per call —
   * two round-trips each, inside the render loop, once per return address on
   * the stack. A deep stack made that dozens of sequential round-trips per
   * tick. The disassembly for every return address is now fetched in one
   * batch and this just parses the result.
   */
  mnemonicOf(disasmStr) {
    const match = disasmStr && disasmStr.match(/:\s*[0-9A-F ]+\s+(\w+)/);
    return match ? match[1] : "???";
  }

  async isLikelyReturnAddress(sp, wasmModule) {
    const top = this.stackTop(sp);
    if (sp + 2 > top) return false; // Need at least two bytes above it

    const [low, high] = await wasmModule.batch([
      ['_peekMemory', sp + 1],
      ['_peekMemory', sp + 2],
    ]);
    return await wasmModule._isLikelyReturnAddress(
      this.returnAddressOf(low, high),
    );
  }

  async update(wasmModule) {
    if (!this.isVisible || !this.contentDiv) return;

    // The stack is in bank zero on both processors, but the pointer is not
    // the same thing: a 6502's is an offset into page one and a 65816's is
    // the whole sixteen-bit address. Everything below works in addresses.
    const banked = machineProcessor().hasBanks;
    const rawSP = await wasmModule._getSP();
    const sp = banked ? rawSP : 0x0100 | (rawSP & 0xff);
    const top = this.stackTop(sp);
    const fullDepth = Math.max(0, top - sp);
    const stackDepth = Math.min(fullDepth, this.MAX_ENTRIES);

    // Shown as the processor's own register: a 6502's SP is the byte, and a
    // 65816's is the whole pointer.
    this.spValueSpan.textContent = `$${this.formatHex(rawSP, banked ? 4 : 2)}`;
    this.depthValueSpan.textContent = fullDepth.toString();

    // The bar is a fraction of a page, which is all a 6502's stack can be and
    // is the page a 65816's pointer is sitting in.
    const depthPercent = Math.min(100, (fullDepth / 256) * 100);
    this.depthFill.style.width = `${depthPercent}%`;

    // Color code depth bar
    if (depthPercent > 80) {
      this.depthFill.classList.add("danger");
      this.depthFill.classList.remove("warning");
    } else if (depthPercent > 60) {
      this.depthFill.classList.add("warning");
      this.depthFill.classList.remove("danger");
    } else {
      this.depthFill.classList.remove("warning", "danger");
    }

    // Batch-read the stack, highest address first.
    const addresses = [];
    for (let i = 0; i < stackDepth; i++) addresses.push(top - i);
    const stackBytes = addresses.length
      ? await wasmModule.batch(addresses.map((a) => ['_peekMemory', a]))
      : [];
    const byteAt = (address) => stackBytes[top - address];

    // Which pairs look like a return address a call left behind.
    //
    // The test is the same on both processors and is a heuristic either way:
    // the bytes point somewhere that could be code, and the instruction three
    // bytes before it is a JSR — or, on a 65816, four bytes before it is a
    // JSL, which pushes three bytes rather than two.
    const candidates = [];
    for (let address = top; address > sp + 1; address--) {
      const low = byteAt(address - 1);
      const high = byteAt(address);
      if (low === undefined || high === undefined) continue;
      const retAddr = this.returnAddressOf(low, high);
      // Somewhere that could plausibly hold code rather than data.
      const plausible = banked
        ? retAddr >= 0x0100
        : (retAddr >= 0x0800 && retAddr < 0xc000) ||
          (retAddr >= 0xd000 && retAddr <= 0xffff);
      if (!plausible) continue;
      candidates.push({ address, retAddr });
    }

    const callChecks = [];
    for (const c of candidates) {
      callChecks.push(['_peekMemory', (c.retAddr - 3) & 0xffff]);
      if (banked) callChecks.push(['_peekMemory', (c.retAddr - 4) & 0xffff]);
    }
    const callResults = callChecks.length
      ? await wasmModule.batch(callChecks)
      : [];
    const perCandidate = banked ? 2 : 1;
    const returns = new Map();
    candidates.forEach((c, n) => {
      const jsr = callResults[n * perCandidate] === 0x20;
      const jsl = banked && callResults[n * perCandidate + 1] === 0x22;
      if (jsr || jsl) returns.set(c.address, { retAddr: c.retAddr, long: jsl });
    });

    // Disassemble every detected return address in ONE batch, rather than two
    // round-trips per address from inside the render loop below.
    const entries = [...returns.entries()];
    const disasmResults = entries.length
      ? await wasmModule.batch(
          entries.map(([, r]) => ['__callString', '_disassembleAt', r.retAddr]),
        )
      : [];
    const mnemonicByAddress = new Map();
    entries.forEach(([address], n) => {
      mnemonicByAddress.set(address, this.mnemonicOf(disasmResults[n]));
    });

    // Build the row data, then apply it in place — see _renderRows().
    const rows = [];
    let skipReturnAddr = false;

    for (let address = top; address > sp; address--) {
      const value = byteAt(address);
      if (value === undefined) break;
      const isSP = address === sp + 1; // Current top of stack
      const found = returns.get(address);

      const classes = ["stack-entry"];
      if (isSP) classes.push("stack-top");
      if (skipReturnAddr) {
        classes.push("return-addr-low");
        skipReturnAddr = false;
      } else if (found) {
        classes.push("return-addr-high");
        skipReturnAddr = true;
      }

      let infoStr = "";
      if (found) {
        // A long call's return carries a bank; a short one returns into
        // whichever bank the code was already running in, which the stack
        // does not record.
        infoStr =
          `→ $${this.formatHex(found.retAddr, 4)} ` +
          `(${mnemonicByAddress.get(address)})` +
          (found.long ? " long" : "");
      } else if (value >= 0x20 && value < 0x7f) {
        infoStr = `'${String.fromCharCode(value)}'`;
      }

      rows.push({
        className: classes.join(" "),
        addr: `$${this.formatHex(address, 4)}`,
        value: `$${this.formatHex(value, 2)}`,
        info: infoStr,
      });
    }

    this._renderRows(rows);
    this.previousSP = rawSP;

    // Build call stack summary
    await this.updateCallStack(wasmModule, sp);
  }

  /**
   * Apply row data to the DOM, reusing the existing elements.
   *
   * This ran as `contentDiv.innerHTML = html` on every tick, which discards and
   * re-parses every row — a full style recalc, layout and paint for the window
   * (and, for a floating window, a backdrop re-blur) 15 times a second even
   * when a single byte changed. Rows are now created only when the stack depth
   * changes, and their text is written only where it differs.
   */
  _renderRows(rows) {
    if (!this._rowPool) this._rowPool = [];
    const pool = this._rowPool;

    if (rows.length === 0) {
      if (this.contentDiv.firstElementChild?.className !== "stack-empty") {
        this.contentDiv.replaceChildren();
        const empty = document.createElement("div");
        empty.className = "stack-empty";
        empty.textContent = "Stack is empty";
        this.contentDiv.appendChild(empty);
        pool.length = 0;
      }
      return;
    }

    // Depth changed — rebuild the pool. Cheap relative to doing it every tick.
    if (pool.length !== rows.length) {
      pool.length = 0;
      const fragment = document.createDocumentFragment();
      for (let i = 0; i < rows.length; i++) {
        const row = document.createElement("div");
        const addrSpan = document.createElement("span");
        addrSpan.className = "stack-addr";
        const valueSpan = document.createElement("span");
        valueSpan.className = "stack-value";
        const infoSpan = document.createElement("span");
        infoSpan.className = "stack-info-text";
        row.append(addrSpan, valueSpan, infoSpan);
        fragment.appendChild(row);
        pool.push({ row, addrSpan, valueSpan, infoSpan, last: {} });
      }
      this.contentDiv.replaceChildren(fragment);
    }

    for (let i = 0; i < rows.length; i++) {
      const data = rows[i];
      const el = pool[i];
      const last = el.last;
      if (last.className !== data.className) {
        el.row.className = data.className;
        last.className = data.className;
      }
      if (last.addr !== data.addr) {
        el.addrSpan.textContent = data.addr;
        last.addr = data.addr;
      }
      if (last.value !== data.value) {
        el.valueSpan.textContent = data.value;
        last.value = data.value;
      }
      if (last.info !== data.info) {
        el.infoSpan.textContent = data.info;
        last.info = data.info;
      }
    }
  }

  /**
   * Build a call stack summary by walking the stack for return addresses.
   * Display: current_PC → caller → caller → ...
   */
  async updateCallStack(wasmModule, sp) {
    const callStackEl = this.contentElement.querySelector("#call-stack");
    if (!callStackEl) return;

    const [pc, count] = await wasmModule.batch([
      ['_getPC'],
      ['_getCallStack'],
    ]);

    // The call stack is tracked by the //e's emulator as it executes JSRs;
    // the IIgs machine does not keep one, so it reports none and the summary
    // line stays empty rather than showing a //e's.
    if (!count) {
      callStackEl.innerHTML = "";
      return;
    }

    // Read packed CallStackEntry structs (4 bytes each: uint16_t returnAddr, uint16_t jsrTarget)
    const bufPtr = await wasmModule._getCallStackBuffer();
    const heap = await wasmModule.heapRead(bufPtr, count * 4);

    let stackHtml = '<span class="call-stack-label">Call:</span> ';
    stackHtml += `<span class="call-stack-addr">$${formatMachineAddress(pc)}</span>`;

    for (let i = 0; i < count; i++) {
      const offset = i * 4;
      const retAddr = heap[offset] | (heap[offset + 1] << 8);
      const jsrTarget = heap[offset + 2] | (heap[offset + 3] << 8);
      stackHtml += ` ← <span class="call-stack-addr" title="Returns to $${this.formatHex(retAddr, 4)}">$${this.formatHex(jsrTarget, 4)}</span>`;
    }

    callStackEl.innerHTML = stackHtml;
  }
}
