/*
 * cpu-debugger-window.js - CPU debugger with registers, disassembly, and breakpoints
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import { getSymbolInfo, getCategoryClass, ALL_SYMBOLS } from "./symbols.js";
import { BreakpointManager } from "./breakpoint-manager.js";
import { LabelManager } from "./label-manager.js";
import {
  machineProcessor,
  machineTiming,
  formatMachineAddress,
  machineAddressMask,
} from "../machine/machine-profile.js";

// How often the 64K profiling table is re-read for the disassembly heat
// overlay. See updateProfileData().
const PROFILE_REFRESH_MS = 250;

// Disassembly view geometry.
const DISASM_TOTAL_LINES = 24;
const DISASM_LINES_BEFORE = 6;

/**
 * CPUDebuggerWindow - CPU registers, disassembly, and breakpoints
 */
export class CPUDebuggerWindow extends BaseWindow {
  constructor(wasmModule, isRunningCallback) {
    super({
      id: "cpu-debugger",
      title: "CPU Debugger",
      minWidth: 420,
      minHeight: 400,
      defaultWidth: 500,
      defaultHeight: 660,
    });

    this.wasmModule = wasmModule;
    this.updateEveryNFrames = 2; // ~30fps - responsive for stepping
    this.isRunningCallback = isRunningCallback || (() => true);
    this.bpManager = new BreakpointManager(wasmModule);
    this.labelManager = new LabelManager();
    this.lastPC = null;
    this.disasmCache = []; // Cache of {addr, disasm, len} for current view
    this.disasmStartAddr = 0;
    this.disasmViewAddress = null; // When set, overrides PC-centered view
    this.previousRegisters = {}; // For change highlighting
    this.previousWatchValues = {}; // For watch change highlighting
    this.profileEnabled = false; // When true, shows heat overlay in disassembly
    this.watchExpressions = []; // Array of expression strings
    this.loadWatchExpressions();
    this.bookmarks = []; // Array of addresses
    this.loadBookmarks();
    this.beamBreakpoints = []; // Array of { id, scanline, hPos, enabled, mode }
    this.loadBeamBreakpoints();
    this.activeTab = "breakpoints"; // Active tab panel (breakpoints, watch, beam)
    this._hitBpAddr = -1; // Address of the breakpoint/watchpoint that triggered a pause
    this._lastHitBpAddr = -2; // Previous value for change detection

    // Re-render breakpoint list when breakpoints change
    this.bpManager.onChange(() => {
      this.updateBreakpointList();
      this.updateDisassembly();
    });
  }

  renderContent() {
    return `
      <div class="cpu-dbg">
        <div class="cpu-dbg-toolbar">
          <div class="cpu-dbg-btn-group">
            <button class="cpu-dbg-btn cpu-btn-run" id="dbg-run" title="Continue (F5)">▶ Run</button>
            <button class="cpu-dbg-btn cpu-btn-pause" id="dbg-pause" title="Pause">⏸ Pause</button>
          </div>
          <span class="cpu-dbg-sep"></span>
          <div class="cpu-dbg-btn-group">
            <button class="cpu-dbg-btn" id="dbg-step" title="Step Into (F11)">Step</button>
            <button class="cpu-dbg-btn" id="dbg-step-over" title="Step Over (F10)">Over</button>
            <button class="cpu-dbg-btn" id="dbg-step-out" title="Step Out (Shift+F11)">Out</button>
          </div>
        </div>
        <div class="cpu-dbg-status-bar">
          <div class="cpu-dbg-status-bar-left">
            <span class="cpu-dbg-status-dot"></span>
            <span class="cpu-dbg-status" id="dbg-status">PAUSED</span>
          </div>
        </div>

        <div class="cpu-dbg-section">
          <span class="cpu-dbg-section-label">REGS</span>
          <div class="cpu-dbg-regs">
            <div class="cpu-dbg-reg"><span class="reg-label">A</span><span class="reg-value" id="reg-a">00</span></div>
            <div class="cpu-dbg-reg"><span class="reg-label">X</span><span class="reg-value" id="reg-x">00</span></div>
            <div class="cpu-dbg-reg"><span class="reg-label">Y</span><span class="reg-value" id="reg-y">00</span></div>
            <div class="cpu-dbg-reg"><span class="reg-label">SP</span><span class="reg-value" id="reg-sp">FF</span></div>
            <div class="cpu-dbg-reg reg-wide"><span class="reg-label">PC</span><span class="reg-value" id="reg-pc">0000</span></div>
            <!-- A 65816's own registers. Hidden on a machine that has none;
                 see applyProcessor(). -->
            <div class="cpu-dbg-reg" id="reg-row-pbr" hidden><span class="reg-label">PB</span><span class="reg-value" id="reg-pbr">00</span></div>
            <div class="cpu-dbg-reg" id="reg-row-dbr" hidden><span class="reg-label">DB</span><span class="reg-value" id="reg-dbr">00</span></div>
            <div class="cpu-dbg-reg reg-wide" id="reg-row-dp" hidden><span class="reg-label">D</span><span class="reg-value" id="reg-dp">0000</span></div>
          </div>
        </div>

        <div class="cpu-dbg-section">
          <span class="cpu-dbg-section-label">FLAGS</span>
          <div class="cpu-flags" id="flags">
            <span class="flag" id="flag-n" title="Negative">N</span>
            <span class="flag" id="flag-v" title="Overflow">V</span>
            <!-- Bits 5 and 4 are the unused bit and Break on a 6502, and the
                 accumulator and index widths on a 65816 in native mode. The
                 labels follow the processor and its mode; see applyProcessor()
                 and updateFlags(). -->
            <span class="flag separator" id="flag-m">-</span>
            <span class="flag" id="flag-b" title="Break">B</span>
            <span class="flag" id="flag-d" title="Decimal">D</span>
            <span class="flag" id="flag-i" title="Interrupt Disable">I</span>
            <span class="flag" id="flag-z" title="Zero">Z</span>
            <span class="flag" id="flag-c" title="Carry">C</span>
          </div>
        </div>

        <div class="cpu-dbg-section">
          <span class="cpu-dbg-section-label">TIMING</span>
          <div class="cpu-dbg-timing-row">
            <span class="cpu-dbg-cycles" title="Total CPU cycles since reset"><span class="meta-dim">CYC</span> <span id="cycle-count">0</span></span>
            <span class="irq-indicator" id="irq-pending" title="IRQ Pending">IRQ</span>
            <span class="irq-indicator" id="nmi-pending" title="NMI Pending">NMI</span>
            <span class="irq-indicator" id="nmi-edge" title="NMI Edge Detected">EDGE</span>
          </div>
        </div>

        <div class="cpu-dbg-section">
          <span class="cpu-dbg-section-label">BEAM</span>
          <div class="cpu-dbg-scanline-row">
            <span class="scanline-item" title="Current scanline (0-261)"><span class="scanline-label">SCAN</span> <span class="scanline-value" id="scan-line">--</span></span>
            <span class="scanline-item" title="Horizontal position within scanline (0-64), includes visible and blanking portions"><span class="scanline-label">H</span> <span class="scanline-value" id="scan-hpos">--</span></span>
            <span class="scanline-item" title="Visible screen column (0-39), only valid during the visible portion of the scanline"><span class="scanline-label">COL</span> <span class="scanline-value" id="scan-col">--</span></span>
            <span class="scanline-item" title="CPU cycle count within the current frame"><span class="scanline-label">FCYC</span> <span class="scanline-value" id="scan-fcyc">--</span></span>
            <span class="scanline-badge scanline-badge-idle" id="scan-badge">--</span>
          </div>
        </div>

        <div class="cpu-dbg-disasm">
          <div class="cpu-dbg-disasm-bar">
            <input type="text" id="disasm-goto-input" placeholder="Address / Symbol" spellcheck="false">
            <button class="cpu-dbg-bar-btn" id="disasm-goto-btn" title="Go to address">Go</button>
            <button class="cpu-dbg-bar-btn" id="disasm-goto-pc" title="Follow PC">PC</button>
            <button class="cpu-dbg-bar-btn" id="disasm-import-sym" title="Import symbol file">Sym</button>
          </div>
          <div class="cpu-disasm-view" id="disasm-view"></div>
        </div>

        <div class="cpu-dbg-tabs">
          <div class="cpu-dbg-tab-bar">
            <button class="cpu-dbg-tab active" data-tab="breakpoints">Breakpoints <span class="cpu-dbg-tab-count" id="bp-tab-count">0</span></button>
            <button class="cpu-dbg-tab" data-tab="watch">Watch <span class="cpu-dbg-tab-count" id="watch-tab-count">0</span></button>
            <button class="cpu-dbg-tab" data-tab="beam">Beam <span class="cpu-dbg-tab-count" id="beam-tab-count">0</span></button>
          </div>
          <div class="cpu-dbg-tab-content active" data-tab="breakpoints">
            <div class="cpu-dbg-tab-toolbar">
              <select id="bp-source-select" title="Breakpoint source">
                <option value="addr">Addr</option>
                <option value="switch">Switch</option>
              </select>
              <select id="bp-type-select" title="Breakpoint type">
                <option value="exec">Exec</option>
                <option value="read">Read</option>
                <option value="write">Write</option>
                <option value="readwrite">R/W</option>
              </select>
              <input type="text" id="breakpoint-input" placeholder="$XXXX" spellcheck="false">
              <select id="bp-switch-select" title="Soft switch" style="display:none"></select>
              <button class="cpu-dbg-add-btn" id="breakpoint-add-btn" title="Add breakpoint">+</button>
            </div>
            <div class="cpu-bp-list" id="breakpoint-list"></div>
          </div>
          <div class="cpu-dbg-tab-content" data-tab="watch">
            <div class="cpu-dbg-tab-toolbar">
              <select id="watch-source-select" title="Watch source type">
                <option value="reg">Register</option>
                <option value="flag">Flag</option>
                <option value="byte">Byte</option>
                <option value="word">Word</option>
              </select>
              <select id="watch-detail-reg" class="watch-detail-control active" title="Register">
                <option value="A">A</option>
                <option value="X">X</option>
                <option value="Y">Y</option>
                <option value="SP">SP</option>
                <option value="PC">PC</option>
                <option value="P">P</option>
              </select>
              <select id="watch-detail-flag" class="watch-detail-control" title="Flag">
                <option value="N">N</option>
                <option value="V">V</option>
                <option value="B">B</option>
                <option value="D">D</option>
                <option value="I">I</option>
                <option value="Z">Z</option>
                <option value="C">C</option>
              </select>
              <input type="text" id="watch-detail-addr" class="watch-detail-control" placeholder="$0000" spellcheck="false">
              <button class="cpu-dbg-add-btn" id="watch-add-btn" title="Add watch">+</button>
            </div>
            <div class="cpu-watch-list" id="watch-list"></div>
          </div>
          <div class="cpu-dbg-tab-content" data-tab="beam">
            <div class="cpu-dbg-tab-toolbar">
              <select id="beam-mode-select" title="Beam breakpoint type">
                <option value="vbl">VBL Start</option>
                <option value="hblank">HBLANK</option>
                <option value="scanline">Scanline</option>
                <option value="column">Column</option>
                <option value="scancol">Scan + Col</option>
              </select>
              <input type="text" id="beam-scan-input" class="beam-input" placeholder="Row" maxlength="3" style="display:none">
              <input type="text" id="beam-col-input" class="beam-input" placeholder="Col" maxlength="2" style="display:none">
              <button class="cpu-dbg-add-btn" id="beam-add-btn" title="Add beam breakpoint">+</button>
            </div>
            <div class="cpu-beam-list" id="beam-list"></div>
          </div>
        </div>
      </div>
    `;
  }

