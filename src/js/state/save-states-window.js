/*
 * save-states-window.js - Save states window UI
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/**
 * SaveStatesWindow - Window for managing save state slots
 * Auto-save row at top (load/download only), plus 5 numbered manual slots
 * with save, load, clear, download. Load-from-file at bottom.
 */

import { BaseWindow } from "../windows/base-window.js";
import {
  getAutosaveInfo,
  loadStateFromStorage,
  getAllSlotInfo,
  clearSlot,
  loadStateFromSlot,
} from "./state-persistence.js";
import { parseStateHeader } from "./state-header.js";
import { getMachineProfile } from "../machine/machine-profile.js";
import { showConfirm } from "../ui/confirm.js";

const SLOT_COUNT = 5;

export class SaveStatesWindow extends BaseWindow {
  constructor(stateManager, uiController) {
    super({
      id: "save-states",
      title: "Save States",
      defaultWidth: 480,
      defaultHeight: 560,
      minWidth: 400,
      minHeight: 380,
    });
    this.stateManager = stateManager;
    this.uiController = uiController;
    this.autosaveElement = null;
    this.slotElements = [];
    this.hoverPreview = null;
  }

  renderContent() {
    // Autosave row (no Save or Clear buttons)
    const autosaveHtml = `
      <div class="save-slot save-slot-auto" data-slot="auto">
        <div class="slot-number auto">A</div>
        <div class="slot-thumbnail">
          <span class="slot-empty-icon">--</span>
        </div>
        <div class="slot-info">
          <div class="slot-status empty">No autosave</div>
          <div class="slot-timestamp"></div>
        </div>
        <div class="slot-actions">
          <button class="slot-btn load-btn" data-action="load-auto" disabled>Load</button>
          <button class="slot-btn download-btn" data-action="download-auto" disabled>DL</button>
        </div>
      </div>`;

    let slotsHtml = "";
    for (let i = 1; i <= SLOT_COUNT; i++) {
      slotsHtml += `
        <div class="save-slot" data-slot="${i}">
          <div class="slot-number">${i}</div>
          <div class="slot-thumbnail">
            <span class="slot-empty-icon">--</span>
          </div>
          <div class="slot-info">
            <div class="slot-status empty">Empty</div>
            <div class="slot-timestamp"></div>
          </div>
          <div class="slot-actions">
            <button class="slot-btn save-btn" data-action="save" data-slot="${i}">Save</button>
            <button class="slot-btn load-btn" data-action="load" data-slot="${i}" disabled>Load</button>
            <button class="slot-btn clear-btn" data-action="clear" data-slot="${i}" disabled>Clear</button>
            <button class="slot-btn download-btn" data-action="download" data-slot="${i}" disabled>DL</button>
          </div>
        </div>`;
    }

    return `
      <div class="save-states-container">
        ${autosaveHtml}
        <div class="save-states-divider"></div>
        ${slotsHtml}
        <div class="save-states-toolbar">
          <input type="file" accept=".a2state" style="display:none" />
          <button class="slot-btn load-file-btn">Load from File...</button>
        </div>
      </div>`;
  }

  onContentRendered() {
    // Cache autosave row
    this.autosaveElement = this.contentElement.querySelector(".save-slot-auto");

    // Cache slot row elements
    for (let i = 1; i <= SLOT_COUNT; i++) {
      const row = this.contentElement.querySelector(
        `.save-slot[data-slot="${i}"]`,
      );
      this.slotElements.push(row);
    }

    // Button click delegation
    this.contentElement.addEventListener("click", (e) => {
      const btn = e.target.closest(".slot-btn");
      if (!btn || btn.disabled) return;

      const action = btn.dataset.action;
      const slot = parseInt(btn.dataset.slot, 10);

      if (action === "save") this.handleSave(slot);
      else if (action === "load") this.handleLoad(slot);
      else if (action === "clear") this.handleClear(slot);
      else if (action === "download") this.handleDownload(slot);
      else if (action === "load-auto") this.handleLoadAutosave();
      else if (action === "download-auto") this.handleDownloadAutosave();
    });

    // Hover preview tooltip
    this.hoverPreview = document.createElement("div");
    this.hoverPreview.className = "slot-thumbnail-preview";
    document.body.appendChild(this.hoverPreview);

    this.contentElement.addEventListener(
      "mouseenter",
      (e) => {
        const thumb = e.target.closest(".slot-thumbnail");
        if (!thumb) return;
        const previewSrc = thumb.dataset.preview;
        if (!previewSrc) return;
        this.hoverPreview.innerHTML = `<img src="${previewSrc}" />`;
        this.hoverPreview.classList.add("visible");
      },
      true,
    );

    this.contentElement.addEventListener(
      "mouseleave",
      (e) => {
        const thumb = e.target.closest(".slot-thumbnail");
        if (!thumb) return;
        this.hoverPreview.classList.remove("visible");
      },
      true,
    );

    this.contentElement.addEventListener("mousemove", (e) => {
      if (!this.hoverPreview.classList.contains("visible")) return;
      const previewW = 280;
      const previewH = 192;
      const pad = 12;
      let x = e.clientX + pad;
      let y = e.clientY + pad;
      if (x + previewW > window.innerWidth) x = e.clientX - previewW - pad;
      if (y + previewH > window.innerHeight) y = e.clientY - previewH - pad;
      this.hoverPreview.style.left = `${x}px`;
      this.hoverPreview.style.top = `${y}px`;
    });

    // Load from file
    const fileInput = this.contentElement.querySelector('input[type="file"]');
    const loadFileBtn = this.contentElement.querySelector(".load-file-btn");

    if (loadFileBtn && fileInput) {
      loadFileBtn.addEventListener("click", () => fileInput.click());
      fileInput.addEventListener("change", (e) => {
        const file = e.target.files[0];
        if (file) this.handleLoadFromFile(file);
        fileInput.value = "";
      });
    }
  }

