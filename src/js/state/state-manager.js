/*
 * state-manager.js - Emulator state serialization and management
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/**
 * StateManager - Manages emulator state save/restore and UI
 * Handles auto-save, manual save/restore, slot saves, and state popup UI
 */

import {
  saveStateToStorage,
  loadStateFromStorage,
  hasSavedState,
  saveStateToSlot,
  loadStateFromSlot,
} from "./state-persistence.js";
import {
  getMachineProfile,
  listMachineProfiles,
  machineDisplay,
} from "../machine/machine-profile.js";
import { parseStateHeader } from "./state-header.js";

// Constants
const AUTO_SAVE_INTERVAL_MS = 5000;
const THUMBNAIL_WIDTH = 140;
const THUMBNAIL_HEIGHT = 96;
// The save-state preview is a copy of the machine's framebuffer at full size,
// so it follows the machine rather than fixing a //e picture.

/**
 * @typedef {Object} StateManagerDeps
 * @property {Object} emulator - The emulator instance
 * @property {Object} wasmModule - The WASM module
 * @property {Object} uiController - UI controller for notifications
 * @property {Object} diskManager - Disk manager for state sync
 * @property {Object} reminderController - Reminder controller
 * @property {Object} [cpuDebuggerWindow] - CPU debugger window (for resync after import)
 * @property {Object} [hardDriveManager] - Hard drive manager, told when a state put images back
 * @property {function(string): Promise<boolean>} [switchMachine] - Puts a different machine in
 *   the core, for a state that was saved off one
 */

export class StateManager {
  /**
   * @param {StateManagerDeps} deps - Dependencies
   */
  constructor(deps) {
    this.emulator = deps.emulator;
    this.wasmModule = deps.wasmModule;
    this.uiController = deps.uiController;
    this.diskManager = deps.diskManager;
    this.reminderController = deps.reminderController;
    this.cpuDebuggerWindow = deps.cpuDebuggerWindow || null;
    this.basicProgramWindow = deps.basicProgramWindow || null;
    this.hardDriveManager = deps.hardDriveManager || null;
    this.switchMachine = deps.switchMachine || null;
    this.machines = null;

    this.autoSaveEnabled = false;
    this.autoSaveInterval = null;

    /** @type {function|null} Called after each autosave completes */
    this.onAutosave = null;
  }

  /**
   * Initialize state management
   */
  init() {
    this.setupAutoSave();
    this.setupStatePopup();
  }

  /**
   * Turn autosave off for this session only, leaving the stored preference
   * alone so it comes back on the next visit.
   *
   * The toggle is unticked to match, otherwise the System menu would claim
   * autosave is on while nothing is being written.
   *
   * @param {string} reason - Logged so the silence is explainable
   */
  suspendAutoSave(reason) {
    if (!this.autoSaveEnabled) return;
    this.autoSaveEnabled = false;
    const toggle = document.getElementById("autosave-toggle");
    if (toggle) toggle.checked = false;
    console.log(`Autosave suspended for this session: ${reason}`);
  }

  /**
   * Set up auto-save functionality
   */
  setupAutoSave() {
    // Load saved auto-save setting (default to enabled)
    const savedAutosave = localStorage.getItem("a2e-autosave-state");
    this.autoSaveEnabled = savedAutosave === "true";

    // Save window states and emulator state when page is closed
    window.addEventListener("beforeunload", () => {
      if (this.autoSaveEnabled) {
        this.saveState();
      }
    });

    // Save state when page becomes hidden (tab switch, minimize, mobile)
    document.addEventListener("visibilitychange", () => {
      if (
        document.hidden &&
        this.emulator.isRunning() &&
        this.autoSaveEnabled
      ) {
        this.saveState();
      }
    });

    // Periodic auto-save while running, deferred to idle time to avoid
    // stalling the audio/render loop (which causes stutters in Safari/Brave)
    this.autoSavePending = false;
    this.autoSaveIdleHandle = null;
    this.autoSaveInterval = setInterval(() => {
      if (
        this.emulator.isRunning() &&
        !document.hidden &&
        this.autoSaveEnabled &&
        !this.autoSavePending
      ) {
        this.autoSavePending = true;
        const doSave = () => {
          this.autoSavePending = false;
          this.saveState();
        };
        if (typeof requestIdleCallback === "function") {
          this.autoSaveIdleHandle = requestIdleCallback(doSave, {
            timeout: AUTO_SAVE_INTERVAL_MS,
          });
        } else {
          // Safari <16.4 fallback — setTimeout(0) yields to the render loop
          this.autoSaveIdleHandle = setTimeout(doSave, 0);
        }
      }
    }, AUTO_SAVE_INTERVAL_MS);
  }