  /**
   * Set up event listeners after content is rendered
   */
  setupContentEventListeners() {
    // Disassembly click handler using event delegation with mousedown
    const disasmView = this.contentElement.querySelector("#disasm-view");
    if (disasmView) {
      disasmView.addEventListener("mousedown", (e) => {
        const line = e.target.closest(".cpu-disasm-line");
        if (line && line.dataset.addr) {
          const addr = parseInt(line.dataset.addr, 16);
          e.preventDefault();
          e.stopPropagation();
          if (e.ctrlKey || e.metaKey) {
            this.toggleBookmark(addr);
          } else {
            this.bpManager.toggle(addr);
          }
        }
      });

      // Context menu for run to cursor
      disasmView.addEventListener("contextmenu", (e) => {
        e.preventDefault();
        const line = e.target.closest(".cpu-disasm-line");
        if (line && line.dataset.addr) {
          const addr = parseInt(line.dataset.addr, 16);
          this.showDisasmContextMenu(e.clientX, e.clientY, addr);
        }
      });
    }

    // Debug control buttons
    const runBtn = this.contentElement.querySelector("#dbg-run");
    const pauseBtn = this.contentElement.querySelector("#dbg-pause");
    const stepBtn = this.contentElement.querySelector("#dbg-step");

    if (runBtn) {
      runBtn.addEventListener("click", () => {
        this.wasmModule._setPaused(false);
      });
    }

    if (pauseBtn) {
      pauseBtn.addEventListener("click", () => {
        this.bpManager.clearTemp();
        this.wasmModule._setPaused(true);
      });
    }

    if (stepBtn) {
      stepBtn.addEventListener("click", () => {
        this.bpManager.clearTemp();
        this.wasmModule._stepInstruction();
      });
    }

    // Step Over - step over JSR instructions
    const stepOverBtn = this.contentElement.querySelector("#dbg-step-over");
    if (stepOverBtn) {
      stepOverBtn.addEventListener("click", () => this.stepOver());
    }

    // Step Out - run until RTS returns
    const stepOutBtn = this.contentElement.querySelector("#dbg-step-out");
    if (stepOutBtn) {
      stepOutBtn.addEventListener("click", () => this.stepOut());
    }

    // Goto address in disassembly
    const gotoInput = this.contentElement.querySelector("#disasm-goto-input");
    const gotoBtn = this.contentElement.querySelector("#disasm-goto-btn");
    const gotoPcBtn = this.contentElement.querySelector("#disasm-goto-pc");

    if (gotoBtn && gotoInput) {
      const doGoto = () => {
        const text = gotoInput.value.trim();
        if (!text) return;
        const addr = this.resolveAddress(text);
        if (addr !== null) {
          this.disasmViewAddress = addr;
          this.updateDisassembly();
        }
      };
      gotoBtn.addEventListener("click", doGoto);
      gotoInput.addEventListener("keypress", (e) => {
        if (e.key === "Enter") doGoto();
      });
      gotoInput.addEventListener("keydown", (e) => e.stopPropagation());
    }

    if (gotoPcBtn) {
      gotoPcBtn.addEventListener("click", () => {
        this.disasmViewAddress = null;
        this.updateDisassembly();
      });
    }

    // Import symbols button
    const importSymBtn =
      this.contentElement.querySelector("#disasm-import-sym");
    if (importSymBtn) {
      importSymBtn.addEventListener("click", () => this.importSymbolFile());
    }

    // Double-click disassembly line to add/edit comment
    if (disasmView) {
      disasmView.addEventListener("dblclick", (e) => {
        const line = e.target.closest(".cpu-disasm-line");
        if (line && line.dataset.addr) {
          const addr = parseInt(line.dataset.addr, 16);
          this.editInlineComment(addr);
        }
      });
    }

    // Breakpoint source mode switching (addr/switch)
    const bpSourceSelect =
      this.contentElement.querySelector("#bp-source-select");
    const bpTypeSelect = this.contentElement.querySelector("#bp-type-select");
    const bpSwitchSelect =
      this.contentElement.querySelector("#bp-switch-select");

    if (bpSourceSelect && bpSwitchSelect) {
      // Populate soft switch dropdown with optgroups
      for (const group of CPUDebuggerWindow.SOFT_SWITCH_GROUPS) {
        const optgroup = document.createElement("optgroup");
        optgroup.label = group.category;
        for (const sw of group.switches) {
          const option = document.createElement("option");
          option.value = `${sw.start}:${sw.end}:${sw.name}`;
          const startHex = sw.start.toString(16).toUpperCase();
          const endHex = sw.end.toString(16).toUpperCase();
          const range =
            sw.start === sw.end ? `$${startHex}` : `$${startHex}-${endHex}`;
          option.textContent = `${sw.name} (${range})`;
          option.title = sw.desc;
          optgroup.appendChild(option);
        }
        bpSwitchSelect.appendChild(optgroup);
      }

      bpSourceSelect.addEventListener("change", () => {
        const isSwitch = bpSourceSelect.value === "switch";
        const bpInput = this.contentElement.querySelector("#breakpoint-input");
        if (bpInput) bpInput.style.display = isSwitch ? "none" : "";
        bpSwitchSelect.style.display = isSwitch ? "" : "none";

        if (isSwitch) {
          // Hide Exec option, auto-select R/W
          if (bpTypeSelect) {
            const execOption = bpTypeSelect.querySelector(
              'option[value="exec"]',
            );
            if (execOption) execOption.style.display = "none";
            if (bpTypeSelect.value === "exec") bpTypeSelect.value = "readwrite";
          }
        } else {
          // Show Exec option again
          if (bpTypeSelect) {
            const execOption = bpTypeSelect.querySelector(
              'option[value="exec"]',
            );
            if (execOption) execOption.style.display = "";
          }
        }
      });
    }

    // Breakpoint add
    const bpInput = this.contentElement.querySelector("#breakpoint-input");
    const bpAddBtn = this.contentElement.querySelector("#breakpoint-add-btn");
    if (bpAddBtn && bpInput) {
      bpAddBtn.addEventListener("click", () => this.addBreakpointFromInput());
      bpInput.addEventListener("keypress", (e) => {
        if (e.key === "Enter") this.addBreakpointFromInput();
      });
      bpInput.addEventListener("keydown", (e) => e.stopPropagation());
    }

    // Breakpoint list event delegation (survives DOM rebuilds)
    const bpList = this.contentElement.querySelector("#breakpoint-list");
    if (bpList) {
      bpList.addEventListener("mousedown", (e) => {
        const removeBtn = e.target.closest(".bp-remove");
        if (removeBtn) {
          e.stopPropagation();
          e.preventDefault();
          const item = removeBtn.closest(".cpu-bp-item");
          if (item && item.dataset.addr) {
            this.bpManager.remove(parseInt(item.dataset.addr, 10));
          }
          return;
        }
        const editBtn = e.target.closest(".bp-edit");
        if (editBtn) {
          e.stopPropagation();
          e.preventDefault();
          const item = editBtn.closest(".cpu-bp-item");
          if (item && item.dataset.addr) {
            this.editBreakpointCondition(parseInt(item.dataset.addr, 10));
          }
          return;
        }
        const checkbox = e.target.closest(".bp-enable input");
        if (checkbox) {
          // Let the checkbox handle its own change event
          return;
        }
      });
      bpList.addEventListener("change", (e) => {
        const checkbox = e.target.closest(".bp-enable input");
        if (checkbox) {
          const item = checkbox.closest(".cpu-bp-item");
          if (item && item.dataset.addr) {
            this.bpManager.setEnabled(
              parseInt(item.dataset.addr, 10),
              checkbox.checked,
            );
          }
        }
      });
      bpList.addEventListener("dblclick", (e) => {
        const item = e.target.closest(".cpu-bp-item");
        if (item && item.dataset.addr) {
          e.stopPropagation();
          this.editBreakpointCondition(parseInt(item.dataset.addr, 10));
        }
      });
    }

    // Watch source/detail switching
    const watchSourceSelect = this.contentElement.querySelector(
      "#watch-source-select",
    );
    const watchDetailReg =
      this.contentElement.querySelector("#watch-detail-reg");
    const watchDetailFlag =
      this.contentElement.querySelector("#watch-detail-flag");
    const watchDetailAddr =
      this.contentElement.querySelector("#watch-detail-addr");
    const watchAddBtn = this.contentElement.querySelector("#watch-add-btn");

    if (watchSourceSelect) {
      watchSourceSelect.addEventListener("change", () => {
        const source = watchSourceSelect.value;
        if (watchDetailReg)
          watchDetailReg.classList.toggle("active", source === "reg");
        if (watchDetailFlag)
          watchDetailFlag.classList.toggle("active", source === "flag");
        if (watchDetailAddr)
          watchDetailAddr.classList.toggle(
            "active",
            source === "byte" || source === "word",
          );
      });
    }

    if (watchAddBtn) {
      watchAddBtn.addEventListener("click", () => this.addWatchFromForm());
    }

    if (watchDetailAddr) {
      watchDetailAddr.addEventListener("keypress", (e) => {
        if (e.key === "Enter") this.addWatchFromForm();
      });
      watchDetailAddr.addEventListener("keydown", (e) => e.stopPropagation());
    }

    // Tab switching
    const tabBar = this.contentElement.querySelector(".cpu-dbg-tab-bar");
    if (tabBar) {
      tabBar.addEventListener("click", (e) => {
        const tab = e.target.closest(".cpu-dbg-tab");
        if (!tab) return;
        const tabName = tab.dataset.tab;
        tabBar
          .querySelectorAll(".cpu-dbg-tab")
          .forEach((t) => t.classList.remove("active"));
        tab.classList.add("active");
        tab.classList.remove("hit-alert");
        this.contentElement
          .querySelectorAll(".cpu-dbg-tab-content")
          .forEach((c) => {
            c.classList.toggle("active", c.dataset.tab === tabName);
          });
        this.activeTab = tabName;
        if (this.onStateChange) this.onStateChange();
      });
    }

    // Watch list event delegation (survives DOM rebuilds from updateWatchList)
    const watchList = this.contentElement.querySelector("#watch-list");
    if (watchList) {
      watchList.addEventListener("mousedown", (e) => {
        const removeBtn = e.target.closest(".watch-remove");
        if (removeBtn) {
          e.stopPropagation();
          e.preventDefault();
          const item = removeBtn.closest(".cpu-watch-item");
          if (item && item.dataset.index !== undefined) {
            const idx = parseInt(item.dataset.index, 10);
            this.watchExpressions.splice(idx, 1);
            this.saveWatchExpressions();
            this.updateWatchList();
          }
        }
      });
    }
  }

  /**
   * Override create to set up content event listeners
   */
  create() {
    super.create();
    // Which processor this machine has decides what the panels show, so it is
    // applied before anything is wired to them.
    this.applyProcessor();
    this.applyBeamLimits();
    this.setupContentEventListeners();
    this.setupKeyboardShortcuts();
    this.setupRegisterEditing();
    this.setupBeamBreakTabEvents();
    this.updateBreakpointList();
    this.updateBeamList();
  }

  destroy() {
    this.removeKeyboardShortcuts();
    super.destroy();
  }

