/*
 * trace-panel.js - CPU instruction execution trace panel
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import { machineProcessor } from "../machine/machine-profile.js";

/**
 * TracePanelWindow - Displays instruction trace from the WASM ring buffer
 * with virtual scrolling for performance.
 */
export class TracePanelWindow extends BaseWindow {
  constructor(wasmModule) {
    super({
      id: "trace-panel",
      title: "Instruction Trace",
      minWidth: 480,
      minHeight: 300,
      defaultWidth: 680,
      defaultHeight: 400,
    });

    this.wasmModule = wasmModule;
    this.ROW_HEIGHT = 14;
    this.scrollTop = 0;
    this.filterAddr = null; // Optional filter by address range
    this.recording = false;
  }

  renderContent() {
    return `
      <div class="trace-panel-content">
        <div class="trace-toolbar">
          <label class="trace-toggle">
            <input type="checkbox" id="trace-enabled">
            <span>Record</span>
          </label>
          <button class="trace-clear-btn" id="trace-clear">Clear</button>
          <span class="trace-count" id="trace-count">0 entries</span>
        </div>
        <div class="trace-header">
          <span class="trace-col-cycle">Cycle</span>
          <span class="trace-col-pc">PC</span>
          <span class="trace-col-bytes">Bytes</span>
          <span class="trace-col-mnemonic">Mnem</span>
          <span class="trace-col-operand">Operand</span>
          <span class="trace-col-regs" id="trace-regs-legend">A  X  Y  SP NV-BDIZC</span>
        </div>
        <div class="trace-scroll-container" id="trace-scroll">
          <div class="trace-scroll-spacer" id="trace-spacer"></div>
          <div class="trace-rows" id="trace-rows"></div>
        </div>
      </div>
    `;
  }

  /**
   * The register legend, which names the flags this processor has.
   *
   * A 65816's two width flags sit where a 6502's unused bit and Break are,
   * and its registers are twice as wide, so the columns are wider too.
   */
  applyProcessor() {
    const legend = this.contentElement?.querySelector("#trace-regs-legend");
    if (!legend) return;
    const processor = machineProcessor();
    const wide = processor.registerBits > 8;
    const pad = wide ? "    " : "  ";
    legend.textContent =
      `A${pad}X${pad}Y${pad}SP ` + (processor.hasModes ? "NVmxDIZC" : "NV-BDIZC");
  }

  /** The machine changed, and with it the processor the columns describe. */
  onMachineChanged() {
    this.applyProcessor();
    this.renderVisibleRows();
  }

  setupContentEventListeners() {
    const enabledCheck = this.contentElement.querySelector("#trace-enabled");
    if (enabledCheck) {
      enabledCheck.addEventListener("change", () => {
        this.recording = enabledCheck.checked;
        if (this.wasmModule._setTraceEnabled) {
          this.wasmModule._setTraceEnabled(enabledCheck.checked);
        }
      });
    }

    const clearBtn = this.contentElement.querySelector("#trace-clear");
    if (clearBtn) {
      clearBtn.addEventListener("click", () => {
        if (this.wasmModule._clearTrace) {
          this.wasmModule._clearTrace();
          this.renderVisibleRows();
        }
      });
    }

    const scrollContainer = this.contentElement.querySelector("#trace-scroll");
    if (scrollContainer) {
      scrollContainer.addEventListener("scroll", () => {
        this.scrollTop = scrollContainer.scrollTop;
        this.renderVisibleRows();
      });
    }
  }

  create() {
    super.create();
    this.applyProcessor();
    this.setupContentEventListeners();
  }

  async update(wasmModule) {
    this.wasmModule = wasmModule;

    const countEl = this.contentElement.querySelector("#trace-count");
    if (countEl && this.wasmModule._getTraceCount) {
      const count = await this.wasmModule._getTraceCount();
      countEl.textContent = `${count} entries`;

      // Auto-scroll to latest entry when recording
      if (this.recording && count > 0) {
        const container = this.contentElement.querySelector("#trace-scroll");
        if (container) {
          const totalHeight = count * this.ROW_HEIGHT;
          container.scrollTop = totalHeight - container.clientHeight;
          this.scrollTop = container.scrollTop;
        }
      }
    }

    await this.renderVisibleRows();
  }

