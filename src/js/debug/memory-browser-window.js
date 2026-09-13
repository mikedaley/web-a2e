/*
 * memory-browser-window.js - Scrollable hex/ASCII memory browser for 64KB address space
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import {
  machineProcessor,
  formatMachineAddress,
} from "../machine/machine-profile.js";

// What is where in a bank the //e's memory map describes: bank 0 on a //e,
// and the Mega II's two banks on a IIgs, which is the //e inside it.
const MEMORY_REGIONS = [
  { name: "Zero Page", start: 0x0000, end: 0x00ff },
  { name: "Stack", start: 0x0100, end: 0x01ff },
  { name: "Input Buffer", start: 0x0200, end: 0x02ff },
  { name: "Vectors/Data", start: 0x0300, end: 0x03ff },
  { name: "Text Page 1", start: 0x0400, end: 0x07ff },
  { name: "Text Page 2", start: 0x0800, end: 0x0bff },
  { name: "Free RAM", start: 0x0c00, end: 0x1fff },
  { name: "HiRes Page 1", start: 0x2000, end: 0x3fff },
  { name: "HiRes Page 2", start: 0x4000, end: 0x5fff },
  { name: "Free RAM", start: 0x6000, end: 0x95ff },
  { name: "DOS 3.3", start: 0x9600, end: 0xbfff },
  { name: "I/O Space", start: 0xc000, end: 0xc0ff },
  { name: "Slot ROMs", start: 0xc100, end: 0xcfff },
  { name: "ROM/LC RAM", start: 0xd000, end: 0xffff },
];

// What is where in one of a IIgs's own banks. Fast RAM is a program's to use
// as it likes, so only the parts the machine itself defines are named — and
// the auxiliary side of the Mega II is where Super Hi-Res lives.
const IIGS_AUX_REGIONS = [
  { name: "Zero Page", start: 0x0000, end: 0x00ff },
  { name: "Stack", start: 0x0100, end: 0x01ff },
  { name: "Text Page 1", start: 0x0400, end: 0x07ff },
  { name: "Super Hi-Res pixels", start: 0x2000, end: 0x9cff },
  { name: "Scan-line control bytes", start: 0x9d00, end: 0x9dff },
  { name: "Super Hi-Res palettes", start: 0x9e00, end: 0x9fff },
  { name: "Free RAM", start: 0xa000, end: 0xbfff },
  { name: "I/O Space", start: 0xc000, end: 0xc0ff },
  { name: "Slot ROMs", start: 0xc100, end: 0xcfff },
  { name: "Language Card", start: 0xd000, end: 0xffff },
];

// Where a IIgs keeps the things worth jumping to, which is not where a //e
// keeps them: the text page and Super Hi-Res are the Mega II's banks, and the
// ROM is at the top of the address space rather than at $D000.
const IIGS_QUICK_JUMPS = [
  { label: "ZP", bank: 0x00, addr: 0x0000, title: "Bank 0 zero page" },
  { label: "Stack", bank: 0x00, addr: 0x0100, title: "Bank 0 stack" },
  { label: "Text1", bank: 0xe0, addr: 0x0400, title: "Text Page 1 ($E0/0400)" },
  { label: "Text aux", bank: 0xe1, addr: 0x0400, title: "80-column text ($E1/0400)" },
  { label: "HiRes1", bank: 0xe0, addr: 0x2000, title: "HiRes Page 1 ($E0/2000)" },
  { label: "SHR", bank: 0xe1, addr: 0x2000, title: "Super Hi-Res pixels ($E1/2000)" },
  { label: "SCB", bank: 0xe1, addr: 0x9d00, title: "Scan-line control bytes ($E1/9D00)" },
  { label: "Palettes", bank: 0xe1, addr: 0x9e00, title: "Super Hi-Res palettes ($E1/9E00)" },
  { label: "I/O", bank: 0xe0, addr: 0xc000, title: "I/O space ($E0/C000)" },
  { label: "ROM", bank: 0xff, addr: 0x0000, title: "ROM ($FF/0000)" },
];

// Quick jump buttons
const QUICK_JUMPS = [
  { label: "ZP", addr: 0x0000, title: "Zero Page ($0000)" },
  { label: "Stack", addr: 0x0100, title: "Stack ($0100)" },
  { label: "Text1", addr: 0x0400, title: "Text Page 1 ($0400)" },
  { label: "Text2", addr: 0x0800, title: "Text Page 2 ($0800)" },
  { label: "HiRes1", addr: 0x2000, title: "HiRes Page 1 ($2000)" },
  { label: "HiRes2", addr: 0x4000, title: "HiRes Page 2 ($4000)" },
  { label: "I/O", addr: 0xc000, title: "I/O Space ($C000)" },
  { label: "ROM", addr: 0xd000, title: "ROM / Language Card ($D000)" },
  { label: "Vectors", addr: 0xfff0, title: "Vectors ($FFF0)" },
  { label: "DOS", addr: 0x9600, title: "DOS 3.3 ($9600)" },
];

export class MemoryBrowserWindow extends BaseWindow {
  constructor(wasmModule) {
    super({
      id: "memory-browser",
      title: "Memory Browser",
      defaultWidth: 600,
      defaultHeight: 520,
      minWidth: 600,
      minHeight: 300,
      maxWidth: 600,
    });
    this.wasmModule = wasmModule;
    // Which bank is being browsed, and where in it. A view of 16MB scrolled as
    // one run would be unusable and is not how the machine is thought about:
    // a 65816 program lives in a bank, so the view browses one at a time.
    this.bank = 0;
    this.banks = [{ bank: 0, name: "Main" }];
    this.baseAddress = 0x0000;
    this.bytesPerRow = 16;
    this.visibleRows = 28;
    this.previousMemory = new Uint8Array(65536);
    this.changedBytes = new Set();
    this.changeTimestamps = new Map();
  }

  renderContent() {
    return `
      <div class="mem-browser-toolbar">
        <div class="mem-browser-jumps"></div>
        <div class="mem-browser-nav">
          <select class="mem-bank-select" title="Which bank to browse" hidden></select>
          <input type="text" class="mem-addr-input" placeholder="Address" maxlength="7" />
          <button class="mem-go-btn" title="Go to address">Go</button>
          <input type="text" class="mem-search-input" placeholder="Search hex" maxlength="16" />
          <button class="mem-search-btn" title="Search for bytes">Find</button>
          <button class="mem-refresh-btn" title="Refresh memory view">Refresh</button>
        </div>
      </div>
      <div class="mem-browser-region">Region: <span class="mem-region-name">Zero Page</span></div>
      <div class="mem-browser-header">
        <span class="mem-addr-col">Addr</span>
        <span class="mem-hdr-separator">:</span>
        ${Array.from({ length: 16 }, (_, i) => `<span class="mem-hdr-byte">${i.toString(16).toUpperCase().padStart(2, "0")}</span>`).join("")}
        <span class="mem-ascii-hdr">ASCII</span>
      </div>
      <div class="mem-browser-scroll-container">
        <div class="mem-browser-content"></div>
        <div class="mem-browser-scrollbar">
          <div class="mem-scrollbar-track">
            <div class="mem-scrollbar-thumb"></div>
          </div>
        </div>
      </div>
    `;
  }

  onContentRendered() {
    this.contentDiv = this.contentElement.querySelector(".mem-browser-content");
    this.scrollContainer = this.contentElement.querySelector(
      ".mem-browser-scroll-container",
    );
    this.scrollbarThumb = this.contentElement.querySelector(
      ".mem-scrollbar-thumb",
    );
    this.regionNameSpan = this.contentElement.querySelector(".mem-region-name");
    this.addrInput = this.contentElement.querySelector(".mem-addr-input");
    this.searchInput = this.contentElement.querySelector(".mem-search-input");
    this.bankSelect = this.contentElement.querySelector(".mem-bank-select");

    this.renderQuickJumps();
    this.setupScrolling();
    this.setupContentEventListeners();
    this.loadBanks();
  }

  setupScrolling() {
    // Mouse wheel scrolling - explicitly non-passive since we need preventDefault()
    this.scrollContainer.addEventListener(
      "wheel",
      (e) => {
        e.preventDefault();
        const delta = Math.sign(e.deltaY) * this.bytesPerRow * 4;
        this.scrollToAddress(this.baseAddress + delta);
      },
      { passive: false },
    );

    // Scrollbar dragging
    let isDragging = false;
    let dragStartY = 0;
    let dragStartAddr = 0;

    this.scrollbarThumb.addEventListener("mousedown", (e) => {
      isDragging = true;
      dragStartY = e.clientY;
      dragStartAddr = this.baseAddress;
      e.preventDefault();
    });

    document.addEventListener("mousemove", (e) => {
      if (!isDragging) return;
      const trackHeight =
        this.scrollbarThumb.parentElement.offsetHeight -
        this.scrollbarThumb.offsetHeight;
      const deltaY = e.clientY - dragStartY;
      const maxAddress = 0x10000 - this.bytesPerRow * this.visibleRows;
      const newAddr = Math.round(
        dragStartAddr + (deltaY / trackHeight) * maxAddress,
      );
      this.scrollToAddress(newAddr);
    });

    document.addEventListener("mouseup", () => {
      isDragging = false;
    });

    // Track click
    this.scrollbarThumb.parentElement.addEventListener("click", (e) => {
      if (e.target === this.scrollbarThumb) return;
      const rect = this.scrollbarThumb.parentElement.getBoundingClientRect();
      const clickY = e.clientY - rect.top;
      const trackHeight = rect.height;
      const maxAddress = 0x10000 - this.bytesPerRow * this.visibleRows;
      const newAddr = Math.round((clickY / trackHeight) * maxAddress);
      this.scrollToAddress(newAddr);
    });
  }

  setupContentEventListeners() {
    // Quick jump buttons
    // Delegated, because the buttons are rebuilt when the machine changes.
    this.contentElement
      .querySelector(".mem-browser-jumps")
      ?.addEventListener("click", (e) => {
        const btn = e.target.closest(".mem-jump-btn");
        if (!btn) return;
        if (btn.dataset.bank !== undefined) {
          this.selectBank(parseInt(btn.dataset.bank, 10));
        }
        this.scrollToAddress(parseInt(btn.dataset.addr, 10));
      });

    // Go button
    this.contentElement
      .querySelector(".mem-go-btn")
      .addEventListener("click", () => {
        this.goToAddress();
      });

    // Address input enter key
    this.addrInput.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        this.goToAddress();
      }
    });

    // Search button
    this.contentElement
      .querySelector(".mem-search-btn")
      .addEventListener("click", () => {
        this.searchBytes();
      });

    // Search input enter key
    this.searchInput.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        this.searchBytes();
      }
    });

    // Bank selector
    this.bankSelect?.addEventListener("change", () => {
      this.selectBank(parseInt(this.bankSelect.value, 10));
    });

    // Refresh button
    this.contentElement
      .querySelector(".mem-refresh-btn")
      .addEventListener("click", () => {
        this.forceRefresh = true;
      });

    // Byte click to edit
    this.contentDiv.addEventListener("click", (e) => {
      const byteSpan = e.target.closest(".mem-byte");
      if (byteSpan && byteSpan.dataset.addr) {
        this.startEditByte(parseInt(byteSpan.dataset.addr, 10));
      }
    });
  }

  /**
   * The quick jumps this machine's memory deserves.
   *
   * A //e's landmarks are all in one bank; a IIgs's are spread across its
   * own — the text page and Super Hi-Res belong to the Mega II, and the ROM
   * is at the top of the address space rather than at $D000 — so each jump
   * names a bank as well as an offset.
   */
  renderQuickJumps() {
    const container = this.contentElement?.querySelector(".mem-browser-jumps");
    if (!container) return;
    const jumps = machineProcessor().hasBanks ? IIGS_QUICK_JUMPS : QUICK_JUMPS;
    container.innerHTML = jumps
      .map(
        (j) =>
          `<button class="mem-jump-btn" data-addr="${j.addr}"` +
          (j.bank === undefined ? "" : ` data-bank="${j.bank}"`) +
          ` title="${j.title}">${j.label}</button>`,
      )
      .join("");
  }

  /**
   * Ask the machine which banks it has.
   *
   * A //e has one and the selector stays hidden; a IIgs has its fast RAM, the
   * Mega II's two banks and its ROM, and how much fast RAM is the user's
   * choice — so the list comes from the machine rather than from a constant.
   */
  async loadBanks() {
    if (!this.wasmModule?.callString) return;
    try {
      const json = await this.wasmModule.callString("_getMemoryBanksJSON");
      const banks = json ? JSON.parse(json) : null;
      if (Array.isArray(banks) && banks.length) this.banks = banks;
    } catch (error) {
      console.warn("memory browser: could not read the machine's banks", error);
    }
    if (!this.bankSelect) return;

    const many = this.banks.length > 1;
    this.bankSelect.hidden = !many;
    this.bankSelect.innerHTML = this.banks
      .map(
        (b) =>
          `<option value="${b.bank}">${b.bank
            .toString(16)
            .toUpperCase()
            .padStart(2, "0")} ${b.name}</option>`,
      )
      .join("");
    if (!this.banks.some((b) => b.bank === this.bank)) {
      this.bank = this.banks[0].bank;
    }
    this.bankSelect.value = String(this.bank);
    this.updateRegionName();
    this.forceRefresh = true;
  }

  /** The machine changed, and so did the banks it has. */
  onMachineChanged() {
    this.bank = 0;
    this.baseAddress = 0;
    this.renderQuickJumps();
    this.previousMemory.fill(0);
    this.changedBytes.clear();
    this.changeTimestamps.clear();
    this.loadBanks();
  }

  /** An offset in the bank being browsed, as a full machine address. */
  address(offset) {
    return ((this.bank << 16) | (offset & 0xffff)) >>> 0;
  }

  /** What is where in the bank being browsed. */
  regionsForBank() {
    if (!machineProcessor().hasBanks) return MEMORY_REGIONS;
    const name = this.banks.find((b) => b.bank === this.bank)?.name ?? "";
    if (name.startsWith("Mega II aux")) return IIGS_AUX_REGIONS;
    if (name.startsWith("Mega II")) return MEMORY_REGIONS;
    // Fast RAM and ROM have no map of their own: a program uses fast RAM as it
    // likes, and ROM is ROM.
    return [{ name, start: 0x0000, end: 0xffff }];
  }

  goToAddress() {
    const input = this.addrInput.value.trim();
    // "E1/2000" selects the bank as well as the offset, which is how this
    // machine's own monitor is told where to go.
    const banked = input.match(/^\$?([0-9A-Fa-f]{1,2})\/([0-9A-Fa-f]{1,4})$/);
    if (banked && machineProcessor().hasBanks) {
      this.selectBank(parseInt(banked[1], 16));
      this.scrollToAddress(parseInt(banked[2], 16));
      return;
    }
    let addr = parseInt(input.replace(/^\$/, ""), 16);
    if (isNaN(addr)) addr = parseInt(input, 10);
    if (isNaN(addr) || addr < 0) return;
    // Six digits is a bank and an offset together.
    if (addr > 0xffff && machineProcessor().hasBanks) {
      this.selectBank((addr >>> 16) & 0xff);
      this.scrollToAddress(addr & 0xffff);
      return;
    }
    if (addr <= 0xffff) this.scrollToAddress(addr);
  }

  /** Browse a different bank, keeping the offset. */
  selectBank(bank) {
    if (!this.banks.some((b) => b.bank === bank)) return;
    if (bank === this.bank) return;
    this.bank = bank;
    // The change highlighting compares against what was last seen, and what
    // was last seen was a different bank.
    this.previousMemory.fill(0);
    this.changedBytes.clear();
    this.changeTimestamps.clear();
    if (this.bankSelect) this.bankSelect.value = String(bank);
    this.forceRefresh = true;
    this.updateRegionName();
    if (this.onStateChange) this.onStateChange();
  }

  async searchBytes() {
    const input = this.searchInput.value.trim().replace(/\s/g, "");
    if (input.length === 0 || input.length % 2 !== 0) return;

    const bytes = [];
    for (let i = 0; i < input.length; i += 2) {
      const byte = parseInt(input.substr(i, 2), 16);
      if (isNaN(byte)) return;
      bytes.push(byte);
    }

    // Search from current position + 1
    const startAddr = this.baseAddress + 1;
    for (let i = 0; i < 65536; i++) {
      const addr = (startAddr + i) & 0xffff;
      // Batch read the bytes we need to compare
      const batchCalls = bytes.map((_, j) =>
        ['_peekMemory', this.address(addr + j)]);
      const results = await this.wasmModule.batch(batchCalls);
      let found = true;
      for (let j = 0; j < bytes.length; j++) {
        if (results[j] !== bytes[j]) {
          found = false;
          break;
        }
      }
      if (found) {
        this.scrollToAddress(addr);
        return;
      }
    }
  }

  scrollToAddress(addr) {
    // Align to row boundary and clamp
    addr = Math.floor(addr / this.bytesPerRow) * this.bytesPerRow;
    addr = Math.max(
      0,
      Math.min(addr, 0x10000 - this.bytesPerRow * this.visibleRows),
    );
    if (addr !== this.baseAddress) {
      this.baseAddress = addr;
      this.forceRefresh = true;
      if (this.onStateChange) this.onStateChange();
    }
    this.updateScrollbar();
    this.updateRegionName();
  }

  updateScrollbar() {
    const maxAddress = 0x10000 - this.bytesPerRow * this.visibleRows;
    const thumbHeight = Math.max(
      20,
      (this.visibleRows * this.bytesPerRow * 100) / 65536,
    );
    const thumbPosition = (this.baseAddress / maxAddress) * (100 - thumbHeight);
    this.scrollbarThumb.style.height = `${thumbHeight}%`;
    this.scrollbarThumb.style.top = `${thumbPosition}%`;
  }

  updateRegionName() {
    if (!this.regionNameSpan) return;
    const addr = this.baseAddress;
    for (const region of this.regionsForBank()) {
      if (addr >= region.start && addr <= region.end) {
        this.regionNameSpan.textContent = region.name;
        return;
      }
    }
    this.regionNameSpan.textContent = "Unknown";
  }

  async startEditByte(addr) {
    const full = this.address(addr);
    const currentValue = await this.wasmModule._peekMemory(full);
    const newValueStr = prompt(
      `Edit $${formatMachineAddress(full)}\nCurrent value: $${this.formatHex(currentValue, 2)}`,
      this.formatHex(currentValue, 2),
    );
    if (newValueStr !== null) {
      const newValue = parseInt(newValueStr, 16);
      if (!isNaN(newValue) && newValue >= 0 && newValue <= 255) {
        this.wasmModule._writeMemory(full, newValue);
      }
    }
  }

  getRegionForAddress(addr) {
    for (const region of this.regionsForBank()) {
      if (addr >= region.start && addr <= region.end) {
        return region.name;
      }
    }
    return "Unknown";
  }

  recalcVisibleRows() {
    if (!this.contentDiv) return;
    const container = this.contentDiv.parentElement;
    if (!container) return;

    // Each row is ~17px (11px font * 1.4 line-height + 2px padding)
    const rowHeight = 17;
    const rows = Math.floor(container.clientHeight / rowHeight);

    // Ignore a measurement that cannot fit a single row. The container is laid
    // out to a definite height, so this only happens before layout has run or
    // while the window is collapsed — and trusting it is dangerous: the row
    // count drives how much gets rendered, so a bogus small value can become
    // self-fulfilling and stick. Keeping the previous value means the view
    // simply re-measures on the next tick.
    if (rows < 1) return;

    this.visibleRows = rows;
  }

  async update(wasmModule) {
    if (!this.isVisible || !this.contentDiv) return;
    this.recalcVisibleRows();
    // Read the locally cached pause state the Worker pushes on change, rather
    // than spending a round-trip per tick purely to discover we should bail.
    const isPaused = wasmModule.isPaused;
    if (!isPaused && !this.forceRefresh) return;
    this.forceRefresh = false;

    const now = Date.now();
    const fadeTime = 1000;

    // Batch read all visible memory addresses
    const totalBytes = Math.min(this.visibleRows * this.bytesPerRow, 0x10000 - this.baseAddress);
    const batchCalls = [];
    for (let i = 0; i < totalBytes; i++) {
      batchCalls.push(['_peekMemory', this.address(this.baseAddress + i)]);
    }
    const memValues = await wasmModule.batch(batchCalls);

    const pool = this._ensureRowPool();

    let memIdx = 0;
    let usedRows = 0;
    for (let row = 0; row < this.visibleRows; row++) {
      const rowAddr = this.baseAddress + row * this.bytesPerRow;
      if (rowAddr >= 0x10000) break;

      const rowEl = pool[row];
      usedRows++;

      const addrText = this.formatAddr(this.address(rowAddr));
      if (rowEl.last.addr !== addrText) {
        rowEl.addrSpan.textContent = addrText;
        rowEl.last.addr = addrText;
      }

      // Hex bytes
      let ascii = "";
      let col = 0;
      for (; col < this.bytesPerRow; col++) {
        const addr = rowAddr + col;
        if (addr >= 0x10000) break;

        const value = memValues[memIdx];
        const prevValue = this.previousMemory[addr];

        if (value !== prevValue) {
          this.changedBytes.add(addr);
          this.changeTimestamps.set(addr, now);
          this.previousMemory[addr] = value;
        }

        let byteClass = "mem-byte";
        if (value === 0x00) {
          byteClass += " mem-zero";
        } else if (value >= 0x20 && value < 0x7f) {
          byteClass += " mem-printable";
        } else if (value >= 0x80) {
          byteClass += " mem-highbit";
        }

        if (this.changeTimestamps.has(addr)) {
          const elapsed = now - this.changeTimestamps.get(addr);
          if (elapsed < fadeTime) {
            byteClass += " changed";
          } else {
            this.changeTimestamps.delete(addr);
          }
        }

        const byteEl = rowEl.byteSpans[col];
        const byteLast = rowEl.last.bytes[col];
        const text = this.formatHex(value, 2);
        if (byteLast.text !== text) {
          byteEl.textContent = text;
          byteLast.text = text;
        }
        if (byteLast.cls !== byteClass) {
          byteEl.className = byteClass;
          byteLast.cls = byteClass;
        }
        if (byteLast.addr !== addr) {
          byteEl.dataset.addr = addr;
          byteLast.addr = addr;
        }
        if (byteEl.hidden) byteEl.hidden = false;

        const ch = value & 0x7f;
        ascii += ch >= 0x20 && ch < 0x7f ? String.fromCharCode(ch) : ".";
        memIdx++;
      }

      // Past the end of the address space on the final row
      for (let c = col; c < this.bytesPerRow; c++) {
        if (!rowEl.byteSpans[c].hidden) rowEl.byteSpans[c].hidden = true;
      }

      if (rowEl.last.ascii !== ascii) {
        rowEl.asciiSpan.textContent = ascii;
        rowEl.last.ascii = ascii;
      }
      if (rowEl.el.hidden) rowEl.el.hidden = false;
    }

    for (let row = usedRows; row < pool.length; row++) {
      if (!pool[row].el.hidden) pool[row].el.hidden = true;
    }
  }

  /**
   * Build (or rebuild) the reusable row elements.
   *
   * The view used to be re-serialised into contentDiv.innerHTML on every tick,
   * discarding and re-parsing well over a thousand nodes and forcing a full
   * style recalc, layout and paint each time — while paused, which is exactly
   * when the user is staring at it and almost nothing is changing. Rows are now
   * built once per view shape and only the cells that actually differ get
   * written.
   */
  _ensureRowPool() {
    const shape = `${this.visibleRows}x${this.bytesPerRow}`;
    if (this._rowPool && this._rowPoolShape === shape) {
      return this._rowPool;
    }

    this._rowPoolShape = shape;
    this._rowPool = [];
    const fragment = document.createDocumentFragment();

    for (let row = 0; row < this.visibleRows; row++) {
      const el = document.createElement("div");
      el.className = "mem-row";

      const addrSpan = document.createElement("span");
      addrSpan.className = "mem-addr";
      const separator = document.createElement("span");
      separator.className = "mem-separator";
      separator.textContent = ":";
      el.append(addrSpan, separator);

      const byteSpans = [];
      const bytes = [];
      for (let col = 0; col < this.bytesPerRow; col++) {
        // Half-way gap, previously a bare space in the markup
        if (col === 8) el.appendChild(document.createTextNode(" "));
        const byteSpan = document.createElement("span");
        byteSpan.className = "mem-byte";
        el.appendChild(byteSpan);
        byteSpans.push(byteSpan);
        bytes.push({ text: null, cls: null, addr: null });
      }

      const asciiSpan = document.createElement("span");
      asciiSpan.className = "mem-ascii";
      el.appendChild(asciiSpan);

      fragment.appendChild(el);
      this._rowPool.push({
        el,
        addrSpan,
        asciiSpan,
        byteSpans,
        last: { addr: null, ascii: null, bytes },
      });
    }

    this.contentDiv.replaceChildren(fragment);
    return this._rowPool;
  }

  getState() {
    const base = super.getState();
    base.baseAddress = this.baseAddress;
    return base;
  }

  restoreState(state) {
    if (state.baseAddress !== undefined) {
      this.baseAddress = state.baseAddress;
    }
    super.restoreState(state);
  }

  show() {
    super.show();
    // Apply restored base address to UI after content is rendered
    if (this.contentDiv) {
      this.updateScrollbar();
      this.updateRegionName();
      this.forceRefresh = true;
    }
  }

  // Allow external code to jump to a specific address
  jumpToAddress(addr) {
    this.scrollToAddress(addr);
  }
}