  setupKeyboardShortcuts() {
    this._keyHandler = (e) => {
      if (!this.isVisible) return;

      switch (e.key) {
        case "F5":
          e.preventDefault();
          e.stopPropagation();
          this.wasmModule._setPaused(false);
          break;
        case "F10":
          e.preventDefault();
          e.stopPropagation();
          this.stepOver();
          break;
        case "F11":
          e.preventDefault();
          e.stopPropagation();
          if (e.shiftKey) {
            this.stepOut();
          } else {
            this.bpManager.clearTemp();
            this.wasmModule._stepInstruction();
          }
          break;
      }
    };
    document.addEventListener("keydown", this._keyHandler, { capture: true });
  }

  removeKeyboardShortcuts() {
    if (this._keyHandler) {
      document.removeEventListener("keydown", this._keyHandler, {
        capture: true,
      });
      this._keyHandler = null;
    }
  }

  /**
   * Add breakpoint from input field
   */
  addBreakpointFromInput() {
    const sourceSelect = this.contentElement.querySelector("#bp-source-select");
    const typeSelect = this.contentElement.querySelector("#bp-type-select");
    const type = typeSelect ? typeSelect.value : "exec";

    if (sourceSelect && sourceSelect.value === "switch") {
      // Switch mode: parse the switch dropdown value
      const switchSelect =
        this.contentElement.querySelector("#bp-switch-select");
      if (!switchSelect || !switchSelect.value) return;

      const parts = switchSelect.value.split(":");
      const startAddr = parseInt(parts[0], 10);
      const endAddr = parseInt(parts[1], 10);
      const name = parts.slice(2).join(":");

      if (!isNaN(startAddr) && !isNaN(endAddr)) {
        this.bpManager.add(startAddr, {
          type,
          endAddress: endAddr,
          name,
        });
      }
    } else {
      // Address mode: parse the text input
      const input = this.contentElement.querySelector("#breakpoint-input");
      if (!input) return;

      const text = input.value.trim();
      const addr = this.resolveAddress(text) ?? parseInt(text, 16);

      if (!isNaN(addr) && addr >= 0 && addr <= machineAddressMask()) {
        this.bpManager.add(addr, { type });
        input.value = "";
      }
    }
  }

  /**
   * Build expression string from the watch form and add it
   */
  addWatchFromForm() {
    const source = this.contentElement.querySelector(
      "#watch-source-select",
    )?.value;
    if (!source) return;

    let expr = null;
    if (source === "reg") {
      expr = this.contentElement.querySelector("#watch-detail-reg")?.value;
    } else if (source === "flag") {
      expr = this.contentElement.querySelector("#watch-detail-flag")?.value;
    } else if (source === "byte" || source === "word") {
      const addrInput = this.contentElement.querySelector("#watch-detail-addr");
      if (!addrInput) return;
      const text = addrInput.value.trim();
      if (!text) return;
      const addr = this.resolveAddress(text);
      if (addr === null) return;
      const hexAddr = "$" + formatMachineAddress(addr);
      expr = source === "byte" ? `PEEK(${hexAddr})` : `DEEK(${hexAddr})`;
      addrInput.value = "";
    }

    if (expr) {
      this.watchExpressions.push(expr);
      this.saveWatchExpressions();
      this.updateWatchList();
    }
  }

  /**
   * Resolve an address from hex string or symbol name
   * @returns {number|null} Address or null if invalid
   */
  resolveAddress(text) {
    // A bank, a slash and an offset: "E1/2000", which is how this machine's
    // own monitor writes an address and how the panels above write one back.
    const banked = text.match(/^\$?([0-9A-Fa-f]{1,2})\/([0-9A-Fa-f]{1,4})$/);
    if (banked) {
      return (parseInt(banked[1], 16) << 16) | parseInt(banked[2], 16);
    }

    // Try hex: $XXXX, 0xXXXX, or plain hex — up to six digits on a machine
    // whose addresses are that wide.
    const hexMatch = text.match(/^\$?(?:0x)?([0-9A-Fa-f]{1,6})$/);
    if (hexMatch) {
      const value = parseInt(hexMatch[1], 16);
      return value <= machineAddressMask() ? value : null;
    }

    // Try label manager first (user labels + imported symbols)
    const labelAddr = this.labelManager.resolveByName(text);
    if (labelAddr !== null) return labelAddr;

    // Try built-in symbol name lookup (case-insensitive)
    const upper = text.toUpperCase();
    for (const [addr, info] of Object.entries(ALL_SYMBOLS)) {
      if (info.name.toUpperCase() === upper) {
        return parseInt(addr);
      }
    }

    return null;
  }

  // Breakpoint management is delegated to this.bpManager (BreakpointManager)

  /**
   * Step Over - if current instruction is JSR, run until it returns
   * Otherwise, just do a single step
   */
  async stepOver() {
    this.bpManager.clearTemp();
    const tempAddr = await this.wasmModule._stepOver();
    if (tempAddr) {
      this.bpManager.syncTemp(tempAddr);
    }
  }

  /**
   * Step Out - run until the current subroutine returns
   * Reads return address from stack and sets breakpoint there
   */
  async stepOut() {
    this.bpManager.clearTemp();
    const tempAddr = await this.wasmModule._stepOut();
    if (tempAddr) {
      this.bpManager.syncTemp(tempAddr);
    }
  }

  /**
   * Show context menu on disassembly line
   */
  showDisasmContextMenu(x, y, addr) {
    // Remove existing menu
    this.hideDisasmContextMenu();

    const menu = document.createElement("div");
    menu.className = "cpu-disasm-context-menu";
    menu.innerHTML = `
      <div class="ctx-item" data-action="run-to">Run to $${formatMachineAddress(addr)}</div>
      <div class="ctx-item" data-action="goto">Go to $${formatMachineAddress(addr)}</div>
      <div class="ctx-item" data-action="toggle-bp">${this.bpManager.has(addr) ? "Remove" : "Set"} Breakpoint</div>
    `;
    menu.style.cssText = `
      position: fixed;
      left: ${x}px;
      top: ${y}px;
      z-index: 10000;
      background: var(--glass-bg-solid);
      border: 1px solid var(--glass-border);
      border-radius: 4px;
      padding: 4px 0;
      font-family: var(--font-mono);
      font-size: 11px;
      box-shadow: var(--shadow-md);
      min-width: 160px;
    `;

    menu.querySelectorAll(".ctx-item").forEach((item) => {
      item.style.cssText = `
        padding: 6px 12px;
        color: var(--text-secondary);
        cursor: pointer;
      `;
      item.addEventListener("mouseenter", () => {
        item.style.background = "var(--accent-blue-bg-strong)";
        item.style.color = "var(--text-primary)";
      });
      item.addEventListener("mouseleave", () => {
        item.style.background = "transparent";
        item.style.color = "var(--text-secondary)";
      });
      item.addEventListener("click", () => {
        const action = item.dataset.action;
        if (action === "run-to") {
          this.runToCursor(addr);
        } else if (action === "goto") {
          this.disasmViewAddress = addr;
          this.updateDisassembly();
        } else if (action === "toggle-bp") {
          this.bpManager.toggle(addr);
        }
        this.hideDisasmContextMenu();
      });
    });

    document.body.appendChild(menu);
    this._contextMenu = menu;

    // Close on click anywhere else
    this._contextMenuClose = () => this.hideDisasmContextMenu();
    setTimeout(() => {
      document.addEventListener("mousedown", this._contextMenuClose);
    }, 0);
  }

  hideDisasmContextMenu() {
    if (this._contextMenu) {
      this._contextMenu.remove();
      this._contextMenu = null;
    }
    if (this._contextMenuClose) {
      document.removeEventListener("mousedown", this._contextMenuClose);
      this._contextMenuClose = null;
    }
  }

  /**
   * Run to cursor - set temp breakpoint and resume
   */
  runToCursor(addr) {
    this.bpManager.setTemp(addr);
    this.wasmModule._setPaused(false);
  }

  /**
   * Update all window content
   */
  async update(wasmModule) {
    this.wasmModule = wasmModule;

    // ONE round-trip for the whole window.
    //
    // This used to be eight: a [_getPC,_isPaused] batch, then checkTemp, then
    // updateRegisters, updateFlags, updateIRQState, updateDisassembly and
    // updateBeamHitHighlight each awaiting their own. At ~30fps that was ~240
    // round-trips a second, every one of them queued on the worker thread that
    // is also running the emulator.
    //
    // A few entries here are only meaningful under conditions we cannot know
    // until the results come back (the beam readouts matter only while paused,
    // the breakpoint addresses only on a hit). Fetching them unconditionally
    // costs a handful of trivial WASM getters inside a message we are already
    // sending; splitting the batch to avoid them would cost a second
    // round-trip, which is far more expensive.
    const S = CPUDebuggerWindow.UPDATE_BATCH;
    const results = await this.wasmModule.batch([
      ['_getPC'],
      ['_isPaused'],
      ['_isTempBreakpointHit'],
      ['_getP'],
      ['_isIRQPending'],
      ['_isNMIPending'],
      ['_isNMIEdge'],
      ['_getA'],
      ['_getX'],
      ['_getY'],
      ['_getSP'],
      ['_getTotalCycles'],
      ['_isBreakpointHit'],
      ['_getBreakpointAddress'],
      ['_isWatchpointHit'],
      ['_getWatchpointAddress'],
      ['_isBeamBreakpointHit'],
      ['_getBeamBreakpointHitId'],
      ['_getFrameCycle'],
      ['_getBeamScanline'],
      ['_getBeamHPos'],
      ['_getBeamColumn'],
      ['_isInVBL'],
      ['_isInHBLANK'],
      // The registers and the widths only a 65816 has. Asked for
      // unconditionally because they are trivial getters inside a message
      // already being sent, and they read zero on a machine without them.
      ['_getPBR'],
      ['_getDBR'],
      ['_getDirectPage'],
      ['_getCpuWidths'],
      // Negative centre address means "use the current PC" — see the
      // _disassembleRange export. Passing pc explicitly is impossible here
      // because we do not have it until this very batch returns.
      ['__callString', '_disassembleRange',
        this.disasmViewAddress !== null ? this.disasmViewAddress : -1,
        DISASM_LINES_BEFORE, DISASM_TOTAL_LINES],
    ]);

    const pc = results[S.PC];
    const isPaused = results[S.IS_PAUSED];

    // Update status indicator
    const statusEl = this.contentElement.querySelector("#dbg-status");
    const statusBar = this.contentElement.querySelector(".cpu-dbg-status-bar");
    if (statusEl) {
      const emulatorOn = this.isRunningCallback();
      if (!emulatorOn) {
        statusEl.textContent = "EMULATOR OFF";
        statusEl.classList.remove("running");
        if (statusBar) statusBar.dataset.state = "off";
      } else if (isPaused) {
        statusEl.textContent = "PAUSED";
        statusEl.classList.remove("running");
        if (statusBar) statusBar.dataset.state = "paused";
      } else {
        statusEl.textContent = "RUNNING";
        statusEl.classList.add("running");
        if (statusBar) statusBar.dataset.state = "running";
      }
    }

    // Check temp breakpoint. The return value matters: a temp breakpoint
    // (run-to-cursor, step over/out) is not in the JS breakpoint map, so the
    // conditional-breakpoint check below would fail to recognise it, decide it
    // was a stale hit, and resume — undoing the stop we just made.
    const tempHit = this.bpManager.checkTempWithState(
      results[S.TEMP_BP_HIT], isPaused, results[S.BP_HIT], results[S.BP_ADDR],
    );

    // Check if a watchpoint was hit - evaluate conditions/hit counts (range-aware)
    if (isPaused && results[S.WP_HIT]) {
      const wpAddr = results[S.WP_ADDR];
      const entry = this.bpManager.findByAddress(wpAddr);
      if (entry) {
        if (!await this.bpManager.shouldBreakEntry(entry)) {
          // Condition not met or hit target not reached - resume
          this.wasmModule._setPaused(false);
          return;
        }
        this._hitBpAddr = entry.address;
      } else if (!await this.bpManager.shouldBreak(wpAddr)) {
        // Fallback for direct-address match
        this.wasmModule._setPaused(false);
        return;
      } else {
        this._hitBpAddr = wpAddr;
      }
    }

    // Check conditional breakpoint evaluation. A temp breakpoint has no
    // condition or hit count to evaluate — arriving at it IS the stop — so it
    // bypasses this and simply stays paused.
    if (isPaused && results[S.BP_HIT]) {
      const bpAddr = results[S.BP_ADDR];
      // tempHit is only true on the update that first sees the stop; isTempStop
      // covers every later update that re-examines the same still-set hit.
      if (tempHit || this.bpManager.isTempStop(bpAddr)) {
        this._hitBpAddr = bpAddr;
      } else if (!await this.bpManager.shouldBreak(bpAddr)) {
        // Condition not met - resume execution
        this.wasmModule._setPaused(false);
        return;
      } else {
        this._hitBpAddr = bpAddr;
      }
    }

    // Clear hit address when running
    if (!isPaused) {
      this._hitBpAddr = -1;
      this.bpManager.clearTempStop();
    }

    // If PC changed, snap disassembly back to follow PC
    if (this.lastPC !== null && pc !== this.lastPC) {
      this.disasmViewAddress = null;
    }

    // Resolves without a round-trip when handed batch results; awaited only so
    // a throw surfaces rather than becoming an unhandled rejection.
    await this.updateRegisters(results);
    this.updateFlags(results[S.P], results[S.CPU_WIDTHS]);
    this.updateIRQState([
      results[S.IRQ_PENDING],
      results[S.NMI_PENDING],
      results[S.NMI_EDGE],
    ]);
    if (isPaused) {
      this.updateScanline([
        results[S.FRAME_CYCLE],
        results[S.BEAM_SCANLINE],
        results[S.BEAM_HPOS],
        results[S.BEAM_COLUMN],
        results[S.IN_VBL],
        results[S.IN_HBLANK],
      ]);
    } else {
      this.clearScanline();
    }
    this.renderDisassembly(results[S.DISASM], pc, isPaused);
    await this.updateWatchList();
    this.updateBeamHitHighlight(
      isPaused && results[S.BEAM_BP_HIT] ? results[S.BEAM_BP_HIT_ID] : -1,
    );
    this.updateBreakpointHitHighlight();
  }

