/*
 * memory-map-window.js - Memory bank configuration overview window
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import { machineProcessor } from "../machine/machine-profile.js";

// Which region of bank $00/$01 each bit of a IIgs's $C035 inhibits. A set bit
// turns shadowing *off*, which is the way round that catches everybody: the
// register says what is not copied to the Mega II rather than what is.
const SHADOW_REGIONS = [
  { bit: 0x01, name: "Text 1" },
  { bit: 0x02, name: "HiRes 1" },
  { bit: 0x04, name: "HiRes 2" },
  { bit: 0x08, name: "SHR" },
  { bit: 0x10, name: "Aux HiRes" },
  { bit: 0x20, name: "Text 2" },
  { bit: 0x40, name: "I/O + LC" },
];

/**
 * MemoryMapWindow - Visual representation of memory bank configuration
 * Shows which memory banks are currently active based on soft switch states
 */
export class MemoryMapWindow extends BaseWindow {
  constructor(wasmModule) {
    super({
      id: "memory-map",
      title: "Memory Map",
      minWidth: 260,
      minHeight: 405,
      maxWidth: 260,
      maxHeight: 405,
      defaultWidth: 260,
      defaultHeight: 405,
    });

    this.wasmModule = wasmModule;
  }

  renderContent() {
    return `
      <div class="memory-map-content">
        <div class="bank-map">
          <div class="bank-row" id="bank-zp">
            <span class="bank-addr">$0000-$01FF</span>
            <span class="bank-region bank-main" id="bank-zp-main">Main ZP/Stack</span>
            <span class="bank-region bank-aux hidden" id="bank-zp-aux">Aux ZP/Stack</span>
          </div>
          <div class="bank-row" id="bank-0200">
            <span class="bank-addr">$0200-$03FF</span>
            <span class="bank-region bank-main" id="bank-0200-main">Main RAM</span>
            <span class="bank-region bank-aux hidden" id="bank-0200-aux">Aux RAM</span>
          </div>
          <div class="bank-row" id="bank-text1">
            <span class="bank-addr">$0400-$07FF</span>
            <span class="bank-region bank-main" id="bank-text1-main">Main Text 1</span>
            <span class="bank-region bank-aux hidden" id="bank-text1-aux">Aux Text 1</span>
          </div>
          <div class="bank-row" id="bank-0800">
            <span class="bank-addr">$0800-$1FFF</span>
            <span class="bank-region bank-main" id="bank-0800-main">Main RAM</span>
            <span class="bank-region bank-aux hidden" id="bank-0800-aux">Aux RAM</span>
          </div>
          <div class="bank-row" id="bank-hires1">
            <span class="bank-addr">$2000-$3FFF</span>
            <span class="bank-region bank-main" id="bank-hires1-main">Main HiRes 1</span>
            <span class="bank-region bank-aux hidden" id="bank-hires1-aux">Aux HiRes 1</span>
          </div>
          <div class="bank-row" id="bank-hires2">
            <span class="bank-addr">$4000-$5FFF</span>
            <span class="bank-region bank-main" id="bank-hires2-main">Main HiRes 2</span>
            <span class="bank-region bank-aux hidden" id="bank-hires2-aux">Aux HiRes 2</span>
          </div>
          <div class="bank-row" id="bank-6000">
            <span class="bank-addr">$6000-$BFFF</span>
            <span class="bank-region bank-main" id="bank-6000-main">Main RAM</span>
            <span class="bank-region bank-aux hidden" id="bank-6000-aux">Aux RAM</span>
          </div>
          <div class="bank-row" id="bank-c000">
            <span class="bank-addr">$C000-$C0FF</span>
            <span class="bank-region bank-io">I/O Space</span>
          </div>
          <div class="bank-row" id="bank-slot">
            <span class="bank-addr">$C100-$CFFF</span>
            <span class="bank-region bank-rom" id="bank-slot-int">Internal ROM</span>
            <span class="bank-region bank-slot hidden" id="bank-slot-card">Slot ROMs</span>
          </div>
          <div class="bank-row" id="bank-lc">
            <span class="bank-addr">$D000-$FFFF</span>
            <span class="bank-region bank-rom" id="bank-lc-rom">System ROM</span>
            <span class="bank-region bank-ram hidden" id="bank-lc-ram">LC RAM Bnk1</span>
            <span class="bank-region bank-ram hidden" id="bank-lc-ram2">LC RAM Bnk2</span>
          </div>
        </div>
        <div class="bank-legend">
          <span class="legend-item"><span class="legend-box bank-main"></span>Main</span>
          <span class="legend-item"><span class="legend-box bank-aux"></span>Aux</span>
          <span class="legend-item"><span class="legend-box bank-rom"></span>ROM</span>
          <span class="legend-item"><span class="legend-box bank-ram"></span>LC RAM</span>
          <span class="legend-item"><span class="legend-box bank-io"></span>I/O</span>
        </div>
        <div class="bank-status">
          <div class="bank-status-row">
            <span class="bank-status-label">Read:</span>
            <span class="bank-status-value" id="bank-read-status">Main RAM</span>
          </div>
          <div class="bank-status-row">
            <span class="bank-status-label">Write:</span>
            <span class="bank-status-value" id="bank-write-status">Main RAM</span>
          </div>
          <!-- A IIgs's own memory state: which regions of bank $00/$01 are
               copied through to the Mega II, and which clock the processor is
               running on. Hidden on a machine that has neither. -->
          <div class="bank-status-row" id="bank-shadow-row" hidden>
            <span class="bank-status-label">Shadow:</span>
            <span class="bank-status-value" id="bank-shadow-status">--</span>
          </div>
          <div class="bank-status-row" id="bank-speed-row" hidden>
            <span class="bank-status-label">Speed:</span>
            <span class="bank-status-value" id="bank-speed-status">--</span>
          </div>
        </div>
      </div>
    `;
  }

