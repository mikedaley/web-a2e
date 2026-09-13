/*
 * soft-switch-window.js - Soft switch monitor window displaying switch states and addresses
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import { machineProcessor } from "../machine/machine-profile.js";

// The registers a IIgs has and no other Apple II does.
//
// They are bytes rather than one-bit switches, so they cannot join the packed
// switch word: each is read by peeking its address, which a debugger may do
// without disturbing the machine, and shown as a value. Between them they are
// most of what makes a IIgs a IIgs rather than a fast //e.
const IIGS_REGISTERS = [
  { addr: 0xc029, name: "NEWVIDEO", desc: "Super Hi-Res on (bit 7), linear video memory" },
  { addr: 0xc022, name: "TCOLOR", desc: "Text foreground and background colour" },
  { addr: 0xc034, name: "BORDER", desc: "Border colour, low nibble (the clock has the high)" },
  { addr: 0xc035, name: "SHADOW", desc: "Shadowing off per region — a set bit is off" },
  { addr: 0xc036, name: "CYAREG", desc: "Fast speed (bit 7), slot motor detect (bits 0-3)" },
  { addr: 0xc068, name: "STATEREG", desc: "Eight of the //e's memory switches in one byte" },
  { addr: 0xc02d, name: "SLOTREG", desc: "Slots answering with a card rather than the firmware" },
  { addr: 0xc023, name: "VGCINT", desc: "VGC interrupt enables and flags" },
  { addr: 0xc041, name: "INTEN", desc: "Mega II interrupt enables" },
  { addr: 0xc046, name: "INTFLAG", desc: "Mega II interrupt flags" },
  { addr: 0xc02e, name: "VERTCNT", desc: "Vertical counter, as the VGC reports it" },
  { addr: 0xc02f, name: "HORIZCNT", desc: "Horizontal counter" },
];

export class SoftSwitchWindow extends BaseWindow {
  constructor(wasmModule) {
    super({
      id: "soft-switches",
      title: "Soft Switches",
      minWidth: 325,
      minHeight: 200,
      maxWidth: 325,
      maxHeight: Infinity,
      defaultWidth: 325,
      defaultHeight: 500,
    });

    this.wasmModule = wasmModule;

    // Define all soft switches with their bit positions, addresses, and descriptions
    // Bit positions match the 64-bit state returned by getSoftSwitchState/getSoftSwitchStateHigh
    this.switchGroups = [
      {
        title: "Display Mode",
        switches: [
          {
            id: "text",
            bit: 0,
            name: "TEXT",
            addr: "$C050/51",
            desc: "Text mode",
          },
          {
            id: "mixed",
            bit: 1,
            name: "MIXED",
            addr: "$C052/53",
            desc: "Mixed text+graphics",
          },
          {
            id: "page2",
            bit: 2,
            name: "PAGE2",
            addr: "$C054/55",
            desc: "Display page 2",
          },
          {
            id: "hires",
            bit: 3,
            name: "HIRES",
            addr: "$C056/57",
            desc: "Hi-res graphics",
          },
          {
            id: "col80",
            bit: 4,
            name: "80COL",
            addr: "$C00C/0D",
            desc: "80 column mode",
          },
          {
            id: "altchar",
            bit: 5,
            name: "ALTCHAR",
            addr: "$C00E/0F",
            desc: "Alt charset (MouseText)",
          },
          {
            id: "dhires",
            bit: 28,
            name: "DHIRES",
            addr: "computed",
            desc: "Double hi-res active",
          },
        ],
      },
      {
        title: "Memory Banking",
        switches: [
          {
            id: "store80",
            bit: 6,
            name: "80STORE",
            addr: "$C000/01",
            desc: "PAGE2 selects aux mem",
          },
          {
            id: "ramrd",
            bit: 7,
            name: "RAMRD",
            addr: "$C002/03",
            desc: "Read from aux RAM",
          },
          {
            id: "ramwrt",
            bit: 8,
            name: "RAMWRT",
            addr: "$C004/05",
            desc: "Write to aux RAM",
          },
          {
            id: "intcxrom",
            bit: 9,
            name: "INTCXROM",
            addr: "$C006/07",
            desc: "Internal $Cxxx ROM",
          },
          {
            id: "altzp",
            bit: 10,
            name: "ALTZP",
            addr: "$C008/09",
            desc: "Aux zero page/stack",
          },
          {
            id: "slotc3rom",
            bit: 11,
            name: "SLOTC3ROM",
            addr: "$C00A/0B",
            desc: "Slot 3 ROM enabled",
          },
          {
            id: "intc8rom",
            bit: 12,
            name: "INTC8ROM",
            addr: "internal",
            desc: "Internal $C800 ROM",
          },
        ],
      },
      {
        title: "Language Card",
        switches: [
          {
            id: "lcram",
            bit: 13,
            name: "LCRAM",
            addr: "$C080-8F",
            desc: "LC RAM read enabled",
          },
          {
            id: "lcbank2",
            bit: 14,
            name: "LCBANK2",
            addr: "$C080-8F",
            desc: "LC bank 2 selected",
          },
          {
            id: "lcwrite",
            bit: 15,
            name: "LCWRITE",
            addr: "$C080-8F",
            desc: "LC RAM write enabled",
          },
          {
            id: "lcprewrite",
            bit: 16,
            name: "LCPREWRT",
            addr: "$C080-8F",
            desc: "LC pre-write state",
          },
        ],
      },
      {
        title: "Annunciators",
        switches: [
          {
            id: "an0",
            bit: 17,
            name: "AN0",
            addr: "$C058/59",
            desc: "Annunciator 0",
          },
          {
            id: "an1",
            bit: 18,
            name: "AN1",
            addr: "$C05A/5B",
            desc: "Annunciator 1",
          },
          {
            id: "an2",
            bit: 19,
            name: "AN2",
            addr: "$C05C/5D",
            desc: "Annunciator 2",
          },
          {
            id: "an3",
            bit: 20,
            name: "AN3",
            addr: "$C05E/5F",
            desc: "Annunciator 3 / DHIRES",
          },
        ],
      },
      {
        title: "I/O Status",
        switches: [
          {
            id: "vblbar",
            bit: 21,
            name: "VBLBAR",
            addr: "$C019",
            desc: "Vertical blank",
            readOnly: true,
          },
          {
            id: "cassout",
            bit: 22,
            name: "CASSOUT",
            addr: "$C020",
            desc: "Cassette output",
          },
          {
            id: "cassin",
            bit: 23,
            name: "CASSIN",
            addr: "$C060",
            desc: "Cassette input",
            readOnly: true,
          },
        ],
      },
      {
        title: "Buttons",
        switches: [
          {
            id: "btn0",
            bit: 24,
            name: "BTN0",
            addr: "$C061",
            desc: "Open Apple / Button 0",
            readOnly: true,
          },
          {
            id: "btn1",
            bit: 25,
            name: "BTN1",
            addr: "$C062",
            desc: "Closed Apple / Button 1",
            readOnly: true,
          },
          {
            id: "btn2",
            bit: 26,
            name: "BTN2",
            addr: "$C063",
            desc: "Button 2 / Shift",
            readOnly: true,
          },
        ],
      },
      {
        title: "Keyboard",
        switches: [
          {
            id: "keyavail",
            bit: 27,
            name: "KEYAVAIL",
            addr: "$C000",
            desc: "Key available (bit 7)",
            readOnly: true,
          },
        ],
      },
      {
        title: "Other",
        switches: [
          {
            id: "ioudis",
            bit: 29,
            name: "IOUDIS",
            addr: "$C07E/7F",
            desc: "IOU disable (IIc)",
          },
        ],
      },
    ];

    // Reference addresses (read-only status registers)
    this.statusRegisters = [
      { addr: "$C011", name: "RDLCBNK2", desc: "LC bank 2 selected" },
      { addr: "$C012", name: "RDLCRAM", desc: "LC RAM read enabled" },
      { addr: "$C013", name: "RDRAMRD", desc: "Aux RAM read" },
      { addr: "$C014", name: "RDRAMWRT", desc: "Aux RAM write" },
      { addr: "$C015", name: "RDCXROM", desc: "Internal $Cxxx ROM" },
      { addr: "$C016", name: "RDALTZP", desc: "Aux zero page" },
      { addr: "$C017", name: "RDC3ROM", desc: "Slot 3 ROM" },
      { addr: "$C018", name: "RD80STORE", desc: "80STORE enabled" },
      { addr: "$C019", name: "RDVBLBAR", desc: "Vertical blank" },
      { addr: "$C01A", name: "RDTEXT", desc: "Text mode" },
      { addr: "$C01B", name: "RDMIXED", desc: "Mixed mode" },
      { addr: "$C01C", name: "RDPAGE2", desc: "Page 2" },
      { addr: "$C01D", name: "RDHIRES", desc: "Hi-res mode" },
      { addr: "$C01E", name: "RDALTCHAR", desc: "Alt charset" },
      { addr: "$C01F", name: "RD80COL", desc: "80 column mode" },
    ];

    // Other I/O addresses (for reference)
    this.ioAddresses = [
      { addr: "$C010", name: "KBDSTRB", desc: "Clear keyboard strobe" },
      { addr: "$C030", name: "SPKR", desc: "Speaker toggle" },
      { addr: "$C040", name: "STROBE", desc: "Utility strobe" },
      { addr: "$C064", name: "PDL0", desc: "Paddle 0 (joystick X)" },
      { addr: "$C065", name: "PDL1", desc: "Paddle 1 (joystick Y)" },
      { addr: "$C066", name: "PDL2", desc: "Paddle 2" },
      { addr: "$C067", name: "PDL3", desc: "Paddle 3" },
      { addr: "$C070", name: "PTRIG", desc: "Paddle trigger" },
    ];

    // Slot I/O ranges
    this.slotRanges = [
      { range: "$C090-9F", slot: 1, desc: "Slot 1 I/O" },
      { range: "$C0A0-AF", slot: 2, desc: "Slot 2 I/O" },
      { range: "$C0B0-BF", slot: 3, desc: "Slot 3 I/O" },
      { range: "$C0C0-CF", slot: 4, desc: "Slot 4 I/O" },
      { range: "$C0D0-DF", slot: 5, desc: "Slot 5 I/O" },
      { range: "$C0E0-EF", slot: 6, desc: "Slot 6 I/O (Disk II)" },
      { range: "$C0F0-FF", slot: 7, desc: "Slot 7 I/O" },
    ];
  }

  /** The machine's own registers, if it has any beyond the //e's switches. */
  machineRegisters() {
    return machineProcessor().hasBanks ? IIGS_REGISTERS : [];
  }

  renderContent() {
    let html = '<div class="softswitch-content">';

    // The machine's own registers first: on a IIgs they decide what the //e
    // switches below even mean — whether a write is shadowed, which side of
    // the machine a bank is on, and how fast the processor is going.
    const registers = this.machineRegisters();
    if (registers.length) {
      html += `
        <div class="switch-group">
          <div class="switch-group-title">Machine Registers</div>
          <div class="switch-list">
      `;
      for (const reg of registers) {
        const hex = reg.addr.toString(16).toUpperCase();
        html += `
          <div class="switch-item read-only">
            <span class="switch-addr">$${hex}</span>
            <span class="switch-badge active" id="reg-${hex}">${reg.name}</span>
            <span class="switch-value" id="regval-${hex}">--</span>
            <span class="switch-desc">${reg.desc}</span>
          </div>
        `;
      }
      html += `
          </div>
        </div>
      `;
    }

    // Render switch groups
    for (const group of this.switchGroups) {
      html += `
        <div class="switch-group">
          <div class="switch-group-title">${group.title}</div>
          <div class="switch-list">
      `;

      for (const sw of group.switches) {
        const readOnlyClass = sw.readOnly ? " read-only" : "";
        html += `
          <div class="switch-item${readOnlyClass}" id="sw-item-${sw.id}">
            <span class="switch-addr">${sw.addr}</span>
            <span class="switch-badge" id="sw-${sw.id}">${sw.name}</span>
            <span class="switch-desc">${sw.desc}</span>
          </div>
        `;
      }

      html += `
          </div>
        </div>
      `;
    }

    // Add collapsible reference section
    html += `
      <div class="switch-group reference-section">
        <div class="switch-group-title collapsible" id="ref-toggle">
          ▶ I/O Reference
        </div>
        <div class="switch-list reference-list hidden" id="ref-content">
    `;

    // Status registers
    html += '<div class="ref-subtitle">Status Registers ($C011-$C01F)</div>';
    for (const reg of this.statusRegisters) {
      html += `
        <div class="ref-item">
          <span class="ref-addr">${reg.addr}</span>
          <span class="ref-name">${reg.name}</span>
          <span class="ref-desc">${reg.desc}</span>
        </div>
      `;
    }

    // Other I/O
    html += '<div class="ref-subtitle">Other I/O</div>';
    for (const io of this.ioAddresses) {
      html += `
        <div class="ref-item">
          <span class="ref-addr">${io.addr}</span>
          <span class="ref-name">${io.name}</span>
          <span class="ref-desc">${io.desc}</span>
        </div>
      `;
    }

    // Slot I/O
    html += '<div class="ref-subtitle">Slot I/O</div>';
    for (const slot of this.slotRanges) {
      html += `
        <div class="ref-item">
          <span class="ref-addr">${slot.range}</span>
          <span class="ref-name">Slot ${slot.slot}</span>
          <span class="ref-desc">${slot.desc}</span>
        </div>
      `;
    }

    html += `
        </div>
      </div>
    `;

    html += "</div>";
    return html;
  }

  /**
   * Called after content is rendered
   */
  onContentRendered() {
    // Set up collapsible reference section
    const toggle = this.contentElement.querySelector("#ref-toggle");
    const content = this.contentElement.querySelector("#ref-content");

    if (toggle && content) {
      toggle.addEventListener("click", () => {
        content.classList.toggle("hidden");
        toggle.textContent = content.classList.contains("hidden")
          ? "▶ I/O Reference"
          : "▼ I/O Reference";
      });
    }

    // Cache badge elements once. update() runs ~15x/second, so resolving 35
    // querySelectors per tick — and touching classList on badges that have not
    // changed — was invalidating style/paint for the whole window every tick.
    this.badges = [];
    for (const group of this.switchGroups) {
      for (const sw of group.switches) {
        const el = this.contentElement.querySelector(`#sw-${sw.id}`);
        if (el) this.badges.push({ bit: sw.bit, el });
      }
    }
    this.lastStateLow = null;
    this.lastStateHigh = null;

    // The machine's own registers, and where their values are shown.
    this.registerCells = this.machineRegisters()
      .map((reg) => ({
        addr: reg.addr,
        el: this.contentElement.querySelector(
          `#regval-${reg.addr.toString(16).toUpperCase()}`,
        ),
      }))
      .filter((cell) => cell.el);
    this.lastRegisterValues = [];
  }

  /** The machine changed, so the registers it has did too. */
  onMachineChanged() {
    // The list is part of the markup, so the window is rebuilt from scratch.
    this.badges = null;
    this.registerCells = [];
    if (this.contentElement) {
      this.contentElement.innerHTML = this.renderContent();
      this.onContentRendered?.();
    }
  }

  /**
   * Update all soft switch states
   */
  async update(wasmModule) {
    this.wasmModule = wasmModule;
    if (!this.badges) return;

    // One RPC in flight at a time: update() is fired from the render loop
    // without being awaited, so a slow round-trip would otherwise let requests
    // stack up faster than the Worker can answer them.
    if (this._updatePending) return;
    this._updatePending = true;

    let stateLow, stateHigh, registerValues = [];
    try {
      // The packed switch word and the machine's own registers in one batch:
      // the registers are peeks, which do not disturb the machine.
      const cells = this.registerCells ?? [];
      const results = await wasmModule.batch([
        ['_getSoftSwitchState'],
        ['_getSoftSwitchStateHigh'],
        ...cells.map((cell) => ['_peekMemory', cell.addr]),
      ]);
      [stateLow, stateHigh] = results;
      registerValues = results.slice(2);
    } finally {
      this._updatePending = false;
    }

    for (let i = 0; i < registerValues.length; i++) {
      if (registerValues[i] === this.lastRegisterValues[i]) continue;
      this.lastRegisterValues[i] = registerValues[i];
      this.registerCells[i].el.textContent =
        "$" + this.formatHex(registerValues[i], 2);
    }

    // Nothing changed — skip the DOM entirely. Without this, re-toggling the
    // same classes repaints the window (and its backdrop-filter) every tick.
    if (stateLow === this.lastStateLow && stateHigh === this.lastStateHigh) {
      return;
    }

    const changedLow = this.lastStateLow === null ? ~0 : stateLow ^ this.lastStateLow;
    const changedHigh = this.lastStateHigh === null ? ~0 : stateHigh ^ this.lastStateHigh;
    this.lastStateLow = stateLow;
    this.lastStateHigh = stateHigh;

    for (const { bit, el } of this.badges) {
      const [state, changed, mask] =
        bit < 32
          ? [stateLow, changedLow, 1 << bit]
          : [stateHigh, changedHigh, 1 << (bit - 32)];
      if ((changed & mask) === 0) continue;
      el.classList.toggle("active", (state & mask) !== 0);
    }
  }
}
