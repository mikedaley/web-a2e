/*
 * machine-selector-window.js - Choose which Apple II the emulator models
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import { showConfirm } from "../ui/confirm.js";
import {
  getMachineProfile,
  listMachineProfiles,
  switchMachine,
} from "./machine-profile.js";

/*
 * The machines are drawn rather than photographed: a monitor sitting on the
 * wedge case, which is the silhouette anyone who used one recognises straight
 * away. They share that outline deliberately, because these are the same
 * computer eight years apart, and the details that differ are the ones the
 * profile actually models.
 *
 * The screen shows the prompt the machine boots to, and it lights up green on
 * the machine that is currently running — the same green as the first stripe
 * of the Apple logo, and the same green a monochrome monitor of the period
 * would have shown.
 */
function machineArt({ appleKeys, keyRows, deckTop }) {
  const rows = [];
  for (let i = 0; i < keyRows; i++) {
    const y = deckTop + 7 + i * 6;
    rows.push(`<path d="M${30 + i} ${y} H${132 - i}"/>`);
  }

  // The //e put an Open and a Closed Apple either side of the space bar. A II+
  // had neither, which is why its games ask you to press a paddle button.
  const apples = appleKeys
    ? `<rect x="56" y="${deckTop + 24}" width="6" height="5" rx="1"
             class="machine-art-apple-keys" stroke="currentColor" stroke-width="1"/>
       <rect x="100" y="${deckTop + 24}" width="6" height="5" rx="1"
             class="machine-art-apple-keys" stroke="currentColor" stroke-width="1"/>`
    : "";

  return `
    <svg viewBox="0 0 160 116" fill="none" aria-hidden="true">
      <!-- monitor -->
      <rect x="33" y="4" width="94" height="66" rx="6"
            stroke="currentColor" stroke-width="2"/>
      <rect x="41" y="12" width="78" height="46" rx="3"
            class="machine-art-screen" stroke="currentColor" stroke-width="1"/>
      <text x="47" y="34" class="machine-art-prompt"
            font-family="ui-monospace, monospace" font-size="13">]</text>
      <circle cx="121" cy="64" r="1.6" class="machine-art-led"/>
      <!-- stand -->
      <path d="M72 70 h16 l3 6 h-22 z" stroke="currentColor" stroke-width="1.5"
            stroke-linejoin="round"/>
      <!-- case -->
      <path d="M12 112 L26 ${deckTop} H146 L140 112 Z"
            stroke="currentColor" stroke-width="2" stroke-linejoin="round"/>
      <path d="M26 ${deckTop} H146" stroke="currentColor" stroke-width="1.5" opacity="0.45"/>
      <g class="machine-art-keys" stroke="currentColor" stroke-width="1" opacity="0.7">
        ${rows.join("\n        ")}
      </g>
      <rect x="66" y="${deckTop + 24}" width="28" height="5" rx="2"
            stroke="currentColor" stroke-width="1" opacity="0.7"/>
      ${apples}
    </svg>`;
}

const MACHINE_ART = {
  // Three key rows and the two Apple keys: the enhanced //e keyboard.
  apple2e: machineArt({ appleKeys: true, keyRows: 3, deckTop: 82 }),
  // A shallower deck and two rows, and no Apple keys at all.
  apple2plus: machineArt({ appleKeys: false, keyRows: 2, deckTop: 86 }),
};

const FALLBACK_ART = MACHINE_ART.apple2e;

/** Bytes as the machine's own marketing would have said it. */
function formatK(bytes) {
  if (!bytes) return "None";
  return `${Math.round(bytes / 1024)}K`;
}

/**
 * MachineSelectorWindow - pick the machine the emulator models.
 *
 * Switching is destructive: the core cannot convert a running machine into a
 * different one, so it rebuilds the emulator and everything in it is lost. The
 * window says so plainly and asks before doing it, rather than presenting the
 * choice as a harmless toggle.
 */
export class MachineSelectorWindow extends BaseWindow {
  constructor(wasmModule, onMachineChanged) {
    super({
      id: "machine-selector",
      title: "Machine",
      minWidth: 400,
      minHeight: 420,
      defaultWidth: 440,
      defaultHeight: 560,
      cssClasses: { window: "debug-window machine-window" },
      resizeDirections: ["n", "s", "e", "w", "ne", "nw", "se", "sw"],
      storageKey: "a2e-machine-selector-window",
    });

    this.wasmModule = wasmModule;
    this.onMachineChanged = onMachineChanged;
    this.machines = [];
    this.selectedKey = null;
    this.currentKey = null;
    this.switching = false;
  }

  renderContent() {
    return `
      <div class="machine-content">
        <div class="machine-list" id="machine-list"></div>
        <div class="machine-footer">
          <p class="machine-warning" id="machine-warning"></p>
          <button class="machine-switch-btn" id="machine-switch-btn" disabled>
            Switch Machine
          </button>
        </div>
      </div>`;
  }

  async create() {
    super.create();
    this.listEl = this.contentElement.querySelector("#machine-list");
    this.warningEl = this.contentElement.querySelector("#machine-warning");
    this.switchBtn = this.contentElement.querySelector("#machine-switch-btn");
    this.switchBtn.addEventListener("click", () => this.applySwitch());
    await this.refresh();
  }

