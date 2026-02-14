/*
 * slot-configuration-window.js - Expansion slot configuration window
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import { showToast } from "./toast.js";

/**
 * SlotConfigurationWindow - Configure Apple IIe expansion slots
 * Visual drag-and-drop card tray and motherboard slot layout
 */
export class SlotConfigurationWindow extends BaseWindow {
  constructor(wasmModule, onResetCallback) {
    const maxHeight = 650;
    super({
      id: "slot-configuration",
      title: "Expansion Slots",
      minWidth: 450,
      minHeight: maxHeight,
      defaultWidth: 450,
      defaultHeight: maxHeight,
      resizeDirections: [],
    });

    this.wasmModule = wasmModule;
    this.onResetCallback = onResetCallback;

    // Available card types with accent colors
    this.cards = [
      { id: "disk2", name: "Disk II", color: "green" },
      { id: "mockingboard", name: "Mockingboard", color: "purple" },
      { id: "thunderclock", name: "Thunderclock", color: "orange" },
      { id: "mouse", name: "Mouse Card", color: "blue" },
      { id: "smartport", name: "SmartPort", color: "red" },
      { id: "softcard", name: "Z-80 SoftCard", color: "cyan" },
      { id: "ssc", name: "Super Serial Card", color: "yellow" },
    ];

    // Card icon SVGs (simple representations)
    this.cardIcons = {
      disk2: `<svg viewBox="0 0 24 24" width="20" height="20"><rect x="3" y="4" width="18" height="16" rx="2" fill="none" stroke="currentColor" stroke-width="1.5"/><circle cx="12" cy="12" r="4" fill="none" stroke="currentColor" stroke-width="1.2"/><circle cx="12" cy="12" r="1" fill="currentColor"/></svg>`,
      mockingboard: `<svg viewBox="0 0 24 24" width="20" height="20"><rect x="3" y="6" width="18" height="12" rx="1" fill="none" stroke="currentColor" stroke-width="1.5"/><circle cx="8" cy="12" r="2" fill="none" stroke="currentColor" stroke-width="1"/><circle cx="16" cy="12" r="2" fill="none" stroke="currentColor" stroke-width="1"/></svg>`,
      thunderclock: `<svg viewBox="0 0 24 24" width="20" height="20"><circle cx="12" cy="12" r="8" fill="none" stroke="currentColor" stroke-width="1.5"/><line x1="12" y1="12" x2="12" y2="7" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/><line x1="12" y1="12" x2="16" y2="12" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg>`,
      mouse: `<svg viewBox="0 0 24 24" width="20" height="20"><rect x="6" y="3" width="12" height="18" rx="6" fill="none" stroke="currentColor" stroke-width="1.5"/><line x1="12" y1="3" x2="12" y2="10" stroke="currentColor" stroke-width="1"/></svg>`,
      smartport: `<svg viewBox="0 0 24 24" width="20" height="20"><rect x="4" y="3" width="16" height="18" rx="2" fill="none" stroke="currentColor" stroke-width="1.5"/><rect x="7" y="6" width="10" height="3" rx="0.5" fill="none" stroke="currentColor" stroke-width="0.8"/><rect x="7" y="11" width="10" height="3" rx="0.5" fill="none" stroke="currentColor" stroke-width="0.8"/><circle cx="12" cy="18" r="1" fill="currentColor"/></svg>`,
      softcard: `<svg viewBox="0 0 24 24" width="20" height="20"><rect x="3" y="5" width="18" height="14" rx="1" fill="none" stroke="currentColor" stroke-width="1.5"/><text x="12" y="14" text-anchor="middle" font-size="7" font-weight="bold" fill="currentColor">Z80</text><line x1="6" y1="5" x2="6" y2="3" stroke="currentColor" stroke-width="1"/><line x1="9" y1="5" x2="9" y2="3" stroke="currentColor" stroke-width="1"/><line x1="12" y1="5" x2="12" y2="3" stroke="currentColor" stroke-width="1"/><line x1="15" y1="5" x2="15" y2="3" stroke="currentColor" stroke-width="1"/><line x1="18" y1="5" x2="18" y2="3" stroke="currentColor" stroke-width="1"/><line x1="6" y1="19" x2="6" y2="21" stroke="currentColor" stroke-width="1"/><line x1="9" y1="19" x2="9" y2="21" stroke="currentColor" stroke-width="1"/><line x1="12" y1="19" x2="12" y2="21" stroke="currentColor" stroke-width="1"/><line x1="15" y1="19" x2="15" y2="21" stroke="currentColor" stroke-width="1"/><line x1="18" y1="19" x2="18" y2="21" stroke="currentColor" stroke-width="1"/></svg>`,
      ssc: `<svg viewBox="0 0 24 24" width="20" height="20"><rect x="4" y="6" width="16" height="12" rx="1" fill="none" stroke="currentColor" stroke-width="1.5"/><text x="12" y="14" text-anchor="middle" font-size="5" font-weight="bold" fill="currentColor">RS232</text><circle cx="7" cy="20" r="1" fill="currentColor"/><circle cx="12" cy="20" r="1" fill="currentColor"/><circle cx="17" cy="20" r="1" fill="currentColor"/><circle cx="9.5" cy="22" r="1" fill="currentColor"/><circle cx="14.5" cy="22" r="1" fill="currentColor"/></svg>`,
    };

    // Slot metadata
    this.slots = [
      { slot: 1, label: "Slot 1", available: ["ssc", "softcard"], note: "Printer / Serial" },
      {
        slot: 2,
        label: "Slot 2",
        available: ["ssc", "smartport", "softcard"],
        note: "Serial / Modem",
      },
      {
        slot: 3,
        label: "Slot 3",
        available: [],
        note: "80-Column (Built-in)",
        fixed: true,
      },
      {
        slot: 4,
        label: "Slot 4",
        available: ["mockingboard", "mouse", "smartport", "softcard"],
        note: "Sound / Mouse",
      },
      {
        slot: 5,
        label: "Slot 5",
        available: ["thunderclock", "smartport", "softcard"],
        note: "Clock / Drive",
      },
      { slot: 6, label: "Slot 6", available: ["disk2"], note: "Disk drives" },
      {
        slot: 7,
        label: "Slot 7",
        available: ["thunderclock", "smartport", "softcard"],
        note: "RAM / Clock",
      },
    ];

    // Current slot assignments (working state for drag-and-drop)
    this.slotAssignments = {};

    // Track pending changes vs the actual WASM state
    this.pendingChanges = {};
    this.hasChanges = false;

    // Drag state
    this.dragState = null;
    this.ghostElement = null;

    // Bind drag handlers
    this.handleDragMove = this.handleDragMove.bind(this);
    this.handleDragEnd = this.handleDragEnd.bind(this);
  }

