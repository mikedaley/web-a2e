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
import {
  IIGS_MEMORY_SIZES,
  applyMemoryKB,
  formatMemorySize,
  readMemoryKB,
} from "./iigs-memory.js";

/** Bytes as the machine's own marketing would have said it. */
function formatK(bytes) {
  return bytes ? `${Math.round(bytes / 1024)}K` : "";
}

/** The one-line summary under a machine's name. */
function specLine(m, iigsMemoryKB) {
  // A IIgs's memory is a choice rather than a fact about the model, so it
  // reports what is actually fitted. The profile's figure is the Mega II's
  // 128K, which is the part of a IIgs that cannot change.
  const ram =
    m.family === "apple2gs" && iigsMemoryKB
      ? formatMemorySize(iigsMemoryKB)
      : m.memory?.auxRamSize
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
    this.iigsMemoryKB = null;

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
    this.iigsMemoryKB = await readMemoryKB(this.wasmModule);
    this.render();
  }

  render() {
    if (!this.menuEl) return;
    const currentKey = getMachineProfile().key;

    // In the order Apple sold them, which is the order anyone who knows them
    // expects. The registry is in id order, which is not that.
    const machines = [...this.machines].sort(
      (a, b) => (a.released || 0) - (b.released || 0),
    );
    this.menuEl.innerHTML =
      `<div class="machine-menu-heading">Machine</div>` +
      machines.map((m) => this.itemHTML(m, currentKey)).join("");

    for (const item of this.menuEl.querySelectorAll(".machine-menu-item")) {
      if (item.disabled) continue;
      item.addEventListener("click", (e) => {
        e.stopPropagation();
        this.choose(item.dataset.key);
      });
    }

    for (const chip of this.menuEl.querySelectorAll(".machine-menu-ram-chip")) {
      chip.addEventListener("click", (e) => {
        e.stopPropagation();
        this.chooseMemory(parseInt(chip.dataset.kb, 10));
      });
    }
  }

  /**
   * The row of memory sizes under the IIgs.
   *
   * It is offered only for the machine in use, because setting it builds that
   * machine — there would be nothing to apply it to otherwise, and a size
   * sitting under a machine you are not running reads like a promise the menu
   * does not keep.
   */
  memoryRowHTML(m, isCurrent) {
    if (m.family !== "apple2gs" || !isCurrent || !this.iigsMemoryKB) return "";

    const chips = IIGS_MEMORY_SIZES.map((size) => {
      const on = size.kb === this.iigsMemoryKB;
      return `<button class="machine-menu-ram-chip${on ? " on" : ""}"
                      type="button" data-kb="${size.kb}"
                      ${size.note ? `title="${size.note}"` : ""}
                      ${on ? 'aria-pressed="true"' : ""}>${size.label}</button>`;
    }).join("");

    return `
      <div class="machine-menu-ram">
        <span class="machine-menu-ram-label">Memory</span>
        <span class="machine-menu-ram-chips">${chips}</span>
      </div>`;
  }

  itemHTML(m, currentKey) {
    const isCurrent = m.key === currentKey;
    const unavailable = m.runnable === false;
    // A machine that runs but is not finished says so, because the difference
    // between "will not start" and "starts but cannot do the thing you wanted"
    // matters to somebody choosing one. The IIgs boots, reads both its drives
    // and makes a noise; what it has not got is a 3.5" drive, a Control Panel,
    // or enough of the machine for GS/OS.
    const partial = m.family === "apple2gs" && !unavailable;

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
        : partial
          ? `<span class="machine-menu-flag">In progress</span>`
          : "";

    return `
      <button class="${classes.join(" ")}" data-key="${m.key}" type="button"
              ${unavailable ? "disabled" : ""}
              ${
                unavailable
                  ? 'title="Its ROM images are not built in"'
                  : partial
                    ? 'title="Boots DOS 3.3 and ProDOS 8; no 3.5\" drive, Control Panel or GS/OS yet"'
                    : ""
              }>
        <span class="machine-menu-text">
          <span class="machine-menu-name">${m.name}</span>
          <span class="machine-menu-spec">${specLine(m, this.iigsMemoryKB)}</span>
        </span>
        ${mark}
      </button>${this.memoryRowHTML(m, isCurrent)}`;
  }

  /**
   * Fit a different amount of memory, which rebuilds the machine.
   *
   * Same bargain as switching machines, and said in the same words: RAM cannot
   * grow underneath a running program, so everything in memory goes.
   */
  async chooseMemory(kb) {
    if (this.switching || !Number.isFinite(kb)) return;
    if (kb === this.iigsMemoryKB) {
      this.close();
      return;
    }

    this.close();
    const ok = await showConfirm(
      `Fit ${formatMemorySize(kb)} of memory?\n\n` +
        "The machine is rebuilt from scratch. Inserted disks, hard drives and " +
        "anything in memory are lost.",
      "Fit",
    );
    if (!ok) return;

    this.switching = true;
    const settled = await applyMemoryKB(this.wasmModule, kb);
    this.switching = false;

    if (settled === null) {
      console.warn(`The core refused ${formatMemorySize(kb)} of memory.`);
      return;
    }

    if (this.onMachineChanged) {
      await this.onMachineChanged(getMachineProfile());
    }
    await this.refresh();
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
    await this.switchTo(key);
  }

  /**
   * Put a different machine in the core, no questions asked.
   *
   * `choose` asks first because the menu is where a person switches by hand;
   * a save state that was made on another machine has already answered — it
   * is asking for that machine back — and goes through here.
   *
   * @returns {Promise<boolean>} whether the core now runs that machine
   */
  async switchTo(key) {
    if (this.switching) return false;
    const target = this.machines.find((m) => m.key === key);
    if (!target) return false;
    if (target.key === getMachineProfile().key) return true;

    this.switching = true;
    const profile = await switchMachine(this.wasmModule, key);
    this.switching = false;

    if (!profile) {
      console.warn(`The core refused to switch to ${target.name}.`);
      return false;
    }

    this.updateBadge();
    if (this.onMachineChanged) await this.onMachineChanged(profile);
    await this.refresh();
    return true;
  }

  close() {
    if (this.container) this.container.classList.remove("open");
  }
}