  /**
   * Update the memory bank map visualization
   */
  /**
   * The machine changed, so what the two columns are called did too.
   *
   * The map itself needs no change on a IIgs: bank $00 and bank $01 obey
   * exactly the //e's memory switches, which is what makes it a //e inside.
   * What they are called does change — they are banks rather than a main and
   * an auxiliary board — and a IIgs has two things a //e has not: shadowing,
   * and a second clock.
   */
  onMachineChanged() {
    this.applyProcessor();
  }

  /** Shape the map to the machine: what the two columns are, and what else it has. */
  applyProcessor() {
    if (!this.contentElement) return;
    const banked = machineProcessor().hasBanks;

    // A IIgs's two sides are banks rather than a main and an auxiliary board.
    for (const el of this.contentElement.querySelectorAll(".bank-region")) {
      const aux = el.classList.contains("bank-aux");
      if (!el.dataset.label) el.dataset.label = el.textContent;
      el.textContent = banked
        ? el.dataset.label.replace(/^Main\b/, "$00").replace(/^Aux\b/, "$01")
        : el.dataset.label;
    }
    const legend = this.contentElement.querySelectorAll(".bank-legend .legend-item");
    if (legend.length >= 2) {
      legend[0].lastChild.textContent = banked ? "Bank $00" : "Main";
      legend[1].lastChild.textContent = banked ? "Bank $01" : "Aux";
    }

    // Shadowing and the second clock are a IIgs's alone.
    for (const id of ["bank-shadow-row", "bank-speed-row"]) {
      const row = this.contentElement.querySelector(`#${id}`);
      if (row) row.hidden = !banked;
    }
  }

  create() {
    super.create();
    this.applyProcessor();
  }