  show() {
    super.show();
    this.refreshSlots();
  }

  /** The machines change under the window; a machine change redraws it. */
  onMachineChanged() {
    if (this.isVisible) this.refreshSlots();
  }

  /**
   * A slot's machine, for the row.
   *
   * A slot saved off a different machine is still loadable — loading it puts
   * that machine in the core — but the row says which, so nobody is surprised
   * to find the //e gone and a IIgs in its place.
   */
  machineLabel(key) {
    const current = getMachineProfile();
    if (!key || key === current.key) return "";
    const known = this.stateManager.machines?.find((m) => m.key === key);
    return known ? known.shortName || known.name : key;
  }

  describeSlot(statusEl, status, machineKey) {
    const other = this.machineLabel(machineKey);
    statusEl.textContent = other ? `${status} · ${other}` : status;
    statusEl.title = other
      ? `Saved on the ${other}; loading it switches machine`
      : "";
  }

  async refreshSlots() {
    // Refresh autosave row
    await this.refreshAutosaveRow();

    // Refresh manual slots
    const slots = await getAllSlotInfo();

    for (let i = 0; i < SLOT_COUNT; i++) {
      const row = this.slotElements[i];
      if (!row) continue;

      const info = slots[i];
      const thumbEl = row.querySelector(".slot-thumbnail");
      const statusEl = row.querySelector(".slot-status");
      const timestampEl = row.querySelector(".slot-timestamp");
      const loadBtn = row.querySelector('[data-action="load"]');
      const clearBtn = row.querySelector('[data-action="clear"]');
      const downloadBtn = row.querySelector('[data-action="download"]');

      if (info) {
        if (info.thumbnail) {
          thumbEl.innerHTML = `<img src="${info.thumbnail}" alt="Slot ${i + 1}" />`;
        } else {
          thumbEl.innerHTML = '<span class="slot-empty-icon">--</span>';
        }
        thumbEl.dataset.preview = info.preview || info.thumbnail || "";
        this.describeSlot(statusEl, "Saved", info.machine);
        statusEl.classList.remove("empty");
        timestampEl.textContent = this.formatTimestamp(info.savedAt);
        loadBtn.disabled = false;
        clearBtn.disabled = false;
        downloadBtn.disabled = false;
      } else {
        thumbEl.innerHTML = '<span class="slot-empty-icon">--</span>';
        delete thumbEl.dataset.preview;
        statusEl.textContent = "Empty";
        statusEl.title = "";
        statusEl.classList.add("empty");
        timestampEl.textContent = "";
        loadBtn.disabled = true;
        clearBtn.disabled = true;
        downloadBtn.disabled = true;
      }
    }
  }

  async refreshAutosaveRow() {
    const row = this.autosaveElement;
    if (!row) return;

    // The autosave row is this machine's: each machine keeps its own.
    const info = await getAutosaveInfo(this.stateManager.currentMachineKey());
    const thumbEl = row.querySelector(".slot-thumbnail");
    const statusEl = row.querySelector(".slot-status");
    const timestampEl = row.querySelector(".slot-timestamp");
    const loadBtn = row.querySelector('[data-action="load-auto"]');
    const downloadBtn = row.querySelector('[data-action="download-auto"]');

    if (info) {
      if (info.thumbnail) {
        thumbEl.innerHTML = `<img src="${info.thumbnail}" alt="Autosave" />`;
      } else {
        thumbEl.innerHTML = '<span class="slot-empty-icon">--</span>';
      }
      thumbEl.dataset.preview = info.preview || info.thumbnail || "";
      statusEl.textContent = "Autosave";
      statusEl.classList.remove("empty");
      timestampEl.textContent = this.formatTimestamp(info.savedAt);
      loadBtn.disabled = false;
      downloadBtn.disabled = false;
    } else {
      thumbEl.innerHTML = '<span class="slot-empty-icon">--</span>';
      delete thumbEl.dataset.preview;
      statusEl.textContent = "No autosave";
      statusEl.classList.add("empty");
      timestampEl.textContent = "";
      loadBtn.disabled = true;
      downloadBtn.disabled = true;
    }
  }