  /**
   * Index of each value in the single per-update batch issued by update().
   * Must stay in step with the array passed to wasmModule.batch() there.
   */
  static UPDATE_BATCH = {
    PC: 0,
    IS_PAUSED: 1,
    TEMP_BP_HIT: 2,
    P: 3,
    IRQ_PENDING: 4,
    NMI_PENDING: 5,
    NMI_EDGE: 6,
    A: 7,
    X: 8,
    Y: 9,
    SP: 10,
    TOTAL_CYCLES: 11,
    BP_HIT: 12,
    BP_ADDR: 13,
    WP_HIT: 14,
    WP_ADDR: 15,
    BEAM_BP_HIT: 16,
    BEAM_BP_HIT_ID: 17,
    FRAME_CYCLE: 18,
    BEAM_SCANLINE: 19,
    BEAM_HPOS: 20,
    BEAM_COLUMN: 21,
    IN_VBL: 22,
    IN_HBLANK: 23,
    PBR: 24,
    DBR: 25,
    DP: 26,
    CPU_WIDTHS: 27,
    DISASM: 28,
  };

  /**
   * Register definitions for display and editing
   */
  // The registers to show, and how wide each is. `digits` is a default for a
  // //e; applyProcessor() widens them on a machine whose registers are wider
  // and reveals the ones only that machine has. `needs` names the capability
  // a register depends on, so a processor without it is not asked about one
  // it does not have.
  static REGISTER_DEFS = [
    { id: "reg-a", fn: "_getA", setFn: "_setRegA", digits: 2, wide: true },
    { id: "reg-x", fn: "_getX", setFn: "_setRegX", digits: 2, wide: true },
    { id: "reg-y", fn: "_getY", setFn: "_setRegY", digits: 2, wide: true },
    { id: "reg-sp", fn: "_getSP", setFn: "_setRegSP", digits: 2, wide: true },
    { id: "reg-pc", fn: "_getPC", setFn: "_setRegPC", digits: 4, address: true },
    {
      id: "reg-pbr",
      fn: "_getPBR",
      setFn: "_setRegPBR",
      digits: 2,
      needs: "hasBanks",
      row: "reg-row-pbr",
    },
    {
      id: "reg-dbr",
      fn: "_getDBR",
      setFn: "_setRegDBR",
      digits: 2,
      needs: "hasBanks",
      row: "reg-row-dbr",
    },
    {
      id: "reg-dp",
      fn: "_getDirectPage",
      setFn: "_setRegDirectPage",
      digits: 4,
      needs: "hasDirectPage",
      row: "reg-row-dp",
    },
    { id: "cycle-count", fn: "_getTotalCycles", setFn: null, digits: 0 },
  ];

  /**
   * Which of those registers this machine actually has, at this machine's
   * widths. The program counter is an address, so it follows the address
   * width rather than the register width: a 65816's is 24 bits even though
   * its registers are 16.
   */
  static registersFor(processor) {
    const registerDigits = processor.registerBits > 8 ? 4 : 2;
    const addressDigits = processor.addressBits > 16 ? 6 : 4;
    return CPUDebuggerWindow.REGISTER_DEFS.filter(
      (def) => !def.needs || processor[def.needs],
    ).map((def) => ({
      ...def,
      digits: def.wide
        ? registerDigits
        : def.address
          ? addressDigits
          : def.digits,
    }));
  }

  /**
   * Update CPU register display.
   *
   * Values come from update()'s single batch. Called on its own from the
   * register-edit commit path, where batchResults is omitted and the values
   * are fetched — that is a one-off on user input, not a per-frame cost.
   * @param {Array} [batchResults] - results array from update()
   */
  async updateRegisters(batchResults) {
    const S = CPUDebuggerWindow.UPDATE_BATCH;
    const defs = this.registers || CPUDebuggerWindow.registersFor(machineProcessor());
    const BATCH_SLOT = {
      "reg-a": S.A,
      "reg-x": S.X,
      "reg-y": S.Y,
      "reg-sp": S.SP,
      "reg-pc": S.PC,
      "reg-pbr": S.PBR,
      "reg-dbr": S.DBR,
      "reg-dp": S.DP,
      "cycle-count": S.TOTAL_CYCLES,
    };
    const values = batchResults
      ? defs.map(({ id }) => batchResults[BATCH_SLOT[id]])
      : await this.wasmModule.batch(defs.map(({ fn }) => [fn]));

    defs.forEach(({ id, digits }, i) => {
      const elem = this.contentElement.querySelector(`#${id}`);
      if (!elem) return;
      // Don't update if we're currently editing this register
      if (elem.dataset.editing === "true") return;
      const value = values[i];
      // The program counter is an address, and on a machine with banks an
      // address is written with one: "00/FF69" rather than "00FF69".
      const text =
        digits === 0
          ? value.toString()
          : id === "reg-pc"
            ? formatMachineAddress(value)
            : this.formatHex(value, digits);

      // Nothing changed — skip the DOM entirely rather than rewriting the same
      // string and dirtying layout for six elements every frame.
      const prevVal = this.previousRegisters[id];
      if (prevVal === text) return;

      // Highlight changes
      if (prevVal !== undefined) {
        elem.classList.remove("changed");
        // Force reflow to restart animation
        void elem.offsetWidth;
        elem.classList.add("changed");
      }
      this.previousRegisters[id] = text;

      elem.textContent = text;
    });
  }

  /**
   * Set up register editing - click a register value to edit it
   */
  setupRegisterEditing() {
    const editable =
      this.registers || CPUDebuggerWindow.registersFor(machineProcessor());
    editable.forEach(({ id, setFn, digits }) => {
      if (!setFn) return; // Skip non-editable registers like cycle count
      const elem = this.contentElement.querySelector(`#${id}`);
      if (!elem) return;

      elem.style.cursor = "pointer";
      elem.title = "Click to edit";

      elem.addEventListener("dblclick", async (e) => {
        e.stopPropagation();
        if (!await this.wasmModule._isPaused()) return;
        if (elem.dataset.editing === "true") return;

        const currentValue = elem.textContent;
        elem.dataset.editing = "true";

        const input = document.createElement("input");
        input.type = "text";
        input.value = currentValue;
        // One more than the digits, for the slash in a banked address.
        input.maxLength = digits + 1;
        input.className = "cpu-reg-edit-input";
        input.style.cssText = `
          width: ${digits * 8 + 8}px;
          font-family: var(--font-mono);
          font-size: 12px;
          font-weight: 600;
          color: var(--accent-green);
          background: var(--input-bg-deeper);
          border: 1px solid var(--accent-blue);
          border-radius: 2px;
          padding: 0 2px;
          text-align: center;
          outline: none;
        `;

        elem.textContent = "";
        elem.appendChild(input);
        input.focus();
        input.select();

        const commit = () => {
          // A bank and a slash is how this machine writes an address, so it
          // has to be accepted back: "E1/2000" is the program counter the
          // panel just showed.
          const typed = input.value.replace("/", "");
          const val = parseInt(typed, 16);
          const maxVal = Math.pow(16, digits) - 1;
          if (!isNaN(val) && val >= 0 && val <= maxVal) {
            this.wasmModule[setFn](val);
          }
          elem.dataset.editing = "false";
          input.remove();
          this.updateRegisters();
          this.updateDisassembly();
        };

        const cancel = () => {
          elem.dataset.editing = "false";
          input.remove();
          this.updateRegisters();
        };

        input.addEventListener("keydown", (ev) => {
          if (ev.key === "Enter") {
            ev.preventDefault();
            commit();
          } else if (ev.key === "Escape") {
            ev.preventDefault();
            cancel();
          }
          ev.stopPropagation();
        });
        input.addEventListener("blur", commit);
      });
    });
  }

  /**
   * Set up beam breakpoint tab events
   */
  setupBeamBreakTabEvents() {
    const modeSelect = this.contentElement.querySelector("#beam-mode-select");
    const scanInput = this.contentElement.querySelector("#beam-scan-input");
    const colInput = this.contentElement.querySelector("#beam-col-input");
    const addBtn = this.contentElement.querySelector("#beam-add-btn");
    if (!modeSelect) return;

    const updateInputVisibility = () => {
      const mode = modeSelect.value;
      if (scanInput)
        scanInput.style.display =
          mode === "scanline" || mode === "scancol" ? "" : "none";
      if (colInput)
        colInput.style.display =
          mode === "column" || mode === "scancol" ? "" : "none";
    };

    modeSelect.addEventListener("change", updateInputVisibility);
    updateInputVisibility();

    if (addBtn) {
      addBtn.addEventListener("click", () => this.addBeamBreakpointFromForm());
    }

    const onInputKey = (e) => {
      if (e.key === "Enter") this.addBeamBreakpointFromForm();
      e.stopPropagation();
    };
    if (scanInput) scanInput.addEventListener("keydown", onInputKey);
    if (colInput) colInput.addEventListener("keydown", onInputKey);

    // Event delegation on beam list for checkboxes and remove buttons
    const beamList = this.contentElement.querySelector("#beam-list");
    if (beamList) {
      beamList.addEventListener("mousedown", (e) => {
        const removeBtn = e.target.closest(".beam-remove");
        if (removeBtn) {
          e.stopPropagation();
          e.preventDefault();
          const item = removeBtn.closest(".cpu-beam-item");
          if (item && item.dataset.id) {
            this.removeBeamBreakpoint(parseInt(item.dataset.id, 10));
          }
          return;
        }
      });
      beamList.addEventListener("change", (e) => {
        const checkbox = e.target.closest(".beam-enable input");
        if (checkbox) {
          const item = checkbox.closest(".cpu-beam-item");
          if (item && item.dataset.id) {
            this.enableBeamBreakpoint(
              parseInt(item.dataset.id, 10),
              checkbox.checked,
            );
          }
        }
      });
    }
  }

