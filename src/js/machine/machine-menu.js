/*
 * machine-menu.js - The header machine badge and its dropdown
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

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
 * profile actually models — a //e's three key rows and its two Apple keys
 * against a II+'s shallower deck and two rows, and the //c's drive slot in
 * the flank of the case where the other two have a row of sockets.
 */
function machineArt({ appleKeys, keyRows, deckTop, driveSlot = false }) {
  const rows = [];
  for (let i = 0; i < keyRows; i++) {
    const y = deckTop + 7 + i * 6;
    rows.push(`<path d="M${30 + i} ${y} H${132 - i}"/>`);
  }

  // The //e put an Open and a Closed Apple either side of the space bar. A II+
  // had neither, which is why its games ask you to press a paddle button.
  const apples = appleKeys
    ? `<rect x="56" y="${deckTop + 24}" width="6" height="5" rx="1"
             stroke="currentColor" stroke-width="1"/>
       <rect x="100" y="${deckTop + 24}" width="6" height="5" rx="1"
             stroke="currentColor" stroke-width="1"/>`
    : "";

  // The //c is the machine with the disk in it. Everything else here is a box
  // that needs a card and a cable before it can read one, so the slot cut into
  // the right flank is the whole silhouette's worth of difference.
  const drive = driveSlot
    ? `<path d="M137 ${deckTop + 7} L135 ${deckTop + 19}" stroke="currentColor"
             stroke-width="2.5" stroke-linecap="round" opacity="0.8"/>`
    : "";

  return `
    <svg viewBox="0 0 160 116" fill="none" aria-hidden="true">
      <rect x="33" y="4" width="94" height="66" rx="6"
            stroke="currentColor" stroke-width="2"/>
      <rect x="41" y="12" width="78" height="46" rx="3"
            class="machine-art-screen" stroke="currentColor" stroke-width="1"/>
      <text x="47" y="34" class="machine-art-prompt"
            font-family="ui-monospace, monospace" font-size="13">]</text>
      <path d="M72 70 h16 l3 6 h-22 z" stroke="currentColor" stroke-width="1.5"
            stroke-linejoin="round"/>
      <path d="M12 112 L26 ${deckTop} H146 L140 112 Z"
            stroke="currentColor" stroke-width="2" stroke-linejoin="round"/>
      <path d="M26 ${deckTop} H146" stroke="currentColor" stroke-width="1.5" opacity="0.45"/>
      <g stroke="currentColor" stroke-width="1" opacity="0.7">
        ${rows.join("\n        ")}
      </g>
      <rect x="66" y="${deckTop + 24}" width="28" height="5" rx="2"
            stroke="currentColor" stroke-width="1" opacity="0.7"/>
      ${apples}
      ${drive}
    </svg>`;
}

/*
 * A IIgs is not the same shape as the others and cannot be drawn by adding
 * details to their silhouette: the case is a flat slab with the monitor
 * standing on it and the keyboard a separate thing in front, which is the
 * outline of a machine you would recognise across a room as not being a //e.
 */
function iigsArt() {
  return `
    <svg viewBox="0 0 160 116" fill="none" aria-hidden="true">
      <rect x="36" y="2" width="88" height="62" rx="5"
            stroke="currentColor" stroke-width="2"/>
      <rect x="44" y="9" width="72" height="44" rx="3"
            class="machine-art-screen" stroke="currentColor" stroke-width="1"/>
      <text x="50" y="30" class="machine-art-prompt"
            font-family="ui-monospace, monospace" font-size="13">]</text>
      <path d="M22 70 H138 L142 88 H18 Z" stroke="currentColor" stroke-width="2"
            stroke-linejoin="round"/>
      <path d="M120 76 h12" stroke="currentColor" stroke-width="1.5"
            opacity="0.7"/>
      <path d="M120 82 h12" stroke="currentColor" stroke-width="1.5"
            opacity="0.7"/>
      <rect x="30" y="94" width="100" height="18" rx="2"
            stroke="currentColor" stroke-width="2"/>
      <g stroke="currentColor" stroke-width="1" opacity="0.7">
        <path d="M36 100 H124"/>
        <path d="M36 105 H124"/>
      </g>
    </svg>`;
}

const MACHINE_ART = {
  apple2e: machineArt({ appleKeys: true, keyRows: 3, deckTop: 82 }),
  apple2plus: machineArt({ appleKeys: false, keyRows: 2, deckTop: 86 }),
  apple2c: machineArt({
    appleKeys: true,
    keyRows: 3,
    deckTop: 84,
    driveSlot: true,
  }),
  apple2gs: iigsArt(),
};

/** Bytes as the machine's own marketing would have said it. */
function formatK(bytes) {
  return bytes ? `${Math.round(bytes / 1024)}K` : "";
}