  /**
   * Set up state controls within the System menu
   */
  setupStatePopup() {
    const autosaveToggle = document.getElementById("autosave-toggle");

    // Initialize toggle state
    if (autosaveToggle) {
      autosaveToggle.checked = this.autoSaveEnabled;
    }

    // Auto-save toggle
    if (autosaveToggle) {
      autosaveToggle.addEventListener("change", () => {
        this.autoSaveEnabled = autosaveToggle.checked;
        localStorage.setItem("a2e-autosave-state", this.autoSaveEnabled);
      });
    }
  }

  /** The key of the machine in the core, which is the machine any state is for. */
  currentMachineKey() {
    return getMachineProfile().key;
  }

  /**
   * Which machine wrote a state, from its header alone.
   *
   * Every machine's state starts the same way — magic, version, machine id —
   * so this needs none of the layout that follows. Returns the profile, or
   * null if the bytes are not a state or name a machine this build has not
   * got.
   */
  async machineForState(stateData) {
    const header = parseStateHeader(stateData);
    if (!header) return null;
    if (!this.machines) {
      this.machines = await listMachineProfiles(this.wasmModule);
    }
    return this.machines.find((m) => m.id === header.machineId) || null;
  }

  /**
   * Capture current emulator state as a Uint8Array
   * @returns {Uint8Array|null}
   */
  async captureStateData() {
    if (!this.emulator.isRunning() || !this.wasmModule) {
      return null;
    }

    const sizePtr = await this.wasmModule._malloc(4);
    try {
      const statePtr = await this.wasmModule._exportState(sizePtr);

      if (statePtr && sizePtr) {
        const size = await this.wasmModule.heapDataViewU32(sizePtr);

        if (size > 0) {
          const stateData = await this.wasmModule.heapRead(statePtr, size);
          return stateData;
        }
      }
      return null;
    } finally {
      await this.wasmModule._free(sizePtr);
    }
  }

  /**
   * Import raw state data into the emulator (power cycles first)
   * @param {Uint8Array} stateData
   * @returns {boolean} True if state was imported successfully
   */
  async importStateData(stateData) {
    if (!this.wasmModule || !stateData) {
      return false;
    }

    // A state only restores into the machine that wrote it, and the core
    // refuses any other. Rather than fail, put that machine in the core first:
    // a save is a save of a whole machine, and loading one is asking for that
    // machine back. The switch rebuilds the core, which is what a restore
    // wants anyway.
    const machine = await this.machineForState(stateData);
    if (machine && machine.key !== this.currentMachineKey()) {
      if (!this.switchMachine || !machine.runnable) return false;
      if (!(await this.switchMachine(machine.key))) return false;
    }

    // No power cycle for a machine that is already on. importState() resets the
    // core itself before unpacking, so stopping and restarting here would only
    // rebuild the audio stack — and the AudioWorklet is the emulation clock. A
    // fresh AudioContext built this far from the click that asked for the
    // restore comes up suspended in some browsers, and AudioDriver.start() then
    // waits for the *next* click before it builds a worklet. Nothing requests
    // samples in the meantime, so the emulator sits there looking frozen.
    const poweredOnHere = !this.emulator.isRunning();
    if (poweredOnHere) {
      await this.emulator.start();
    } else {
      // start() would have done this. A paste still feeding keys would type
      // into the restored machine.
      this.emulator.inputHandler?.cancelPaste();
    }

    // Copy state data to WASM memory
    const statePtr = await this.wasmModule._malloc(stateData.length);
    await this.wasmModule.heapWrite(statePtr, stateData);

    // Import state
    const success = await this.wasmModule._importState(
      statePtr,
      stateData.length,
    );

    await this.wasmModule._free(statePtr);

    if (success) {
      if (this.reminderController) {
        this.reminderController.dismissPowerReminder();
        this.reminderController.showBasicReminder(false);
      }
      if (this.diskManager) {
        this.diskManager.syncWithEmulatorState();
      }
      // The state carried the hard drive images too.
      if (this.hardDriveManager) {
        this.hardDriveManager.syncWithEmulatorState();
      }
      // Re-push JS-side breakpoints/watchpoints/beam breakpoints to C++
      // since importState() calls reset() which clears them on the WASM side
      if (this.cpuDebuggerWindow) {
        this.cpuDebuggerWindow.bpManager.resyncToWasm();
        this.cpuDebuggerWindow.resyncBeamToWasm();
      }
      // Re-sync BASIC breakpoints
      if (this.basicProgramWindow) {
        this.basicProgramWindow.getBreakpointManager().resyncToWasm();
      }
      return true;
    } else {
      // Only undo the power-on we did ourselves. A machine that was already
      // running is left alone: importState() rejects a bad magic or version
      // before it resets anything, so there is nothing to recover from.
      if (poweredOnHere) {
        await this.emulator.stop();
      }
      return false;
    }
  }