  renderContent() {
    return `
      <div class="slot-config-content">
        <div class="slot-section">
          <div class="slot-section-title">Available Cards</div>
          <div class="card-tray" id="card-tray"></div>
        </div>
        <div class="slot-section">
          <div class="slot-section-title">Motherboard Slots</div>
          <div class="motherboard-slots" id="motherboard-slots"></div>
        </div>
        <div class="slot-section">
          <div class="slot-section-title">Other Hardware</div>
          <div class="nsc-toggle-row">
            <label class="nsc-toggle-label">
              <input type="checkbox" id="nsc-toggle" class="nsc-checkbox">
              <span>No-Slot Clock (DS1215)</span>
            </label>
            <span class="nsc-note">ProDOS real-time clock at $C300</span>
          </div>
        </div>
        <div class="slot-footer">
          <button id="slot-apply-btn" class="slot-apply-btn" disabled>Apply &amp; Reset</button>
        </div>
      </div>`;
  }

  setupContentEventListeners() {
    // Apply button
    const applyBtn = this.contentElement.querySelector("#slot-apply-btn");
    if (applyBtn) {
      applyBtn.addEventListener("click", () => this.applyChanges());
    }

    // No-Slot Clock toggle
    const nscToggle = this.contentElement.querySelector("#nsc-toggle");
    if (nscToggle) {
      // Load saved state
      const saved = localStorage.getItem("a2e-nsc-enabled");
      const enabled = saved === "true";
      nscToggle.checked = enabled;
      this.applyNoSlotClock(enabled);

      nscToggle.addEventListener("change", () => {
        const enable = nscToggle.checked;
        localStorage.setItem("a2e-nsc-enabled", enable ? "true" : "false");
        this.applyNoSlotClock(enable);
        showToast(
          enable ? "No-Slot Clock enabled" : "No-Slot Clock disabled",
          "info",
        );
      });
    }
  }

  applyNoSlotClock(enable) {
    if (this.wasmModule && this.wasmModule._enableNoSlotClock) {
      this.wasmModule._enableNoSlotClock(enable);
    }
  }

  create() {
    super.create();
    this.applyInitialSettings();
    this.initSlotAssignments();
    this.setupContentEventListeners();
    this.updateView();
  }