/** The one-line summary under a machine's name. */
function specLine(m) {
  const ram = m.memory?.auxRamSize
    ? formatK(m.memory.mainRamSize + m.memory.auxRamSize)
    : formatK(m.memory?.mainRamSize);
  const columns = m.caps?.has80Column ? "40/80 col" : "40 col";
  return [m.cpu, ram, columns].filter(Boolean).join(" · ");
}

/**
 * MachineMenu - the header badge that names the running machine, and the
 * dropdown that changes it.
 *
 * Switching is destructive: the core cannot convert a running machine into a
 * different one, so it rebuilds the emulator and everything in it goes. The
 * menu says so and asks first, rather than presenting the choice as a harmless
 * toggle alongside the view options.
 */
export class MachineMenu {
  constructor({ wasmModule, onMachineChanged }) {
    this.wasmModule = wasmModule;
    this.onMachineChanged = onMachineChanged;
    this.machines = [];
    this.switching = false;

    this.container = document.getElementById("machine-menu-container");
    this.trigger = document.getElementById("btn-machine-chip");
    this.nameEl = document.getElementById("machine-chip-name");
    this.menuEl = document.getElementById("machine-menu");
  }

  async init() {
    if (!this.menuEl) return;
    this.updateBadge();
    await this.refresh();
  }

  /**
   * Show which machine is running.
   *
   * The badge is the only thing on screen that names the machine, so it has to
   * be right or the header is lying about what the user is looking at.
   */
  updateBadge() {
    const machine = getMachineProfile();
    if (this.nameEl) {
      // The badge wears the machine's own mark, not a name written for prose.
      this.nameEl.textContent =
        machine.logotype || machine.shortName || machine.name;
    }
    if (this.trigger) {
      this.trigger.title = `${machine.name} — click to change machine`;
    }
    // The tab follows too, so a window switcher shows which machine a tab runs.
    document.title = `${machine.name} Emulator`;
  }

  /** Re-read the machine list and redraw the dropdown. */
  async refresh() {
    if (!this.menuEl || !this.wasmModule) return;
    this.machines = await listMachineProfiles(this.wasmModule);
    this.render();
  }

  render() {
    if (!this.menuEl) return;
    const currentKey = getMachineProfile().key;

    this.menuEl.innerHTML =
      `<div class="machine-menu-heading">Machine</div>` +
      this.machines.map((m) => this.itemHTML(m, currentKey)).join("");

    for (const item of this.menuEl.querySelectorAll(".machine-menu-item")) {
      if (item.disabled) continue;
      item.addEventListener("click", (e) => {
        e.stopPropagation();
        this.choose(item.dataset.key);
      });
    }
  }

  itemHTML(m, currentKey) {
    const isCurrent = m.key === currentKey;
    const unavailable = m.runnable === false;

    const classes = ["header-menu-item", "machine-menu-item"];
    if (isCurrent) classes.push("current");
    if (unavailable) classes.push("unavailable");

    // A tick for the machine in use; a word for one that cannot be started.
    const mark = isCurrent
      ? `<svg class="machine-menu-tick" viewBox="0 0 12 12" aria-hidden="true">
           <path d="M2 6.5l2.5 2.5L10 3.5" fill="none" stroke="currentColor"
                 stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>
         </svg>`
      : unavailable
        ? `<span class="machine-menu-flag">No ROM</span>`
        : "";

    return `
      <button class="${classes.join(" ")}" data-key="${m.key}" type="button"
              ${unavailable ? "disabled" : ""}
              ${unavailable ? 'title="Its ROM images are not built in"' : ""}>
        <span class="machine-menu-art">${MACHINE_ART[m.key] || ""}</span>
        <span class="machine-menu-text">
          <span class="machine-menu-name">${m.name}</span>
          <span class="machine-menu-spec">${specLine(m)}</span>
        </span>
        ${mark}
      </button>`;
  }

  async choose(key) {
    if (this.switching) return;
    const target = this.machines.find((m) => m.key === key);
    if (!target || target.key === getMachineProfile().key) {
      this.close();
      return;
    }

    this.close();

    const ok = await showConfirm(
      `Switch to the ${target.name}?\n\n` +
        "The machine is rebuilt from scratch. Inserted disks, hard drives and " +
        "anything in memory are lost.",
      "Switch",
    );
    if (!ok) return;

    this.switching = true;
    const profile = await switchMachine(this.wasmModule, key);
    this.switching = false;

    if (!profile) {
      console.warn(`The core refused to switch to ${target.name}.`);
      return;
    }

    this.updateBadge();
    if (this.onMachineChanged) await this.onMachineChanged(profile);
    await this.refresh();
  }

  close() {
    if (this.container) this.container.classList.remove("open");
  }
}