  async renderVisibleRows() {
    const container = this.contentElement.querySelector("#trace-scroll");
    const spacer = this.contentElement.querySelector("#trace-spacer");
    const rowsEl = this.contentElement.querySelector("#trace-rows");
    if (!container || !spacer || !rowsEl) return;
    if (!this.wasmModule._getTraceCount) return;

    const count = await this.wasmModule._getTraceCount();
    if (!count) {
      spacer.style.height = "0px";
      rowsEl.innerHTML = '<div class="trace-empty">No trace data</div>';
      return;
    }

    spacer.style.height = count * this.ROW_HEIGHT + "px";

    const containerHeight = container.clientHeight;
    const firstVisible = Math.floor(this.scrollTop / this.ROW_HEIGHT);
    const visibleCount = Math.ceil(containerHeight / this.ROW_HEIGHT) + 1;
    const startIdx = Math.max(0, firstVisible);
    const endIdx = Math.min(count, startIdx + visibleCount);
    if (endIdx <= startIdx) {
      rowsEl.innerHTML = "";
      return;
    }

    // One round trip for the whole visible window, already formatted. This
    // used to be one heap read per row plus a copy of the opcode table, the
    // addressing modes and the operand syntax in this file — a second
    // formatter to be wrong, and one that only knew the 65C02.
    const blob = await this.wasmModule.callString(
      "_formatTraceRange",
      startIdx,
      endIdx - startIdx,
    );

    rowsEl.style.transform = `translateY(${startIdx * this.ROW_HEIGHT}px)`;
    let html = "";
    for (const line of blob ? blob.split("\n") : []) {
      const f = line.split("\t");
      if (f.length < 10) continue;
      const [cycle, address, bytes, text, a, x, y, sp, p, widths] = f;
      const space = text.indexOf(" ");
      const mnemonic = space >= 0 ? text.slice(0, space) : text;
      const operand = space >= 0 ? text.slice(space + 1) : "";

      html +=
        `<div class="trace-row">` +
        `<span class="trace-col-cycle">${cycle}</span>` +
        `<span class="trace-col-pc">${address}</span>` +
        `<span class="trace-col-bytes">${bytes.padEnd(11)}</span>` +
        `<span class="trace-col-mnemonic">${mnemonic}</span>` +
        `<span class="trace-col-operand">${operand}</span>` +
        `<span class="trace-col-regs">${a} ${x} ${y} ${sp} ` +
        `${this.flagsStr(parseInt(p, 16), parseInt(widths, 16))}</span>` +
        `</div>`;
    }
    rowsEl.innerHTML = html;
  }

  hex2(v) { return v.toString(16).toUpperCase().padStart(2, "0"); }
  hex4(v) { return v.toString(16).toUpperCase().padStart(4, "0"); }

  /**
   * The status register as letters.
   *
   * Bits 5 and 4 are the unused bit and Break on a 6502, and the accumulator
   * and index widths on a 65816 in native mode — which the trace records per
   * instruction, because it is what decides how long an immediate was.
   */
  flagsStr(p, widths) {
    const native = widths !== undefined && (widths & 0x01) === 0;
    return (
      ((p & 0x80) ? "N" : ".") +
      ((p & 0x40) ? "V" : ".") +
      (native ? ((p & 0x20) ? "M" : ".") : "-") +
      ((p & 0x10) ? (native ? "X" : "B") : ".") +
      ((p & 0x08) ? "D" : ".") +
      ((p & 0x04) ? "I" : ".") +
      ((p & 0x02) ? "Z" : ".") +
      ((p & 0x01) ? "C" : ".")
    );
  }
}