  /**
   * Capture a thumbnail screenshot of the current emulator display
   * @returns {string|null} Data URL of the thumbnail, or null
   */
  captureScreenshot() {
    const canvas = document.getElementById("screen");
    if (!canvas) return null;

    try {
      const offscreen = document.createElement("canvas");
      offscreen.width = THUMBNAIL_WIDTH;
      offscreen.height = THUMBNAIL_HEIGHT;
      const ctx = offscreen.getContext("2d");
      ctx.drawImage(canvas, 0, 0, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
      return offscreen.toDataURL("image/jpeg", 0.85);
    } catch (error) {
      console.error("Failed to capture screenshot:", error);
      return null;
    }
  }

  /**
   * Capture a high-resolution preview of the current emulator display
   * @returns {string|null} Data URL of the preview, or null
   */
  capturePreview() {
    const canvas = document.getElementById("screen");
    if (!canvas) return null;

    try {
      const offscreen = document.createElement("canvas");
      const { width: previewWidth, height: previewHeight } = machineDisplay();
      offscreen.width = previewWidth;
      offscreen.height = previewHeight;
      const ctx = offscreen.getContext("2d");
      ctx.drawImage(canvas, 0, 0, previewWidth, previewHeight);
      return offscreen.toDataURL("image/jpeg", 0.85);
    } catch (error) {
      console.error("Failed to capture preview:", error);
      return null;
    }
  }

  /**
   * Save the current emulator state to IndexedDB (auto-save)
   * @returns {Promise<void>}
   */
  async saveState() {
    if (!this.emulator.isRunning() || !this.wasmModule) {
      return;
    }

    try {
      this.uiController.flashStateButton();
      const stateData = await this.captureStateData();
      if (stateData) {
        const thumbnail = this.captureScreenshot();
        const preview = this.capturePreview();
        await saveStateToStorage(
          stateData,
          thumbnail,
          preview,
          this.currentMachineKey(),
        );
        if (this.onAutosave) this.onAutosave();
      }
    } catch (error) {
      console.error("Failed to save emulator state:", error);
    }
  }

  /**
   * Restore emulator state from IndexedDB (auto-save)
   * @returns {Promise<boolean>} True if state was restored
   */
  async restoreState() {
    if (!this.wasmModule) {
      return false;
    }

    try {
      const stateData = await loadStateFromStorage(this.currentMachineKey());
      if (!stateData) {
        return false;
      }

      const success = await this.importStateData(stateData);
      if (success) {
        console.log("Restored emulator state from storage");
        return true;
      }
    } catch (error) {
      console.error("Failed to restore emulator state:", error);
    }

    return false;
  }

  /**
   * Save current state to a numbered slot with screenshot
   * @param {number} slotNumber - Slot number (1-5)
   * @returns {Promise<boolean>}
   */
  async saveToSlot(slotNumber) {
    const stateData = await this.captureStateData();
    if (!stateData) return false;

    const thumbnail = this.captureScreenshot();
    const preview = this.capturePreview();
    await saveStateToSlot(
      slotNumber,
      stateData,
      thumbnail,
      preview,
      this.currentMachineKey(),
    );
    return true;
  }

  /**
   * Restore state from a numbered slot
   * @param {number} slotNumber - Slot number (1-5)
   * @returns {Promise<boolean>}
   */
  async restoreFromSlot(slotNumber) {
    const slot = await loadStateFromSlot(slotNumber);
    if (!slot) return false;

    return await this.importStateData(slot.data);
  }

  /**
   * Restore state from raw file data (e.g. uploaded .a2state file)
   * @param {Uint8Array} stateData
   * @returns {boolean}
   */
  async restoreFromFileData(stateData) {
    return await this.importStateData(stateData);
  }

  /**
   * Check if there's a saved state available
   * @returns {Promise<boolean>}
   */
  async hasSavedState() {
    return hasSavedState(this.currentMachineKey());
  }

  /**
   * Check if auto-save is enabled
   * @returns {boolean}
   */
  isAutoSaveEnabled() {
    return this.autoSaveEnabled;
  }

  /**
   * Clean up resources
   */
  destroy() {
    if (this.autoSaveInterval) {
      clearInterval(this.autoSaveInterval);
      this.autoSaveInterval = null;
    }
  }
}