  /**
   * Initialize working slot assignments from current WASM state or saved settings
   */
  initSlotAssignments() {
    this.slotAssignments = {};
    for (const slotInfo of this.slots) {
      if (slotInfo.fixed) continue;
      const card = this.getCurrentSlotCard(slotInfo.slot);
      if (card && card !== "empty") {
        this.slotAssignments[slotInfo.slot] = card;
      }
    }
    // Store the original state for change detection
    this.originalAssignments = { ...this.slotAssignments };
  }

  /**
   * Get the list of cards not currently installed in any slot
   */
  getAvailableCards() {
    const installed = new Set(Object.values(this.slotAssignments));
    return this.cards.filter((c) => !installed.has(c.id));
  }

  /**
   * Re-render the card tray and motherboard slots
   */
  updateView() {
    this.renderCardTray();
    this.renderMotherboardSlots();
    this.detectChanges();
    this.updateUI();
  }

  renderCardTray() {
    const tray = this.contentElement.querySelector("#card-tray");
    if (!tray) return;

    const available = this.getAvailableCards();
    if (available.length === 0) {
      tray.innerHTML = '<div class="card-tray-empty">All cards installed</div>';
      return;
    }

    tray.innerHTML = available
      .map(
        (card) => `
        <div class="card-tile card-color-${card.color}" data-card-id="${card.id}" data-source="tray">
          <div class="card-tile-icon">${this.cardIcons[card.id]}</div>
          <div class="card-tile-name">${card.name}</div>
        </div>`,
      )
      .join("");

    // Attach drag listeners to tray cards
    tray.querySelectorAll(".card-tile").forEach((tile) => {
      tile.addEventListener("mousedown", (e) => {
        e.preventDefault();
        this.startCardDrag(tile.dataset.cardId, "tray", null, e);
      });
    });
  }

  renderMotherboardSlots() {
    const container = this.contentElement.querySelector("#motherboard-slots");
    if (!container) return;

    container.innerHTML = this.slots
      .map((slotInfo) => {
        const cardId = this.slotAssignments[slotInfo.slot];
        const card = cardId ? this.cards.find((c) => c.id === cardId) : null;
        const isFixed = slotInfo.fixed;

        if (isFixed) {
          return `
            <div class="mb-slot-row mb-slot-fixed">
              <div class="mb-slot-badge">S${slotInfo.slot}</div>
              <div class="mb-slot-connector">
                <div class="mb-connector-teeth"></div>
                <div class="mb-slot-card-fixed">
                  <span class="mb-lock-icon">&#128274;</span>
                  <span>80-Column</span>
                </div>
              </div>
              <div class="mb-slot-note">${slotInfo.note}</div>
            </div>`;
        }

        if (card) {
          return `
            <div class="mb-slot-row" data-slot="${slotInfo.slot}">
              <div class="mb-slot-badge">S${slotInfo.slot}</div>
              <div class="mb-slot-connector">
                <div class="mb-connector-teeth"></div>
                <div class="mb-slot-card card-color-${card.color}" data-card-id="${card.id}" data-source="slot" data-slot="${slotInfo.slot}">
                  <div class="card-tile-icon">${this.cardIcons[card.id]}</div>
                  <div class="card-tile-name">${card.name}</div>
                </div>
              </div>
              <div class="mb-slot-note">${slotInfo.note}</div>
            </div>`;
        }

        return `
          <div class="mb-slot-row mb-slot-empty" data-slot="${slotInfo.slot}">
            <div class="mb-slot-badge">S${slotInfo.slot}</div>
            <div class="mb-slot-connector">
              <div class="mb-connector-teeth"></div>
              <div class="mb-slot-dropzone">Empty</div>
            </div>
            <div class="mb-slot-note">${slotInfo.note}</div>
          </div>`;
      })
      .join("");

    // Attach drag listeners to installed cards
    container.querySelectorAll(".mb-slot-card").forEach((tile) => {
      tile.addEventListener("mousedown", (e) => {
        e.preventDefault();
        const slot = parseInt(tile.dataset.slot, 10);
        this.startCardDrag(tile.dataset.cardId, "slot", slot, e);
      });
    });
  }

  // ---- Drag & Drop ----