  /**
   * Add a beam breakpoint from the toolbar form
   */
  async addBeamBreakpointFromForm() {
    const modeSelect = this.contentElement.querySelector("#beam-mode-select");
    const scanInput = this.contentElement.querySelector("#beam-scan-input");
    const colInput = this.contentElement.querySelector("#beam-col-input");
    if (!modeSelect) return;

    const mode = modeSelect.value;
    let scanline = -1;
    let hPos = -1;

    switch (mode) {
      case "vbl":
        // Where vertical blanking starts is the machine's, not 192 everywhere.
        scanline = machineTiming().visibleScanlines;
        hPos = 0;
        break;
      case "hblank":
        scanline = -1;
        hPos = 0;
        break;
      case "scanline": {
        const n = parseInt(scanInput?.value, 10);
        if (isNaN(n) || n < 0 || n >= machineTiming().scanlinesPerFrame) return;
        scanline = n;
        hPos = -1;
        break;
      }
      case "column": {
        const c = parseInt(colInput?.value, 10);
        if (isNaN(c) || c < 0 || c >= machineTiming().visibleColumns) return;
        scanline = -1;
        // A scanline starts in blanking and the visible columns follow it, so
        // a column is an offset past the blanking rather than a position.
        hPos = c + machineTiming().hblankCycles;
        break;
      }
      case "scancol": {
        const n = parseInt(scanInput?.value, 10);
        const c = parseInt(colInput?.value, 10);
        const timing = machineTiming();
        if (isNaN(n) || n < 0 || n >= timing.scanlinesPerFrame) return;
        if (isNaN(c) || c < 0 || c >= timing.visibleColumns) return;
        scanline = n;
        hPos = c + timing.hblankCycles;
        break;
      }
    }

    const id = await this.wasmModule._addBeamBreakpoint(scanline, hPos);
    if (id < 0) return; // full

    this.beamBreakpoints.push({ id, scanline, hPos, enabled: true, mode });
    this.saveBeamBreakpoints();
    await this.updateBeamList();
  }

  /**
   * Remove a beam breakpoint by ID
   */
  removeBeamBreakpoint(id) {
    this.wasmModule._removeBeamBreakpoint(id);
    this.beamBreakpoints = this.beamBreakpoints.filter((bp) => bp.id !== id);
    this.saveBeamBreakpoints();
    this.updateBeamList();
  }

  /**
   * Enable/disable a beam breakpoint by ID
   */
  enableBeamBreakpoint(id, enabled) {
    this.wasmModule._enableBeamBreakpoint(id, enabled);
    const bp = this.beamBreakpoints.find((b) => b.id === id);
    if (bp) bp.enabled = enabled;
    this.saveBeamBreakpoints();
    this.updateBeamList();
  }

  /**
   * Render the beam breakpoint list and update the tab badge
   */
  async updateBeamList() {
    const list = this.contentElement.querySelector("#beam-list");
    if (!list) return;

    list.innerHTML = "";
    const isPaused = await this.wasmModule._isPaused();
    let hitId = -1;
    if (isPaused && this.wasmModule._isBeamBreakpointHit) {
      const isHit = await this.wasmModule._isBeamBreakpointHit();
      if (isHit) {
        hitId = await this.wasmModule._getBeamBreakpointHitId();
      }
    }

    for (const bp of this.beamBreakpoints) {
      const item = document.createElement("div");
      item.className = "cpu-beam-item";
      item.dataset.id = bp.id;
      if (!bp.enabled) item.classList.add("disabled");
      if (bp.id === hitId) item.classList.add("hit");

      const { typeLabel, typeClass, detail } =
        this.getBeamBreakpointDisplay(bp);

      item.innerHTML = `
        <span class="beam-enable"><input type="checkbox" ${bp.enabled ? "checked" : ""}></span>
        <span class="beam-type ${typeClass}">${typeLabel}</span>
        <span class="beam-detail">${detail}</span>
        <button class="beam-remove" title="Remove">×</button>
      `;
      list.appendChild(item);
    }

    if (this.beamBreakpoints.length === 0) {
      const empty = document.createElement("div");
      empty.className = "cpu-dbg-empty-state";
      empty.textContent =
        "Add beam breakpoints to pause at specific raster positions.";
      list.appendChild(empty);
    }

    // Update tab badge count
    const badge = this.contentElement.querySelector("#beam-tab-count");
    if (badge) {
      badge.textContent = this.beamBreakpoints.length;
      badge.classList.toggle("has-items", this.beamBreakpoints.length > 0);
    }
  }

  /**
   * Highlight the breakpoint/watchpoint that triggered a pause
   */
  updateBreakpointHitHighlight() {
    const addr = this._hitBpAddr;
    if (addr === this._lastHitBpAddr) return;
    this._lastHitBpAddr = addr;

    const list = this.contentElement.querySelector("#breakpoint-list");
    if (list) {
      let hitItem = null;
      const items = list.querySelectorAll(".cpu-bp-item");
      for (const item of items) {
        const isHit = parseInt(item.dataset.addr, 10) === addr;
        if (isHit && !item.classList.contains("hit")) {
          item.classList.remove("hit");
          void item.offsetWidth; // force reflow to restart animation
          hitItem = item;
        }
        item.classList.toggle("hit", isHit);
      }
      if (hitItem) {
        hitItem.scrollIntoView({ block: "nearest", behavior: "smooth" });
      }
    }

    // Pulse tab header if breakpoints panel is not active
    this._pulseTabHeader("breakpoints", addr >= 0);
  }

  /**
   * Lightweight hit highlight update — toggles .hit class without rebuilding DOM
   */
  /**
   * @param {number} hitId - id of the beam breakpoint currently hit, or -1.
   *                         Resolved by update() from its single batch.
   */
  updateBeamHitHighlight(hitId) {
    const list = this.contentElement.querySelector("#beam-list");
    if (!list) return;

    if (hitId === this._lastBeamHitId) return;
    this._lastBeamHitId = hitId;

    const items = list.querySelectorAll(".cpu-beam-item");
    for (const item of items) {
      const id = parseInt(item.dataset.id, 10);
      item.classList.toggle("hit", id === hitId);
    }

    // Pulse tab header if beam panel is not active
    this._pulseTabHeader("beam", hitId >= 0);
  }

  /**
   * Pulse a tab header to draw attention when its panel is not active
   */
  _pulseTabHeader(tabName, isHit) {
    const tab = this.contentElement.querySelector(
      `.cpu-dbg-tab[data-tab="${tabName}"]`,
    );
    if (!tab) return;

    if (isHit && this.activeTab !== tabName) {
      if (!tab.classList.contains("hit-alert")) {
        tab.classList.add("hit-alert");
      }
    }
    // hit-alert is cleared only when the user clicks the tab
  }

  /**
   * Get display info for a beam breakpoint
   */
  getBeamBreakpointDisplay(bp) {
    const modeMap = {
      vbl: {
        typeLabel: "VBL",
        typeClass: "beam-type-vbl",
        detail: "Scanline 192",
      },
      hblank: {
        typeLabel: "HBL",
        typeClass: "beam-type-hbl",
        detail: "HPos 0",
      },
      scanline: {
        typeLabel: "SCAN",
        typeClass: "beam-type-scan",
        detail: `Row ${bp.scanline}`,
      },
      column: {
        typeLabel: "COL",
        typeClass: "beam-type-col",
        detail: `Col ${bp.hPos - 25}`,
      },
      scancol: {
        typeLabel: "S+C",
        typeClass: "beam-type-sc",
        detail: `Row ${bp.scanline}, Col ${bp.hPos - 25}`,
      },
    };
    return (
      modeMap[bp.mode] || {
        typeLabel: "?",
        typeClass: "",
        detail: `Scan ${bp.scanline}, HPos ${bp.hPos}`,
      }
    );
  }

  /**
   * Update CPU flags display
   */
  /**
   * @param {number} p - status register, already fetched by update()
   */
  /**
   * Shape the panels to the machine's processor.
   *
   * A 65816 has a program bank, a data bank and a direct page that a 6502
   * does not, so those rows are revealed; the machine's register width
   * decides how many digits each value gets. Called again when the machine
   * changes, because switching machines can change the processor.
   */
  applyProcessor() {
    const processor = machineProcessor();
    this.registers = CPUDebuggerWindow.registersFor(processor);

    // Reveal the rows this processor has, and hide the ones it has not.
    for (const def of CPUDebuggerWindow.REGISTER_DEFS) {
      if (!def.row) continue;
      const row = this.contentElement?.querySelector(`#${def.row}`);
      if (row) row.hidden = !(def.needs && processor[def.needs]);
    }
    // The disassembly's address and byte columns are wider on a machine whose
    // addresses carry a bank and whose instructions run to four bytes.
    const banked = processor.addressBits > 16;
    const root = this.element ?? this.contentElement;
    if (root) {
      root.style.setProperty("--disasm-addr-width", banked ? "58px" : "36px");
      root.style.setProperty("--disasm-bytes-width", banked ? "88px" : "68px");
    }

    // The panel is rebuilt, so the last values no longer describe it.
    this.previousRegisters = {};
    this.applyFlagLabels(false);
  }

  /**
   * The two flag bits whose name depends on the mode.
   *
   * A 6502's bit 5 is unused and bit 4 is Break. A 65816 in native mode
   * calls them M and X — the accumulator and index widths — and in emulation
   * mode it is the 6502 again.
   */
  /**
   * The beam readouts' and inputs' limits, which belong to the machine.
   *
   * A scanline count, a blanking width and a column count are all in the
   * machine profile, and were written into the markup as a //e's.
   */
  applyBeamLimits() {
    const timing = machineTiming();
    const hint = (selector, text) => {
      const el = this.contentElement?.querySelector(selector);
      if (el) el.title = text;
    };
    hint("#scan-line", `Current scanline (0-${timing.scanlinesPerFrame - 1})`);
    hint(
      "#scan-hpos",
      `Horizontal position within scanline (0-${timing.cyclesPerScanline - 1}), ` +
        `includes visible and blanking portions`,
    );
    hint(
      "#scan-col",
      `Visible screen column (0-${timing.visibleColumns - 1}), only valid ` +
        `during the visible portion of the scanline`,
    );
    const scanInput = this.contentElement?.querySelector("#beam-scan-input");
    if (scanInput) {
      scanInput.title = `0-${timing.scanlinesPerFrame - 1}`;
    }
    const colInput = this.contentElement?.querySelector("#beam-col-input");
    if (colInput) {
      colInput.title = `0-${timing.visibleColumns - 1}`;
    }
  }

  applyFlagLabels(native) {
    const m = this.contentElement?.querySelector("#flag-m");
    const b = this.contentElement?.querySelector("#flag-b");
    if (m) {
      m.textContent = native ? "M" : "-";
      m.title = native ? "Accumulator is 8-bit" : "Unused";
      m.classList.toggle("separator", !native);
    }
    if (b) {
      b.textContent = native ? "X" : "B";
      b.title = native ? "Index registers are 8-bit" : "Break";
    }
  }

