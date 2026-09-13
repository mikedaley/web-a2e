/*
 * zero-page-watch-window.js - Zero page watch window for monitoring memory locations
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { machineProcessor } from "../machine/machine-profile.js";
import { BaseWindow } from "../windows/base-window.js";
import { showToast } from "../ui/toast.js";

// Predefined watches for common Apple II zero page locations
const PREDEFINED_WATCHES = {
  "BASIC Pointers": [
    { addr: 0x67, label: "TXTTAB", size: 16, desc: "Start of BASIC program" },
    { addr: 0x69, label: "VARTAB", size: 16, desc: "Start of variables" },
    { addr: 0x6b, label: "ARYTAB", size: 16, desc: "Start of arrays" },
    { addr: 0x6d, label: "STREND", size: 16, desc: "End of strings" },
    { addr: 0x6f, label: "FRETOP", size: 16, desc: "Top of string space" },
    { addr: 0x73, label: "MEMSIZ", size: 16, desc: "Top of memory" },
    { addr: 0x75, label: "CURLIN", size: 16, desc: "Current BASIC line" },
    { addr: 0xb8, label: "TXTPTR", size: 16, desc: "BASIC text pointer" },
  ],
  "Screen/Window": [
    { addr: 0x20, label: "WNDLFT", size: 8, desc: "Window left edge" },
    { addr: 0x21, label: "WNDWDTH", size: 8, desc: "Window width" },
    { addr: 0x22, label: "WNDTOP", size: 8, desc: "Window top edge" },
    { addr: 0x23, label: "WNDBTM", size: 8, desc: "Window bottom" },
    { addr: 0x24, label: "CH", size: 8, desc: "Cursor horizontal" },
    { addr: 0x25, label: "CV", size: 8, desc: "Cursor vertical" },
    { addr: 0x28, label: "BASL", size: 16, desc: "Text base address" },
    { addr: 0x2a, label: "BAS2L", size: 16, desc: "Scroll line base" },
  ],
  Graphics: [
    { addr: 0x26, label: "GBASL", size: 16, desc: "Graphics base address" },
    { addr: 0x30, label: "COLOR", size: 8, desc: "LoRes color" },
    { addr: 0xe0, label: "HCOLOR1", size: 8, desc: "HiRes color 1" },
    { addr: 0xe4, label: "HGRX", size: 16, desc: "HGR X coordinate" },
    { addr: 0xe6, label: "HGRY", size: 8, desc: "HGR Y coordinate" },
  ],
  "DOS 3.3": [
    { addr: 0xaa, label: "DOSSLOT", size: 8, desc: "DOS slot" },
    { addr: 0xab, label: "DOSDRIVE", size: 8, desc: "DOS drive" },
    { addr: 0xac, label: "FILTYP", size: 8, desc: "File type" },
  ],
  System: [
    { addr: 0x00, label: "LOC0", size: 16, desc: "General use" },
    { addr: 0x02, label: "LOC2", size: 16, desc: "General use" },
    { addr: 0x36, label: "CSWL", size: 16, desc: "Char output hook" },
    { addr: 0x38, label: "KSWL", size: 16, desc: "Char input hook" },
    { addr: 0x3c, label: "A1L", size: 16, desc: "Monitor A1" },
    { addr: 0x3e, label: "A2L", size: 16, desc: "Monitor A2" },
    { addr: 0x42, label: "A4L", size: 16, desc: "Monitor A4" },
    { addr: 0x45, label: "ACC", size: 8, desc: "Accumulator save" },
    { addr: 0x46, label: "XREG", size: 8, desc: "X register save" },
    { addr: 0x47, label: "YREG", size: 8, desc: "Y register save" },
    { addr: 0x48, label: "STATUS", size: 8, desc: "Status save" },
  ],
};

export class ZeroPageWatchWindow extends BaseWindow {
  constructor(wasmModule) {
    super({
      id: "zeropage-watch",
      title: "Zero Page Watch",
      defaultWidth: 400,
      defaultHeight: 450,
      minWidth: 400,
      minHeight: 300,
      maxWidth: 400,
    });
    this.wasmModule = wasmModule;
    this.customWatches = this.loadCustomWatches();
    this.previousValues = new Map();
    this.changedAddresses = new Set();
    this.changeTimestamps = new Map();
    this.expandedGroups = new Set(["BASIC Pointers", "Screen/Window"]);
  }

  loadCustomWatches() {
    try {
      const saved = localStorage.getItem("zp-custom-watches");
      return saved ? JSON.parse(saved) : [];
    } catch (e) {
      console.warn("Failed to load custom watches:", e.message);
      return [];
    }
  }

  saveCustomWatches() {
    try {
      localStorage.setItem(
        "zp-custom-watches",
        JSON.stringify(this.customWatches),
      );
    } catch (e) {
      console.warn("Failed to save custom watches:", e.message);
    }
  }

  getState() {
    const base = super.getState();
    base.expandedGroups = [...this.expandedGroups];
    return base;
  }

  restoreState(state) {
    if (state.expandedGroups) {
      this.expandedGroups = new Set(state.expandedGroups);
    }
    super.restoreState(state);
  }

  renderContent() {
    return `
      <div class="zp-toolbar">
        <button class="zp-add-btn" title="Add custom watch">+ Add Watch</button>
        <!-- On a 65816 the zero page is wherever the direct page register
             points, so these absolute addresses are only the processor's zero
             page while it reads zero. The software these watches are for
             (Applesoft, DOS, the monitor) runs with it at zero; a program
             that moved it is reaching somewhere else entirely, and this says
             so rather than letting the values be read as its own. -->
        <span class="zp-direct-page" id="zp-direct-page" hidden></span>
      </div>
      <div class="zp-groups"></div>
    `;
  }

  onMachineChanged() {
    const el = this.contentElement?.querySelector("#zp-direct-page");
    if (el) el.hidden = !machineProcessor().hasDirectPage;
  }

  onContentRendered() {
    this.groupsDiv = this.contentElement.querySelector(".zp-groups");
    this.onMachineChanged();

    this.contentElement
      .querySelector(".zp-add-btn")
      .addEventListener("click", () => {
        this.addCustomWatch();
      });

    this.renderGroups();
  }

  renderGroups() {
    let html = "";

    // Render predefined groups
    for (const [groupName, watches] of Object.entries(PREDEFINED_WATCHES)) {
      const isExpanded = this.expandedGroups.has(groupName);
      html += `
        <div class="zp-group">
          <div class="zp-group-header" data-group="${groupName}">
            <span class="zp-expand-icon">${isExpanded ? "▼" : "▶"}</span>
            <span class="zp-group-name">${groupName}</span>
          </div>
          <div class="zp-group-content ${isExpanded ? "" : "collapsed"}">
            ${watches.map((w) => this.renderWatch(w)).join("")}
          </div>
        </div>
      `;
    }

    // Render custom watches group if any exist
    if (this.customWatches.length > 0) {
      const isExpanded = this.expandedGroups.has("Custom");
      html += `
        <div class="zp-group">
          <div class="zp-group-header" data-group="Custom">
            <span class="zp-expand-icon">${isExpanded ? "▼" : "▶"}</span>
            <span class="zp-group-name">Custom Watches</span>
          </div>
          <div class="zp-group-content ${isExpanded ? "" : "collapsed"}">
            ${this.customWatches.map((w, i) => this.renderWatch(w, i)).join("")}
          </div>
        </div>
      `;
    }

    this.groupsDiv.innerHTML = html;

    // Add event listeners for group headers
    this.groupsDiv.querySelectorAll(".zp-group-header").forEach((header) => {
      header.addEventListener("click", () => {
        const groupName = header.dataset.group;
        if (this.expandedGroups.has(groupName)) {
          this.expandedGroups.delete(groupName);
        } else {
          this.expandedGroups.add(groupName);
        }
        this.renderGroups();
        if (this.onStateChange) this.onStateChange();
      });
    });

    // Add event listeners for removing custom watches
    this.groupsDiv.querySelectorAll(".zp-remove-btn").forEach((btn) => {
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        const index = parseInt(btn.dataset.index, 10);
        this.customWatches.splice(index, 1);
        this.saveCustomWatches();
        this.renderGroups();
      });
    });
  }

  renderWatch(watch, customIndex = null) {
    const isCustom = customIndex !== null;
    const sizeIndicator = watch.size === 16 ? "16" : "8";
    return `
      <div class="zp-watch" data-addr="${watch.addr}" data-size="${watch.size}">
        <span class="zp-addr">$${this.formatHex(watch.addr, 2)}</span>
        <span class="zp-label">${watch.label}</span>
        <span class="zp-size">${sizeIndicator}b</span>
        <span class="zp-value" data-addr="${watch.addr}" data-size="${watch.size}">----</span>
        <span class="zp-desc">${watch.desc || ""}</span>
        ${isCustom ? `<button class="zp-remove-btn" data-index="${customIndex}" title="Remove">×</button>` : ""}
      </div>
    `;
  }

  addCustomWatch() {
    const addrStr = prompt("Enter zero page address (hex, 00-FF):", "00");
    if (!addrStr) return;

    const addr = parseInt(addrStr, 16);
    if (isNaN(addr) || addr < 0 || addr > 0xff) {
      showToast("Invalid address. Must be 00-FF.", "error");
      return;
    }

    const label = prompt(
      "Enter label for this watch:",
      `$${this.formatHex(addr, 2)}`,
    );
    if (!label) return;

    const sizeStr = prompt("Size (8 or 16 bits):", "8");
    const size = sizeStr === "16" ? 16 : 8;

    this.customWatches.push({
      addr,
      label,
      size,
      desc: "Custom watch",
    });

    this.expandedGroups.add("Custom");
    this.saveCustomWatches();
    this.renderGroups();
  }

  async update(wasmModule) {
    if (!this.isVisible || !this.groupsDiv) return;

    const now = Date.now();
    const fadeTime = 1000;

    // Collect all addresses we need to read
    const valueSpans = this.groupsDiv.querySelectorAll(".zp-value");
    const addrsToRead = new Set();
    for (const valueSpan of valueSpans) {
      const addr = parseInt(valueSpan.dataset.addr, 10);
      const size = parseInt(valueSpan.dataset.size, 10);
      addrsToRead.add(addr);
      if (size === 16) addrsToRead.add((addr + 1) & 0xff);
    }

    // Batch read all addresses. They are absolute bank-zero addresses, which
    // is where the software these watches describe keeps its variables.
    const addrList = [...addrsToRead];
    const wantsDirectPage = machineProcessor().hasDirectPage;
    const batchCalls = addrList.map(a => ['_peekMemory', a]);
    if (wantsDirectPage) batchCalls.push(['_getDirectPage']);
    const results = await wasmModule.batch(batchCalls);
    if (wantsDirectPage) {
      const d = results[addrList.length];
      const el = this.contentElement.querySelector("#zp-direct-page");
      if (el) {
        el.hidden = false;
        el.textContent = `D=$${this.formatHex(d, 4)}`;
        // A moved direct page means the processor's zero page is not this one.
        el.classList.toggle("moved", d !== 0);
        el.title =
          d === 0
            ? "The direct page register is zero, so these are the processor's zero page"
            : "The direct page register has been moved: the processor's zero page is elsewhere";
      }
    }
    const memValues = new Map();
    for (let i = 0; i < addrList.length; i++) {
      memValues.set(addrList[i], results[i]);
    }

    // Update all value displays
    for (const valueSpan of valueSpans) {
      const addr = parseInt(valueSpan.dataset.addr, 10);
      const size = parseInt(valueSpan.dataset.size, 10);

      let value;
      if (size === 16) {
        const low = memValues.get(addr);
        const high = memValues.get((addr + 1) & 0xff);
        value = (high << 8) | low;
      } else {
        value = memValues.get(addr);
      }

      const prevValue = this.previousValues.get(addr);

      // Detect changes
      if (prevValue !== undefined && prevValue !== value) {
        this.changeTimestamps.set(addr, now);
      }
      this.previousValues.set(addr, value);

      // Format value
      const digits = size === 16 ? 4 : 2;
      let displayValue = `$${this.formatHex(value, digits)}`;

      // Add decimal value for small numbers
      if (value < 256) {
        displayValue += ` (${value})`;
      }

      valueSpan.textContent = displayValue;

      // Highlight recent changes
      const watchDiv = valueSpan.closest(".zp-watch");
      if (this.changeTimestamps.has(addr)) {
        const elapsed = now - this.changeTimestamps.get(addr);
        if (elapsed < fadeTime) {
          watchDiv.classList.add("changed");
        } else {
          watchDiv.classList.remove("changed");
          this.changeTimestamps.delete(addr);
        }
      } else {
        watchDiv.classList.remove("changed");
      }
    }
  }
}