  startCardDrag(cardId, sourceType, sourceSlot, e) {
    const card = this.cards.find((c) => c.id === cardId);
    if (!card) return;

    this.dragState = { cardId, sourceType, sourceSlot };

    // Create ghost element
    this.ghostElement = document.createElement("div");
    this.ghostElement.className = `card-tile card-color-${card.color} card-ghost`;
    this.ghostElement.innerHTML = `
      <div class="card-tile-icon">${this.cardIcons[card.id]}</div>
      <div class="card-tile-name">${card.name}</div>`;
    document.body.appendChild(this.ghostElement);
    this.positionGhost(e);

    // Dim the source element
    const sourceEl =
      sourceType === "tray"
        ? this.contentElement.querySelector(
            `.card-tray .card-tile[data-card-id="${cardId}"]`,
          )
        : this.contentElement.querySelector(
            `.mb-slot-card[data-card-id="${cardId}"][data-slot="${sourceSlot}"]`,
          );
    if (sourceEl) sourceEl.classList.add("card-dragging");

    // Highlight compatible slots
    this.highlightSlots(cardId);

    document.addEventListener("mousemove", this.handleDragMove);
    document.addEventListener("mouseup", this.handleDragEnd);
  }

  handleDragMove(e) {
    if (!this.dragState) return;
    e.preventDefault();
    this.positionGhost(e);
  }

  positionGhost(e) {
    if (!this.ghostElement) return;
    this.ghostElement.style.left = e.clientX - 40 + "px";
    this.ghostElement.style.top = e.clientY - 20 + "px";
  }

  handleDragEnd(e) {
    e.preventDefault();
    document.removeEventListener("mousemove", this.handleDragMove);
    document.removeEventListener("mouseup", this.handleDragEnd);

    if (!this.dragState) return;

    const { cardId, sourceType, sourceSlot } = this.dragState;
    const target = this.getDropTarget(e);

    if (target) {
      if (target.type === "tray") {
        // Dragged to tray — remove from slot
        if (sourceType === "slot" && sourceSlot != null) {
          delete this.slotAssignments[sourceSlot];
        }
      } else if (target.type === "slot") {
        const targetSlot = target.slot;
        if (this.isCompatible(cardId, targetSlot)) {
          // Remove card from source slot if it came from a slot
          if (sourceType === "slot" && sourceSlot != null) {
            delete this.slotAssignments[sourceSlot];
          }
          // If target slot already has a card, return it to tray
          if (this.slotAssignments[targetSlot]) {
            delete this.slotAssignments[targetSlot];
          }
          this.slotAssignments[targetSlot] = cardId;
        } else {
          // Incompatible — flash red
          this.flashIncompatible(targetSlot);
        }
      }
    }

    this.cleanupDrag();
    this.updateView();
  }

  getDropTarget(e) {
    // Remove ghost temporarily so elementFromPoint sees through it
    if (this.ghostElement) this.ghostElement.style.pointerEvents = "none";
    const el = document.elementFromPoint(e.clientX, e.clientY);
    if (this.ghostElement) this.ghostElement.style.pointerEvents = "";

    if (!el) return null;

    // Check if dropped on card tray
    const tray = el.closest("#card-tray") || el.closest(".card-tray");
    if (tray) return { type: "tray" };

    // Check if dropped on a slot row
    const slotRow = el.closest(".mb-slot-row[data-slot]");
    if (slotRow) {
      return { type: "slot", slot: parseInt(slotRow.dataset.slot, 10) };
    }

    return null;
  }

  highlightSlots(cardId) {
    const rows = this.contentElement.querySelectorAll(
      ".mb-slot-row[data-slot]",
    );
    rows.forEach((row) => {
      const slot = parseInt(row.dataset.slot, 10);
      if (this.isCompatible(cardId, slot)) {
        row.classList.add("mb-slot-compat");
      } else {
        row.classList.add("mb-slot-incompat");
      }
    });
  }

  flashIncompatible(slot) {
    const row = this.contentElement.querySelector(
      `.mb-slot-row[data-slot="${slot}"]`,
    );
    if (!row) return;
    row.classList.add("mb-slot-reject");
    setTimeout(() => row.classList.remove("mb-slot-reject"), 400);
  }

  cleanupDrag() {
    // Remove ghost
    if (this.ghostElement) {
      this.ghostElement.remove();
      this.ghostElement = null;
    }
    // Remove drag styling
    this.contentElement
      .querySelectorAll(".card-dragging")
      .forEach((el) => el.classList.remove("card-dragging"));
    this.contentElement
      .querySelectorAll(".mb-slot-compat, .mb-slot-incompat")
      .forEach((el) => {
        el.classList.remove("mb-slot-compat", "mb-slot-incompat");
      });
    this.dragState = null;
  }

  isCompatible(cardId, slot) {
    const slotInfo = this.slots.find((s) => s.slot === slot);
    if (!slotInfo || slotInfo.fixed) return false;
    return slotInfo.available.includes(cardId);
  }

  // ---- Change detection ----