  /** The machine changed, and with it the processor the panels describe. */
  onMachineChanged() {
    this.applyProcessor();
    this.applyBeamLimits();
    this.updateRegisters();
  }

  updateFlags(p, widths) {
    // Bits 5 and 4 are the unused bit and Break on a 6502. On a 65816 in
    // native mode they are the accumulator and index widths, and getting them
    // wrong is not cosmetic: they are what says how long an immediate is.
    const processor = machineProcessor();
    const native =
      processor.hasModes && widths !== undefined && (widths & 0x01) === 0;
    this.applyFlagLabels(native);

    const flags = [
      { id: "flag-n", bit: 0x80 },
      { id: "flag-v", bit: 0x40 },
      { id: "flag-m", bit: 0x20, only: native },
      { id: "flag-b", bit: 0x10 },
      { id: "flag-d", bit: 0x08 },
      { id: "flag-i", bit: 0x04 },
      { id: "flag-z", bit: 0x02 },
      { id: "flag-c", bit: 0x01 },
    ].filter(({ only }) => only !== false);

    flags.forEach(({ id, bit }) => {
      const elem = this.contentElement.querySelector(`#${id}`);
      if (elem) {
        elem.classList.toggle("active", (p & bit) !== 0);
      }
    });
  }

  /**
   * Update IRQ/NMI state indicators
   */
  /**
   * @param {Array} states - [irqPending, nmiPending, nmiEdge], already fetched
   */
  updateIRQState(states) {
    const ids = ["irq-pending", "nmi-pending", "nmi-edge"];
    ids.forEach((id, i) => {
      const elem = this.contentElement.querySelector(`#${id}`);
      if (elem) {
        elem.classList.toggle("active", !!states[i]);
      }
    });
  }

  /**
   * Cache beam element references to avoid querySelector every frame
   */
  getBeamElements() {
    if (!this._beamEls) {
      this._beamEls = {
        scan: this.contentElement.querySelector("#scan-line"),
        hPos: this.contentElement.querySelector("#scan-hpos"),
        col: this.contentElement.querySelector("#scan-col"),
        fcyc: this.contentElement.querySelector("#scan-fcyc"),
        badge: this.contentElement.querySelector("#scan-badge"),
      };
    }
    return this._beamEls;
  }

  /**
   * Update scanline / beam position display.
   * Skips redundant DOM writes so browser title tooltips aren't interrupted.
   */
  /**
   * @param {Array} beam - [frameCycle, scanline, hPos, col, inVBL, inHBLANK],
   *                       already fetched by update()
   */
  updateScanline(beam) {
    const [frameCycle, scanline, hPos, col, inVBL, inHBLANK] = beam;

    const els = this.getBeamElements();

    const scanText = String(scanline);
    const hPosText = String(hPos);
    const colText = col >= 0 ? col.toString().padStart(2, "0") : "--";
    const fcycText = String(frameCycle);

    if (els.scan && els.scan.textContent !== scanText) els.scan.textContent = scanText;
    if (els.hPos && els.hPos.textContent !== hPosText) els.hPos.textContent = hPosText;
    if (els.col && els.col.textContent !== colText) els.col.textContent = colText;
    if (els.fcyc && els.fcyc.textContent !== fcycText) els.fcyc.textContent = fcycText;

    if (els.badge) {
      let text, cls;
      if (inVBL) {
        text = "VBL";
        cls = "scanline-badge scanline-badge-vbl";
      } else if (inHBLANK) {
        text = "HBLANK";
        cls = "scanline-badge scanline-badge-hblank";
      } else {
        text = "VISIBLE";
        cls = "scanline-badge scanline-badge-visible";
      }
      if (els.badge.textContent !== text) els.badge.textContent = text;
      if (els.badge.className !== cls) els.badge.className = cls;
    }
  }

  /**
   * Clear scanline / beam position display to "--" while running.
   * Skips redundant DOM writes so browser title tooltips aren't interrupted.
   */
  clearScanline() {
    const els = this.getBeamElements();

    if (els.scan && els.scan.textContent !== "--") els.scan.textContent = "--";
    if (els.hPos && els.hPos.textContent !== "--") els.hPos.textContent = "--";
    if (els.col && els.col.textContent !== "--") els.col.textContent = "--";
    if (els.fcyc && els.fcyc.textContent !== "--") els.fcyc.textContent = "--";
    if (els.badge && els.badge.textContent !== "--") {
      els.badge.textContent = "--";
      els.badge.className = "scanline-badge scanline-badge-idle";
    }
  }

  /**
   * Refresh the profiling heat data backing the disassembly overlay.
   *
   * This pulls the whole 64K cycle-count table, so it is throttled rather than
   * run at the disassembly's ~30fps: at full rate it was re-reading 256KB
   * thirty times a second purely to tint two dozen rows. 4Hz is well inside
   * what reads as "live" for a heat overlay.
   */
  async updateProfileData() {
    if (!this.profileEnabled || !this.wasmModule._getProfileCycles) {
      this._profileMax = 0;
      this._profilePtr = 0;
      this._profileData = null;
      return;
    }

    const now = performance.now();
    if (this._lastProfileRead && now - this._lastProfileRead < PROFILE_REFRESH_MS) {
      return;
    }
    // Called without being awaited, so guard against a slow read overlapping
    // the next one.
    if (this._profileReadPending) return;
    this._lastProfileRead = now;

    let sampleData;
    this._profileReadPending = true;
    try {
      this._profilePtr = await this.wasmModule._getProfileCycles();
      if (!this._profilePtr) {
        this._profileMax = 0;
        this._profileData = null;
        return;
      }
      sampleData = await this.wasmModule.heapReadU32(this._profilePtr, 65536);
    } finally {
      this._profileReadPending = false;
    }

    // Normalise against a sample rather than a full scan — the max only needs
    // to be representative, and every 64th entry is 1024 samples.
    let max = 0;
    for (let i = 0; i < 65536; i += 64) {
      const v = sampleData[i];
      if (v > max) max = v;
    }
    this._profileMax = max;
    this._profileData = sampleData;
  }

  /**
   * Update disassembly view.
   *
   * The whole view is fetched in a single worker round-trip via
   * _disassembleRange — see the comment on that export. Pass isPaused when the
   * caller already knows it (update() does) to avoid one more round-trip.
   */
  async updateDisassembly(isPaused = null) {
    const [pc, blob] = await this.wasmModule.batch([
      ['_getPC'],
      ['__callString', '_disassembleRange',
        this.disasmViewAddress !== null ? this.disasmViewAddress : -1,
        DISASM_LINES_BEFORE, DISASM_TOTAL_LINES],
    ]);
    if (isPaused === null) {
      isPaused = await this.wasmModule._isPaused();
    }
    this.renderDisassembly(blob, pc, isPaused);
  }

  /**
   * Render an already-fetched disassembly blob.
   *
   * Split out from updateDisassembly() so update() can render from its single
   * consolidated batch without a second round-trip. Callers outside the update
   * loop (scrolling the view, committing a register edit) use the async
   * wrapper above.
   *
   * @param {string} blob - newline-separated lines from _disassembleRange
   * @param {number} pc - program counter, for the current-line marker
   * @param {boolean} isPaused - when running, keep the PC line scrolled into view
   */
  renderDisassembly(blob, pc, isPaused) {
    const view = this.contentElement.querySelector("#disasm-view");
    if (!view) return;

    const disasmLines = blob ? blob.split("\n") : [];

    // Fire-and-forget: the heat overlay is throttled to 4Hz and applies from
    // the previous read, so rendering never waits on it.
    this.updateProfileData().catch((e) =>
      console.warn("profile data refresh failed:", e),
    );

    const fragment = document.createDocumentFragment();
    let pcLineElement = null;

    for (const disasm of disasmLines) {
      // Three tab-separated fields: the address in hex, the bytes in hex, and
      // the text. It used to be one fixed-width string sliced by column,
      // which could not survive a six-digit address or a four-byte
      // instruction — and would have read the wrong columns rather than
      // failing, so the view would have looked plausible and been wrong.
      const fields = disasm.split("\t");
      if (fields.length < 3) continue;
      const addr = parseInt(fields[0], 16);
      if (Number.isNaN(addr)) continue;

      // Check for label at this address - show on its own line
      const labelInfo = this.labelManager.getLabel(addr);
      if (labelInfo && labelInfo.name) {
        const labelLine = document.createElement("div");
        labelLine.className = "cpu-disasm-label-line";
        labelLine.textContent = labelInfo.name + ":";
        fragment.appendChild(labelLine);
      }

      const line = document.createElement("div");
      line.className = "cpu-disasm-line";
      line.dataset.addr = addr.toString(16);

      const isCurrent = addr === pc;
      if (isCurrent) {
        line.classList.add("current");
        pcLineElement = line;
      }
      if (this.bpManager.has(addr)) {
        line.classList.add("breakpoint");
      }

      // Heat overlay from profiling
      if (this.profileEnabled && this._profileMax > 0) {
        const heat = this.getHeatLevel(addr);
        if (heat > 0) line.classList.add("heat-" + heat);
      }

      // Breakpoint gutter
      const gutterSpan = document.createElement("span");
      gutterSpan.className = "cpu-disasm-gutter";
      const isBookmarked = this.bookmarks.includes(addr);
      if (isBookmarked) {
        line.classList.add("bookmarked");
      }

      if (this.bpManager.has(addr)) {
        gutterSpan.innerHTML = '<span class="bp-dot"></span>';
      } else if (isCurrent) {
        gutterSpan.innerHTML = '<span class="pc-arrow">▶</span>';
      } else if (isBookmarked) {
        gutterSpan.innerHTML = '<span class="bm-star">★</span>';
      }

      const addrPart = formatMachineAddress(addr);
      const bytesPart = fields[1];
      const instrPart = fields[2];

      const addrSpan = document.createElement("span");
      addrSpan.className = "cpu-disasm-addr";
      addrSpan.textContent = addrPart;

      const bytesSpan = document.createElement("span");
      bytesSpan.className = "cpu-disasm-bytes";
      bytesSpan.textContent = bytesPart;

      const instrSpan = document.createElement("span");
      instrSpan.className = "cpu-disasm-instr";

      // Split mnemonic from operand for proper column alignment and color coding
      const spaceIdx = instrPart.indexOf(" ");
      const mnemonic =
        spaceIdx >= 0 ? instrPart.substring(0, spaceIdx) : instrPart;
      const operandStr = spaceIdx >= 0 ? instrPart.substring(spaceIdx + 1) : "";

      const mnemonicSpan = document.createElement("span");
      mnemonicSpan.className = "cpu-disasm-mnemonic";
      if (CPUDebuggerWindow.FLOW_MNEMONICS.has(mnemonic)) {
        mnemonicSpan.classList.add("flow");
      }
      mnemonicSpan.textContent = mnemonic;
      instrSpan.appendChild(mnemonicSpan);

      if (operandStr) {
        const operandSpan = document.createElement("span");
        operandSpan.className = "cpu-disasm-operand";
        operandSpan.innerHTML = this.symbolizeInstruction(operandStr);
        instrSpan.appendChild(operandSpan);
      }

      line.appendChild(gutterSpan);
      line.appendChild(addrSpan);
      line.appendChild(bytesSpan);
      line.appendChild(instrSpan);

      // Inline comment
      if (labelInfo && labelInfo.comment) {
        const commentSpan = document.createElement("span");
        commentSpan.className = "cpu-disasm-comment";
        commentSpan.textContent = "; " + labelInfo.comment;
        line.appendChild(commentSpan);
      }

      fragment.appendChild(line);
    }

    // Single swap — the rows never exist half-built in the live tree.
    view.replaceChildren(fragment);

    // Scroll to keep PC visible only while running — when paused, let user scroll freely
    if (pcLineElement && !isPaused) {
      const viewRect = view.getBoundingClientRect();
      const lineRect = pcLineElement.getBoundingClientRect();

      // Check if line is outside visible area
      if (lineRect.top < viewRect.top || lineRect.bottom > viewRect.bottom) {
        pcLineElement.scrollIntoView({ block: "center", behavior: "auto" });
      }
    }

    this.lastPC = pc;
  }