  async update(wasmModule) {
    this.wasmModule = wasmModule;
    const banked = machineProcessor().hasBanks;

    // Get soft switch states, and on a IIgs the two registers that are its
    // own: the shadow map and the speed register.
    const results = await wasmModule.batch([
      ['_getSoftSwitchState'],
      ['_getSoftSwitchStateHigh'],
      ...(banked
        ? [['_peekMemory', 0xc035], ['_peekMemory', 0xc036]]
        : []),
    ]);
    const [stateLow, stateHigh] = results;
    // Bit positions for relevant switches
    const ALTZP = 10;
    const STORE80 = 6;
    const PAGE2 = 2;
    const HIRES = 3;
    const RAMRD = 7;
    const RAMWRT = 8;
    const INTCXROM = 9;
    const LCRAM = 13;
    const LCBANK2 = 14;
    const LCWRITE = 15;

    const altzp = (stateLow & (1 << ALTZP)) !== 0;
    const store80 = (stateLow & (1 << STORE80)) !== 0;
    const page2 = (stateLow & (1 << PAGE2)) !== 0;
    const hires = (stateLow & (1 << HIRES)) !== 0;
    const ramrd = (stateLow & (1 << RAMRD)) !== 0;
    const ramwrt = (stateLow & (1 << RAMWRT)) !== 0;
    const intcxrom = (stateLow & (1 << INTCXROM)) !== 0;
    const lcram = (stateLow & (1 << LCRAM)) !== 0;
    const lcbank2 = (stateLow & (1 << LCBANK2)) !== 0;
    const lcwrite = (stateLow & (1 << LCWRITE)) !== 0;

    // Zero Page / Stack: ALTZP controls
    this.toggleBankRegion("bank-zp-main", !altzp);
    this.toggleBankRegion("bank-zp-aux", altzp);

    // $0200-$03FF: RAMRD/RAMWRT controls (not affected by 80STORE)
    this.toggleBankRegion("bank-0200-main", !ramrd);
    this.toggleBankRegion("bank-0200-aux", ramrd);

    // Text Page 1 ($0400-$07FF): 80STORE + PAGE2 or RAMRD controls
    const text1Aux = store80 ? page2 : ramrd;
    this.toggleBankRegion("bank-text1-main", !text1Aux);
    this.toggleBankRegion("bank-text1-aux", text1Aux);

    // $0800-$1FFF: RAMRD/RAMWRT controls
    this.toggleBankRegion("bank-0800-main", !ramrd);
    this.toggleBankRegion("bank-0800-aux", ramrd);

    // HiRes Page 1 ($2000-$3FFF): 80STORE + HIRES + PAGE2 or RAMRD controls
    const hires1Aux = store80 && hires ? page2 : ramrd;
    this.toggleBankRegion("bank-hires1-main", !hires1Aux);
    this.toggleBankRegion("bank-hires1-aux", hires1Aux);

    // HiRes Page 2 ($4000-$5FFF): RAMRD/RAMWRT controls
    this.toggleBankRegion("bank-hires2-main", !ramrd);
    this.toggleBankRegion("bank-hires2-aux", ramrd);

    // $6000-$BFFF: RAMRD/RAMWRT controls
    this.toggleBankRegion("bank-6000-main", !ramrd);
    this.toggleBankRegion("bank-6000-aux", ramrd);

    // Slot ROM ($C100-$CFFF): INTCXROM controls
    this.toggleBankRegion("bank-slot-int", intcxrom);
    this.toggleBankRegion("bank-slot-card", !intcxrom);

    // Language Card ($D000-$FFFF): LCRAM and LCBANK2 control
    this.toggleBankRegion("bank-lc-rom", !lcram);
    this.toggleBankRegion("bank-lc-ram", lcram && !lcbank2);
    this.toggleBankRegion("bank-lc-ram2", lcram && lcbank2);

    // Update status display
    const readStatus = this.contentElement.querySelector("#bank-read-status");
    const writeStatus = this.contentElement.querySelector("#bank-write-status");

    if (readStatus) {
      let readBank = ramrd ? "Aux RAM" : "Main RAM";
      if (altzp) readBank += " (Aux ZP)";
      readStatus.textContent = readBank;
    }

    if (writeStatus) {
      let writeBank = ramwrt ? "Aux RAM" : "Main RAM";
      if (lcwrite) writeBank += " + LC";
      writeStatus.textContent = writeBank;
    }

    if (banked) {
      const shadow = results[2];
      const speed = results[3];
      const shadowed = SHADOW_REGIONS.filter((r) => (shadow & r.bit) === 0)
        .map((r) => r.name)
        .join(", ");
      const shadowEl = this.contentElement.querySelector("#bank-shadow-status");
      if (shadowEl) shadowEl.textContent = shadowed || "none";
      const speedEl = this.contentElement.querySelector("#bank-speed-status");
      if (speedEl) {
        // Bit 7 asks for the fast clock; the bottom four bits are slot motor
        // detect, and a drive turning in an enabled slot holds the whole
        // machine at the Mega II's speed whatever bit 7 says.
        const wantsFast = (speed & 0x80) !== 0;
        const motor = (speed & 0x0f) !== 0;
        speedEl.textContent = wantsFast
          ? motor
            ? "1.02 MHz (slot motor)"
            : "2.8 MHz"
          : "1.02 MHz";
      }
    }
  }

  /**
   * Toggle visibility of a bank region element
   */
  toggleBankRegion(id, show) {
    const el = this.contentElement.querySelector(`#${id}`);
    if (el) {
      el.classList.toggle("hidden", !show);
    }
  }
}