  detectChanges() {
    this.pendingChanges = {};
    this.hasChanges = false;

    for (const slotInfo of this.slots) {
      if (slotInfo.fixed) continue;
      const slotNum = slotInfo.slot;
      const current = this.slotAssignments[slotNum] || "empty";
      const original = this.originalAssignments[slotNum] || "empty";
      if (current !== original) {
        this.pendingChanges[slotNum] = current;
        this.hasChanges = true;
      }
    }
  }

  // ---- Existing logic (preserved) ----

  getCurrentSlotCard(slot) {
    if (this.pendingChanges[slot]) {
      return this.pendingChanges[slot];
    }

    if (this.wasmModule && this.wasmModule._getSlotCard) {
      const ptr = this.wasmModule._getSlotCard(slot);
      if (ptr) {
        return this.wasmModule.UTF8ToString(ptr);
      }
    }

    const defaults = {
      4: "mockingboard",
      5: "smartport",
      6: "disk2",
      7: "thunderclock",
    };
    return defaults[slot] || "empty";
  }

  updateUI() {
    const applyBtn = this.contentElement.querySelector("#slot-apply-btn");
    if (!applyBtn) return;

    if (this.hasChanges) {
      applyBtn.disabled = false;
      applyBtn.textContent = "Apply & Reset";
      applyBtn.classList.add("has-changes");
    } else {
      applyBtn.disabled = true;
      applyBtn.textContent = "No Changes";
      applyBtn.classList.remove("has-changes");
    }
  }

  applyChanges() {
    this.saveSettings();

    if (this.wasmModule && this.wasmModule._setSlotCard) {
      // Apply ALL slot assignments, not just changes, to ensure empty slots are set too
      for (const slotInfo of this.slots) {
        if (slotInfo.fixed) continue;
        const slotNum = slotInfo.slot;
        const cardId = this.slotAssignments[slotNum] || "empty";
        const cardIdPtr = this.wasmModule._malloc(cardId.length + 1);
        this.wasmModule.stringToUTF8(cardId, cardIdPtr, cardId.length + 1);
        this.wasmModule._setSlotCard(slotNum, cardIdPtr);
        this.wasmModule._free(cardIdPtr);
      }
    }

    // Update original state to reflect new baseline
    this.originalAssignments = { ...this.slotAssignments };
    this.pendingChanges = {};
    this.hasChanges = false;
    this.updateUI();

    if (this.onResetCallback) {
      this.onResetCallback();
    } else if (this.wasmModule && this.wasmModule._reset) {
      this.wasmModule._reset();
    }

    showToast("Expansion slot configuration updated", "info");
  }

  applyInitialSettings() {
    const saved = this.loadSettingsFromStorage();
    const config = saved || { 5: "smartport", 7: "thunderclock" };
    if (this.wasmModule && this.wasmModule._setSlotCard) {
      for (const [slot, cardId] of Object.entries(config)) {
        const slotNum = parseInt(slot, 10);
        const cardIdPtr = this.wasmModule._malloc(cardId.length + 1);
        this.wasmModule.stringToUTF8(cardId, cardIdPtr, cardId.length + 1);
        this.wasmModule._setSlotCard(slotNum, cardIdPtr);
        this.wasmModule._free(cardIdPtr);
      }
    }

    // Apply No-Slot Clock setting
    const nscEnabled = localStorage.getItem("a2e-nsc-enabled") === "true";
    this.applyNoSlotClock(nscEnabled);
  }

  saveSettings() {
    try {
      const config = {};
      for (const slotInfo of this.slots) {
        if (slotInfo.fixed) continue;
        const cardId = this.slotAssignments[slotInfo.slot] || "empty";
        config[slotInfo.slot] = cardId;
      }
      localStorage.setItem("a2e-slot-config", JSON.stringify(config));
    } catch (e) {
      console.warn("Could not save slot configuration:", e);
    }
  }

  loadSettings() {
    try {
      const saved = this.loadSettingsFromStorage();
      if (saved) {
        // Populate slot assignments from saved config
        for (const [slot, cardId] of Object.entries(saved)) {
          if (cardId && cardId !== "empty") {
            this.slotAssignments[parseInt(slot, 10)] = cardId;
          }
        }
      }
    } catch (e) {
      console.warn("Could not load slot configuration:", e);
    }
  }

  loadSettingsFromStorage() {
    try {
      const saved = localStorage.getItem("a2e-slot-config");
      if (saved) {
        return JSON.parse(saved);
      }
    } catch (e) {
      console.warn("Could not parse slot configuration:", e);
    }
    return null;
  }

  update() {
    // No dynamic updates needed
  }
}