  /**
   * Type icon map for breakpoint list display
   */
  /**
   * Control flow mnemonics - colored differently in disassembly
   * to help developers track execution flow at a glance.
   */
  static FLOW_MNEMONICS = new Set([
    "JMP",
    "JSR",
    "RTS",
    "RTI",
    "BRK",
    "BPL",
    "BMI",
    "BVC",
    "BVS",
    "BCC",
    "BCS",
    "BNE",
    "BEQ",
    "BRA",
  ]);

  static BP_TYPE_ICONS = {
    exec: "●",
    read: "R",
    write: "W",
    readwrite: "RW",
  };

  static BP_TYPE_TITLES = {
    exec: "Execution breakpoint",
    read: "Read watchpoint",
    write: "Write watchpoint",
    readwrite: "Read/Write watchpoint",
  };

  static SOFT_SWITCH_GROUPS = [
    {
      category: "Display",
      switches: [
        {
          name: "TEXT",
          start: 0xc050,
          end: 0xc051,
          desc: "Text/Graphics mode",
        },
        {
          name: "MIXED",
          start: 0xc052,
          end: 0xc053,
          desc: "Mixed text+graphics",
        },
        { name: "PAGE2", start: 0xc054, end: 0xc055, desc: "Display page 2" },
        { name: "HIRES", start: 0xc056, end: 0xc057, desc: "Hi-res graphics" },
        {
          name: "80COL",
          start: 0xc00c,
          end: 0xc00d,
          desc: "80-column display",
        },
        {
          name: "ALTCHAR",
          start: 0xc00e,
          end: 0xc00f,
          desc: "Alt charset (MouseText)",
        },
      ],
    },
    {
      category: "Memory Banking",
      switches: [
        {
          name: "80STORE",
          start: 0xc000,
          end: 0xc001,
          desc: "PAGE2 selects aux memory",
        },
        {
          name: "RAMRD",
          start: 0xc002,
          end: 0xc003,
          desc: "Read from aux RAM",
        },
        {
          name: "RAMWRT",
          start: 0xc004,
          end: 0xc005,
          desc: "Write to aux RAM",
        },
        {
          name: "INTCXROM",
          start: 0xc006,
          end: 0xc007,
          desc: "$Cxxx ROM source",
        },
        {
          name: "ALTZP",
          start: 0xc008,
          end: 0xc009,
          desc: "Aux zero page/stack",
        },
        { name: "SLOTC3ROM", start: 0xc00a, end: 0xc00b, desc: "Slot 3 ROM" },
      ],
    },
    {
      category: "Language Card",
      switches: [
        {
          name: "LANGCARD",
          start: 0xc080,
          end: 0xc08f,
          desc: "Language card control",
        },
      ],
    },
    {
      category: "Annunciators",
      switches: [
        { name: "AN0", start: 0xc058, end: 0xc059, desc: "Annunciator 0" },
        { name: "AN1", start: 0xc05a, end: 0xc05b, desc: "Annunciator 1" },
        { name: "AN2", start: 0xc05c, end: 0xc05d, desc: "Annunciator 2" },
        {
          name: "AN3",
          start: 0xc05e,
          end: 0xc05f,
          desc: "Annunciator 3 / DHIRES",
        },
      ],
    },
    {
      category: "I/O",
      switches: [
        { name: "KBD", start: 0xc000, end: 0xc000, desc: "Keyboard data" },
        {
          name: "KBDSTRB",
          start: 0xc010,
          end: 0xc010,
          desc: "Clear keyboard strobe",
        },
        { name: "SPKR", start: 0xc030, end: 0xc030, desc: "Speaker toggle" },
        { name: "PTRIG", start: 0xc070, end: 0xc070, desc: "Paddle trigger" },
      ],
    },
    {
      category: "Status Registers",
      switches: [
        {
          name: "RDLCBNK2",
          start: 0xc011,
          end: 0xc011,
          desc: "LC bank 2 status",
        },
        {
          name: "RDLCRAM",
          start: 0xc012,
          end: 0xc012,
          desc: "LC RAM read status",
        },
        { name: "RDRAMRD", start: 0xc013, end: 0xc013, desc: "RAMRD status" },
        { name: "RDRAMWRT", start: 0xc014, end: 0xc014, desc: "RAMWRT status" },
        {
          name: "RDCXROM",
          start: 0xc015,
          end: 0xc015,
          desc: "INTCXROM status",
        },
        { name: "RDALTZP", start: 0xc016, end: 0xc016, desc: "ALTZP status" },
        {
          name: "RDC3ROM",
          start: 0xc017,
          end: 0xc017,
          desc: "SLOTC3ROM status",
        },
        {
          name: "RD80STORE",
          start: 0xc018,
          end: 0xc018,
          desc: "80STORE status",
        },
        {
          name: "RDVBL",
          start: 0xc019,
          end: 0xc019,
          desc: "Vertical blank status",
        },
        {
          name: "RDTEXT",
          start: 0xc01a,
          end: 0xc01a,
          desc: "TEXT mode status",
        },
        {
          name: "RDMIXED",
          start: 0xc01b,
          end: 0xc01b,
          desc: "MIXED mode status",
        },
        { name: "RDPAGE2", start: 0xc01c, end: 0xc01c, desc: "PAGE2 status" },
        {
          name: "RDHIRES",
          start: 0xc01d,
          end: 0xc01d,
          desc: "HIRES mode status",
        },
        {
          name: "RDALTCHAR",
          start: 0xc01e,
          end: 0xc01e,
          desc: "ALTCHAR status",
        },
        { name: "RD80COL", start: 0xc01f, end: 0xc01f, desc: "80COL status" },
      ],
    },
  ];

  /**
   * Update breakpoint list with rich UI
   */
  updateBreakpointList() {
    const list = this.contentElement.querySelector("#breakpoint-list");
    if (!list) return;

    list.innerHTML = "";
    const allBps = this.bpManager.getAll();
    let count = 0;

    for (const [addr, entry] of allBps) {
      if (entry.isTemp) continue;
      count++;

      const item = document.createElement("div");
      item.className = "cpu-bp-item";
      item.dataset.addr = addr;
      if (!entry.enabled) item.classList.add("disabled");
      if (addr === this._hitBpAddr) item.classList.add("hit");

      const typeIcon = CPUDebuggerWindow.BP_TYPE_ICONS[entry.type] || "●";
      const typeTitle = CPUDebuggerWindow.BP_TYPE_TITLES[entry.type] || "";
      const typeClass =
        entry.type === "exec" ? "bp-type-exec" : "bp-type-watch";

      // Check if this is a named soft switch breakpoint with a range
      const isRange =
        entry.endAddress != null && entry.endAddress !== entry.address;
      const hasName = !!entry.name;

      let html = `
        <span class="bp-enable" title="Toggle enable">
          <input type="checkbox" ${entry.enabled ? "checked" : ""}>
        </span>
        <span class="bp-type ${typeClass}" title="${typeTitle}">${typeIcon}</span>
      `;

      if (hasName) {
        const startHex = formatMachineAddress(entry.address);
        const endHex = formatMachineAddress(entry.endAddress);
        const rangeStr = isRange ? `$${startHex}-${endHex}` : `$${startHex}`;
        html += `<span class="bp-name" title="${rangeStr}">${entry.name}</span>`;
        html += `<span class="bp-range">${rangeStr}</span>`;
      } else {
        html += `<span class="bp-addr">${this.formatAddr(addr)}</span>`;
        // Symbol name for address
        const symbolInfo = getSymbolInfo(addr);
        const label = symbolInfo ? symbolInfo.name : "";
        if (label) {
          html += `<span class="bp-label" title="${label}">${label}</span>`;
        }
      }

      if (entry.condition) {
        html += `<span class="bp-cond" title="Condition: ${entry.condition}">if</span>`;
      }

      if (entry.hitCount > 0 || entry.hitTarget > 0) {
        const hitText =
          entry.hitTarget > 0
            ? `${entry.hitCount}/${entry.hitTarget}`
            : `${entry.hitCount}`;
        html += `<span class="bp-hits" title="Hit count">${hitText}</span>`;
      }

      html += `<button class="bp-edit" title="Edit condition">if&#8230;</button>`;
      html += `<button class="bp-remove" title="Remove">×</button>`;

      item.innerHTML = html;
      list.appendChild(item);
    }

    if (count === 0) {
      const empty = document.createElement("div");
      empty.className = "cpu-dbg-empty-state";
      empty.textContent =
        "Click a disassembly line to toggle a breakpoint, or add one above.";
      list.appendChild(empty);
    }

    // Update tab badge count
    const badge = this.contentElement.querySelector("#bp-tab-count");
    if (badge) {
      badge.textContent = count;
      badge.classList.toggle("has-items", count > 0);
    }
  }

  /**
   * Set the Rule Builder window reference
   */
  setRuleBuilder(ruleBuilderWindow) {
    this.ruleBuilder = ruleBuilderWindow;
  }

  /**
   * Edit condition on a breakpoint via Rule Builder or prompt fallback
   */
  editBreakpointCondition(addr) {
    const entry = this.bpManager.get(addr);
    if (!entry) return;

    if (this.ruleBuilder) {
      this.ruleBuilder.editBreakpoint(addr, entry);
    } else {
      // Fallback to prompt if Rule Builder not wired
      const condition = prompt(
        `Condition for breakpoint at $${formatMachineAddress(addr)}:\n` +
          `Examples: A==#$FF, PEEK($00)==#$42, C==1 && X>=#$10`,
        entry.condition || "",
      );
      if (condition !== null) {
        this.bpManager.setCondition(addr, condition);
      }
    }
  }