  // The machine can change from elsewhere — the agent, another window — so the
  // list is re-read every time this is opened rather than cached at creation.
  show() {
    super.show();
    this.refresh();
  }

  /** Re-read the machine list and the machine in force. */
  async refresh() {
    if (!this.wasmModule) return;
    this.machines = await listMachineProfiles(this.wasmModule);
    this.currentKey = getMachineProfile().key;
    if (!this.selectedKey) this.selectedKey = this.currentKey;
    this.render();
  }

  render() {
    if (!this.listEl) return;

    this.listEl.innerHTML = this.machines
      .map((m) => this.cardHTML(m))
      .join("");

    for (const card of this.listEl.querySelectorAll(".machine-card")) {
      if (card.classList.contains("unavailable")) continue;
      card.addEventListener("click", () => {
        this.selectedKey = card.dataset.key;
        this.render();
      });
    }

    this.renderFooter();
  }

  cardHTML(m) {
    const isCurrent = m.key === this.currentKey;
    const isSelected = m.key === this.selectedKey;
    const unavailable = m.runnable === false;

    const classes = ["machine-card"];
    if (isCurrent) classes.push("current");
    if (isSelected) classes.push("selected");
    if (unavailable) classes.push("unavailable");

    // The six-stripe Apple rainbow marks the machine actually running. It is
    // the app's own palette, and it is the one thing on the card that says
    // "this is the computer you are using" without a word.
    const stripes = isCurrent
      ? `<div class="machine-stripes" aria-hidden="true">
           <i style="background:var(--accent-green)"></i>
           <i style="background:var(--accent-yellow)"></i>
           <i style="background:var(--accent-orange)"></i>
           <i style="background:var(--accent-red)"></i>
           <i style="background:var(--accent-purple)"></i>
           <i style="background:var(--accent-blue)"></i>
         </div>`
      : "";

    let badge = "";
    if (isCurrent) {
      badge = `<span class="machine-badge running">Running</span>`;
    } else if (unavailable) {
      badge = `<span class="machine-badge missing">No ROM</span>`;
    }

    const ram = m.memory?.auxRamSize
      ? `${formatK(m.memory.mainRamSize)} + ${formatK(m.memory.auxRamSize)} aux`
      : formatK(m.memory?.mainRamSize);

    const video = m.caps?.has80Column ? "40 / 80 column" : "40 column";
    const slots = `${m.firstSlot ?? 1}–${m.lastSlot ?? 7}`;

    const note = unavailable
      ? `<p class="machine-note">Its ROM images are not built in, so this
         machine cannot be started. Add them to <code>roms/</code> and rebuild.</p>`
      : "";

    return `
      <button class="${classes.join(" ")}" data-key="${m.key}"
              ${unavailable ? "disabled" : ""} type="button">
        ${stripes}
        <div class="machine-card-main">
          <div class="machine-art">${MACHINE_ART[m.key] || FALLBACK_ART}</div>
          <div class="machine-detail">
            <div class="machine-name-row">
              <span class="machine-name">${m.name}</span>
              ${badge}
            </div>
            <dl class="machine-specs">
              <div><dt>CPU</dt><dd>${m.cpu}</dd></div>
              <div><dt>RAM</dt><dd>${ram}</dd></div>
              <div><dt>Video</dt><dd>${video}</dd></div>
              <div><dt>Slots</dt><dd>${slots}</dd></div>
            </dl>
            ${note}
          </div>
        </div>
      </button>`;
  }

  renderFooter() {
    const changing = this.selectedKey && this.selectedKey !== this.currentKey;
    const target = this.machines.find((m) => m.key === this.selectedKey);

    this.switchBtn.disabled = !changing || this.switching;
    this.switchBtn.textContent = this.switching
      ? "Switching…"
      : changing && target
        ? `Switch to ${target.shortName || target.name}`
        : "Switch Machine";

    this.warningEl.textContent = changing
      ? "Switching rebuilds the machine from scratch. Inserted disks, hard " +
        "drives and anything in memory are lost, exactly as they would be on " +
        "a reload."
      : "";
    this.warningEl.classList.toggle("visible", !!changing);
  }

  async applySwitch() {
    if (this.switching) return;
    const target = this.machines.find((m) => m.key === this.selectedKey);
    if (!target || target.key === this.currentKey) return;

    const ok = await showConfirm(
      `Switch to the ${target.name}?\n\n` +
        "The machine is rebuilt from scratch. Inserted disks, hard drives and " +
        "anything in memory are lost.",
      "Switch",
    );
    if (!ok) return;

    this.switching = true;
    this.renderFooter();

    const profile = await switchMachine(this.wasmModule, target.key);

    this.switching = false;
    if (!profile) {
      this.warningEl.textContent = `The core refused to switch to ${target.name}.`;
      this.warningEl.classList.add("visible");
      return;
    }

    this.currentKey = profile.key;
    this.selectedKey = profile.key;
    if (this.onMachineChanged) await this.onMachineChanged(profile);
    await this.refresh();
  }
}