  formatTimestamp(ts) {
    const date = new Date(ts);
    const now = new Date();
    const diffMs = now - date;
    const diffMins = Math.floor(diffMs / 60000);
    const diffHours = Math.floor(diffMs / 3600000);

    if (diffMins < 1) return "just now";
    if (diffMins < 60) return `${diffMins}m ago`;
    if (diffHours < 24) return `${diffHours}h ago`;
    return date.toLocaleDateString();
  }

  async handleSave(slot) {
    if (!this.stateManager.emulator.isRunning()) {
      this.uiController.showNotification("Power on the emulator first");
      return;
    }
    const ok = await this.stateManager.saveToSlot(slot);
    if (ok) {
      this.uiController.showNotification(`Saved to slot ${slot}`);
    } else {
      this.uiController.showNotification("Save failed");
    }
    this.refreshSlots();
  }

  /**
   * Loading a state saved off another machine means switching to it, which
   * throws away the machine that is running. Say so first.
   */
  async confirmMachineFor(stateData) {
    const machine = await this.stateManager.machineForState(stateData);
    if (!machine || machine.key === getMachineProfile().key) return true;
    if (!machine.runnable) {
      this.uiController.showNotification(
        `This state needs the ${machine.name}, whose ROM is not built in`,
      );
      return false;
    }
    return showConfirm(
      `This state was saved on the ${machine.name}.\n\n` +
        `Switch to it? The ${getMachineProfile().name} is rebuilt from ` +
        "scratch and anything in it is lost.",
      "Switch and Load",
    );
  }

  async handleLoad(slot) {
    const slotData = await loadStateFromSlot(slot);
    if (!slotData) {
      this.uiController.showNotification("No data in slot");
      return;
    }
    if (!(await this.confirmMachineFor(slotData.data))) return;
    const ok = await this.stateManager.restoreFromSlot(slot);
    if (ok) {
      this.uiController.showNotification(`Loaded slot ${slot}`);
      this.uiController.refocusCanvas();
    } else {
      this.uiController.showNotification("Load failed");
    }
  }

  async handleClear(slot) {
    await clearSlot(slot);
    this.uiController.showNotification(`Cleared slot ${slot}`);
    this.refreshSlots();
  }

  async handleDownload(slot) {
    const slotData = await loadStateFromSlot(slot);
    if (!slotData) {
      this.uiController.showNotification("No data in slot");
      return;
    }

    this.downloadBlob(
      slotData.data,
      `${slotData.machine || "apple2e"}-slot-${slot}.a2state`,
    );
  }

  async handleLoadAutosave() {
    const ok = await this.stateManager.restoreState();
    if (ok) {
      this.uiController.showNotification("Loaded autosave");
      this.uiController.refocusCanvas();
    } else {
      this.uiController.showNotification("Load failed");
    }
  }

  async handleDownloadAutosave() {
    const machine = this.stateManager.currentMachineKey();
    const data = await loadStateFromStorage(machine);
    if (!data) {
      this.uiController.showNotification("No autosave data");
      return;
    }

    this.downloadBlob(data, `${machine}-autosave.a2state`);
  }

  async downloadBlob(data, filename) {
    const blob = new Blob([data], { type: "application/octet-stream" });

    // Try File System Access API, fall back to blob download
    if (window.showSaveFilePicker) {
      try {
        const handle = await window.showSaveFilePicker({
          suggestedName: filename,
          types: [
            {
              description: "Apple II State",
              accept: { "application/octet-stream": [".a2state"] },
            },
          ],
        });
        const writable = await handle.createWritable();
        await writable.write(blob);
        await writable.close();
        this.uiController.showNotification(`Downloaded ${filename}`);
        return;
      } catch (err) {
        if (err.name === "AbortError") return;
        // Fall through to blob download
      }
    }

    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    this.uiController.showNotification(`Downloaded ${filename}`);
  }

  handleLoadFromFile(file) {
    const reader = new FileReader();
    reader.onload = async () => {
      const data = new Uint8Array(reader.result);

      if (!parseStateHeader(data)) {
        this.uiController.showNotification("Invalid state file");
        return;
      }
      if (!(await this.confirmMachineFor(data))) return;

      const ok = await this.stateManager.restoreFromFileData(data);
      if (ok) {
        this.uiController.showNotification("State loaded from file");
        this.uiController.refocusCanvas();
      } else {
        this.uiController.showNotification("Failed to load state file");
      }
    };
    reader.onerror = () => {
      this.uiController.showNotification("Failed to read file");
    };
    reader.readAsArrayBuffer(file);
  }

  destroy() {
    if (this.hoverPreview && this.hoverPreview.parentNode) {
      this.hoverPreview.parentNode.removeChild(this.hoverPreview);
    }
    super.destroy();
  }
}