  /**
   * Import a symbol file via file picker
   */
  importSymbolFile() {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".dbg,.sym,.txt,.labels,.map";
    input.addEventListener("change", () => {
      const file = input.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        const count = this.labelManager.importSymbolFile(
          reader.result,
          file.name,
        );
        this.updateDisassembly();
        console.log(`Imported ${count} symbols from ${file.name}`);
      };
      reader.readAsText(file);
    });
    input.click();
  }

  /**
   * Edit inline comment at an address
   */
  editInlineComment(addr) {
    const labelInfo = this.labelManager.getLabel(addr);
    const currentComment = labelInfo ? labelInfo.comment : "";

    const comment = prompt(
      `Comment for $${formatMachineAddress(addr)}:`,
      currentComment,
    );

    if (comment !== null) {
      if (comment === "" && labelInfo && !labelInfo.name) {
        // No comment and no label - remove the entry
        this.labelManager.removeLabel(addr);
      } else {
        this.labelManager.setComment(addr, comment);
      }
      this.updateDisassembly();
    }
  }

  // ---- Watch Expressions ----

  static WATCH_STORAGE_KEY = "a2e-watch-expressions";

  loadWatchExpressions() {
    try {
      const saved = localStorage.getItem(CPUDebuggerWindow.WATCH_STORAGE_KEY);
      if (saved) this.watchExpressions = JSON.parse(saved);
    } catch (e) {
      /* ignore */
    }
  }

  saveWatchExpressions() {
    try {
      localStorage.setItem(
        CPUDebuggerWindow.WATCH_STORAGE_KEY,
        JSON.stringify(this.watchExpressions),
      );
    } catch (e) {
      /* ignore */
    }
  }

  static REGISTER_NAMES = new Set(["A", "X", "Y", "SP", "PC", "P"]);

  static FLAG_LABELS = {
    N: "Negative",
    V: "Overflow",
    B: "Break",
    D: "Decimal",
    I: "Interrupt",
    Z: "Zero",
    C: "Carry",
  };

  /**
   * Build a friendly display label and type icon for a watch expression
   */
  getWatchLabel(expr) {
    // Register: single token like "A", "X", "PC"
    if (CPUDebuggerWindow.REGISTER_NAMES.has(expr)) {
      return { icon: "R", iconClass: "watch-icon-reg", label: expr };
    }
    // Flag: single letter N/V/B/D/I/Z/C
    if (CPUDebuggerWindow.FLAG_LABELS[expr]) {
      return {
        icon: "F",
        iconClass: "watch-icon-flag",
        label: CPUDebuggerWindow.FLAG_LABELS[expr],
      };
    }
    // PEEK($XXXX) → byte
    const peekMatch = expr.match(/^PEEK\(\$([0-9A-Fa-f]{1,4})\)$/);
    if (peekMatch) {
      return {
        icon: "B",
        iconClass: "watch-icon-byte",
        label: `$${peekMatch[1].toUpperCase()} byte`,
      };
    }
    // DEEK($XXXX) → word
    const deekMatch = expr.match(/^DEEK\(\$([0-9A-Fa-f]{1,4})\)$/);
    if (deekMatch) {
      return {
        icon: "W",
        iconClass: "watch-icon-word",
        label: `$${deekMatch[1].toUpperCase()} word`,
      };
    }
    // Legacy / custom expression
    return { icon: "E", iconClass: "watch-icon-expr", label: expr };
  }

  /**
   * Format a watch value for display — always shows hex + decimal
   */
  formatWatchValue(expr, value) {
    // Flags: show 0/1 plus set/clear label
    if (CPUDebuggerWindow.FLAG_LABELS[expr]) {
      return value ? "1 (set)" : "0 (clear)";
    }
    // Word values (DEEK)
    if (/^DEEK\(/.test(expr)) {
      return `$${this.formatHex(value & 0xffff, 4)} (${value & 0xffff})`;
    }
    // Byte values and registers
    if (value >= 0 && value <= 0xff) {
      return `$${this.formatHex(value & 0xff, 2)} (${value & 0xff})`;
    }
    return `$${this.formatHex(value & 0xffff, 4)} (${value & 0xffff})`;
  }

  async updateWatchList() {
    const list = this.contentElement.querySelector("#watch-list");
    if (!list) return;

    // Check if DOM structure matches expressions (needs full rebuild if not)
    const existingItems = list.querySelectorAll(".cpu-watch-item");
    const needsRebuild = existingItems.length !== this.watchExpressions.length;

    if (needsRebuild) {
      list.innerHTML = "";
      this.previousWatchValues = {};

      for (let i = 0; i < this.watchExpressions.length; i++) {
        const expr = this.watchExpressions[i];
        let valueStr;
        try {
          const value = await this.bpManager.evaluateValue(expr);
          valueStr = this.formatWatchValue(expr, value);
          this.previousWatchValues[i] = valueStr;
        } catch (e) {
          valueStr = `err: ${e.message}`;
          this.previousWatchValues[i] = valueStr;
        }

        const { icon, iconClass, label } = this.getWatchLabel(expr);
        const item = document.createElement("div");
        item.className = "cpu-watch-item";
        item.dataset.index = i;
        item.innerHTML = `
          <span class="watch-type-icon ${iconClass}" title="${expr}">${icon}</span>
          <span class="watch-expr" title="${expr}">${label}</span>
          <span class="watch-value">${valueStr}</span>
          <button class="watch-remove" title="Remove">×</button>
        `;
        list.appendChild(item);
      }

      if (this.watchExpressions.length === 0) {
        const empty = document.createElement("div");
        empty.className = "cpu-dbg-empty-state";
        empty.textContent =
          "Add watch expressions to monitor values during execution.";
        list.appendChild(empty);
      }
    } else {
      // In-place value update with change highlighting
      for (let i = 0; i < this.watchExpressions.length; i++) {
        const expr = this.watchExpressions[i];
        const item = existingItems[i];
        const valueEl = item.querySelector(".watch-value");
        if (!valueEl) continue;

        let valueStr;
        try {
          const value = await this.bpManager.evaluateValue(expr);
          valueStr = this.formatWatchValue(expr, value);
        } catch (e) {
          valueStr = `err: ${e.message}`;
        }

        const prevVal = this.previousWatchValues[i];
        if (prevVal !== undefined && prevVal !== valueStr) {
          valueEl.classList.remove("changed");
          void valueEl.offsetWidth; // force reflow to restart animation
          valueEl.classList.add("changed");
        }
        this.previousWatchValues[i] = valueStr;
        valueEl.textContent = valueStr;
      }
    }

    // Update tab badge count
    const badge = this.contentElement.querySelector("#watch-tab-count");
    if (badge) {
      badge.textContent = this.watchExpressions.length;
      badge.classList.toggle("has-items", this.watchExpressions.length > 0);
    }
  }

  // ---- Address Bookmarks ----

  static BOOKMARK_STORAGE_KEY = "a2e-bookmarks";

  loadBookmarks() {
    try {
      const saved = localStorage.getItem(
        CPUDebuggerWindow.BOOKMARK_STORAGE_KEY,
      );
      if (saved) this.bookmarks = JSON.parse(saved);
    } catch (e) {
      /* ignore */
    }
  }

  saveBookmarks() {
    try {
      localStorage.setItem(
        CPUDebuggerWindow.BOOKMARK_STORAGE_KEY,
        JSON.stringify(this.bookmarks),
      );
    } catch (e) {
      /* ignore */
    }
  }

  toggleBookmark(addr) {
    const idx = this.bookmarks.indexOf(addr);
    if (idx >= 0) {
      this.bookmarks.splice(idx, 1);
    } else {
      this.bookmarks.push(addr);
    }
    this.saveBookmarks();
    this.updateDisassembly();
  }

  // ---- Beam Breakpoint Persistence ----

  static BEAM_STORAGE_KEY = "a2e-beam-breakpoints";

  async loadBeamBreakpoints() {
    try {
      const saved = localStorage.getItem(CPUDebuggerWindow.BEAM_STORAGE_KEY);
      if (!saved) return;
      const data = JSON.parse(saved);
      for (const bp of data) {
        const id = await this.wasmModule._addBeamBreakpoint(bp.scanline, bp.hPos);
        if (id < 0) continue;
        if (!bp.enabled) {
          this.wasmModule._enableBeamBreakpoint(id, false);
        }
        this.beamBreakpoints.push({
          id,
          scanline: bp.scanline,
          hPos: bp.hPos,
          enabled: bp.enabled,
          mode: bp.mode,
        });
      }
    } catch (e) {
      /* ignore */
    }
  }

  saveBeamBreakpoints() {
    try {
      const data = this.beamBreakpoints.map((bp) => ({
        scanline: bp.scanline,
        hPos: bp.hPos,
        enabled: bp.enabled,
        mode: bp.mode,
      }));
      localStorage.setItem(
        CPUDebuggerWindow.BEAM_STORAGE_KEY,
        JSON.stringify(data),
      );
    } catch (e) {
      /* ignore */
    }
  }

  /**
   * Re-push all beam breakpoints from JS state to C++.
   * Called after state import since importState() calls reset() which
   * clears all WASM-side beam breakpoints.
   */
  async resyncBeamToWasm() {
    if (this.wasmModule._clearAllBeamBreakpoints) {
      this.wasmModule._clearAllBeamBreakpoints();
    }
    for (const bp of this.beamBreakpoints) {
      const newId = await this.wasmModule._addBeamBreakpoint(bp.scanline, bp.hPos);
      if (newId >= 0) {
        bp.id = newId;
        if (!bp.enabled) {
          this.wasmModule._enableBeamBreakpoint(newId, false);
        }
      }
    }
    await this.updateBeamList();
  }

  /**
   * Look up a symbol name for an address. User labels take priority,
   * then imported labels, then built-in symbols.
   */
  lookupSymbol(addr) {
    const label = this.labelManager.getLabel(addr);
    if (label && label.name) {
      return {
        name: label.name,
        desc: label.comment || label.name,
        category: "user",
      };
    }
    return getSymbolInfo(addr);
  }

  symbolizeInstruction(instrText) {
    // First, wrap immediate constants (#$XX or #$XXXX) in spans
    let result = instrText.replace(/#\$([0-9A-Fa-f]{2,4})/g, (match) => {
      return `<span class="cpu-disasm-const">${match}</span>`;
    });

    // Then replace $XXXX patterns (4-digit hex addresses) with symbols
    result = result.replace(
      /\$([0-9A-Fa-f]{4})(?![0-9A-Fa-f])/g,
      (match, hexAddr) => {
        const addr = parseInt(hexAddr, 16);
        const info = this.lookupSymbol(addr);
        if (info) {
          const cssClass =
            info.category === "user"
              ? "cpu-disasm-user-label"
              : getCategoryClass(info.category);
          return `<span class="cpu-disasm-symbol ${cssClass}" data-tooltip="${info.desc}">${info.name}</span>`;
        }
        return match;
      },
    );

    // Also handle 2-digit zero page addresses that have symbols
    result = result.replace(
      /\$([0-9A-Fa-f]{2})(?![0-9A-Fa-f])/g,
      (match, hexAddr) => {
        const addr = parseInt(hexAddr, 16);
        const info = this.lookupSymbol(addr);
        if (info) {
          const cssClass =
            info.category === "user"
              ? "cpu-disasm-user-label"
              : getCategoryClass(info.category);
          return `<span class="cpu-disasm-symbol ${cssClass}" data-tooltip="${info.desc}">${info.name}</span>`;
        }
        return match;
      },
    );

    return result;
  }

  /**
   * Get heat level (1-5) for an address from profiling data
   */
  getHeatLevel(addr) {
    if (!this._profilePtr || this._profileMax === 0 || !this._profileData) return 0;
    const value = this._profileData[addr];
    if (value === 0) return 0;
    const ratio = value / this._profileMax;
    if (ratio > 0.6) return 5;
    if (ratio > 0.3) return 4;
    if (ratio > 0.1) return 3;
    if (ratio > 0.02) return 2;
    return 1;
  }

  getState() {
    const base = super.getState();
    base.activeTab = this.activeTab;
    return base;
  }

  restoreState(state) {
    if (state.activeTab) {
      this.activeTab = state.activeTab;
    }
    super.restoreState(state);
    // Apply tab selection to DOM after restoreState calls show()
    if (this.contentElement && this.activeTab) {
      const tabBar = this.contentElement.querySelector(".cpu-dbg-tab-bar");
      if (tabBar) {
        tabBar.querySelectorAll(".cpu-dbg-tab").forEach((t) => {
          t.classList.toggle("active", t.dataset.tab === this.activeTab);
        });
        this.contentElement
          .querySelectorAll(".cpu-dbg-tab-content")
          .forEach((c) => {
            c.classList.toggle("active", c.dataset.tab === this.activeTab);
          });
      }
    }
  }

}
