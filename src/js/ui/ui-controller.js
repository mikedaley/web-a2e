/*
 * ui-controller.js - Main UI controller and menu wiring
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/**
 * UIController - Manages all toolbar and control UI interactions
 * Handles button clicks, popups, menus, and UI state updates
 */

import { clearStateFromStorage } from "../state/state-persistence.js";
import { ThemeManager } from "./theme-manager.js";

// Timing constants
const REMINDER_DISMISS_DELAY_MS = 2000;
const NOTIFICATION_DISPLAY_MS = 3000;
const STATE_BUTTON_FLASH_MS = 600;

/**
 * @typedef {Object} UIControllerDeps
 * @property {Object} emulator - The emulator instance (for start/stop/running state)
 * @property {Object} wasmModule - The WASM module
 * @property {Object} audioDriver - Audio driver for volume/mute
 * @property {Object} diskManager - Disk manager for drive sounds
 * @property {Object} fileExplorer - File explorer window
 * @property {Object} windowManager - Debug window manager
 * @property {Object} screenWindow - Screen window instance
 * @property {Object} reminderController - Reminder controller
 */

export class UIController {
  /**
   * @param {UIControllerDeps} deps - Dependencies
   */
  constructor(deps) {
    this.emulator = deps.emulator;
    this.wasmModule = deps.wasmModule;
    this.audioDriver = deps.audioDriver;
    this.diskManager = deps.diskManager;
    this.fileExplorer = deps.fileExplorer;
    this.windowManager = deps.windowManager;
    this.screenWindow = deps.screenWindow;
    this.reminderController = deps.reminderController;
    this.inputHandler = deps.inputHandler;
    this.themeManager = deps.themeManager;
    this.windowSwitcher = deps.windowSwitcher;

    this.isFullPageMode = false;
    this.canvas = null;
  }

  /**
   * Initialize all UI controls
   */
  init() {
    this.canvas = document.getElementById("screen");
    if (!this.canvas) {
      console.error("Required DOM element not found: screen");
      return;
    }

    this.setupMenus();
    this.setupPowerControls();
    this.setupAgentButton();
    this.setupFullPageModeControls();
    this.setupSoundControls();
    this.setupSystemMenuActions();
    this.setupHardwareMenuActions();
    this.setupDebugMenuActions();
    this.setupDevMenuActions();
    this.setupHelpMenuActions();
    this.setupThemeSelector();
    this.setupWindowSwitcher();
  }

  /**
   * Helper to refocus canvas after button clicks
   */
  refocusCanvas() {
    if (this.canvas) {
      setTimeout(() => this.canvas.focus(), 0);
    }
  }

  /**
   * Close all open header menus
   */
  closeAllMenus() {
    document.querySelectorAll(".header-menu-container.open").forEach((c) => {
      c.classList.remove("open");
    });
  }

  /**
   * Set up generic menu open/close behavior for all header-menu-container elements
   */
  setupMenus() {
    const containers = document.querySelectorAll(".header-menu-container");

    containers.forEach((container) => {
      const trigger = container.querySelector(".header-menu-trigger");
      if (!trigger) return;

      trigger.addEventListener("click", (e) => {
        e.stopPropagation();
        const wasOpen = container.classList.contains("open");

        // Close all menus first
        this.closeAllMenus();

        // Toggle this one
        if (!wasOpen) {
          container.classList.add("open");
        }
      });

      // Hovering over a different menu trigger while one is open switches menus
      trigger.addEventListener("mouseenter", () => {
        const anyOpen = document.querySelector(".header-menu-container.open");
        if (anyOpen && anyOpen !== container) {
          this.closeAllMenus();
          container.classList.add("open");
        }
      });
    });

    // Close menus when clicking outside
    document.addEventListener("click", (e) => {
      const inMenu = e.target.closest(".header-menu-container");
      if (!inMenu) {
        this.closeAllMenus();
      }
    });

    // Close menus on Escape
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        this.closeAllMenus();
      }
    });

    // Close menus on window resize
    window.addEventListener("resize", () => {
      this.closeAllMenus();
    });
  }

  /**
   * Set up power button and reset controls (System menu items)
   */
  setupPowerControls() {
    const powerBtn = document.getElementById("btn-power");
    if (!powerBtn) {
      console.error("Required DOM element not found: btn-power");
      return;
    }

    // Power button - simple on/off
    powerBtn.addEventListener("click", () => {
      this.reminderController.dismissPowerReminder();
      if (this.emulator.isRunning()) {
        this.emulator.stop();
        this.reminderController.showBasicReminder(false);
      } else {
        this.emulator.start();
        this.reminderController.showBasicReminder(true);
      }
      this.refocusCanvas();
    });
  }

  /**
   * Set up agent button for MCP server connection
   */
  setupAgentButton() {
    console.log("[UIController] setupAgentButton() called");
    const agentBtn = document.getElementById("btn-agent");
    console.log("[UIController] agentBtn:", agentBtn);
    if (!agentBtn) {
      console.warn("[UIController] Agent button not found");
      return;
    }

    const agentManager = this.emulator.agentManager;
    if (!agentManager) {
      console.warn("[UIController] AgentManager not available");
      return;
    }

    console.log("[UIController] Agent button initialized");

    // Hide button by default
    agentBtn.classList.add("hidden");

    const updateButtonState = () => {
      const state = agentManager.getState();

      // Remove all state classes
      agentBtn.classList.remove("connected", "disconnected", "severed", "hidden");

      if (!state.serverAvailable) {
        // Hide button when server is not available
        agentBtn.classList.add("hidden");
        agentBtn.title = "MCP Server not available";
      } else if (state.connected) {
        // Connected - yellow
        agentBtn.classList.add("connected");
        agentBtn.title = "Disconnect";
      } else if (state.reconnecting) {
        // Connection severed but reconnecting - light red
        agentBtn.classList.add("severed");
        agentBtn.title = "Connection lost - Click to abort reconnection";
      } else {
        // Server available but not connected - default appearance
        agentBtn.classList.add("disconnected");
        agentBtn.title = "Connect to Agent";
      }
    };

    // Set up callbacks
    agentManager.onServerAvailable = () => {
      console.log("[UIController] Server became available - showing button");
      updateButtonState();
    };

    agentManager.onServerUnavailable = () => {
      console.log("[UIController] Server became unavailable - hiding button");
      updateButtonState();
    };

    agentManager.onConnectionChange = (connected) => {
      console.log(`[UIController] Connection changed: ${connected}`);
      updateButtonState();
    };

    // Handle button click
    agentBtn.addEventListener("click", () => {
      console.log("[UIController] Agent button clicked");
      const state = agentManager.getState();

      if (agentManager.isConnected()) {
        // Connected - disconnect
        console.log("[UIController] Disconnecting...");
        agentManager.disconnect();
        // Resume heartbeat polling after manual disconnect
        agentManager.startHeartbeatPolling();
      } else if (state.reconnecting) {
        // Reconnecting - abort reconnection and reset to disconnected
        console.log("[UIController] Aborting reconnection attempts...");
        agentManager.disconnect();
        // Don't auto-connect, let user click again if they want
      } else {
        // Disconnected - connect
        console.log("[UIController] Connecting...");
        agentManager.connect();
        setTimeout(() => updateButtonState(), 100);
      }
      updateButtonState();
      this.refocusCanvas();
    });

    // Start heartbeat polling to detect when server becomes available
    agentManager.startHeartbeatPolling();

    updateButtonState();
  }

  /**
   * Set up reset buttons and System menu state management actions
   */
  setupSystemMenuActions() {
    // Warm reset button (top-level, preserves memory)
    const warmResetBtn = document.getElementById("btn-warm-reset");
    if (warmResetBtn) {
      warmResetBtn.addEventListener("click", () => {
        if (this.inputHandler) this.inputHandler.cancelPaste();
        this.wasmModule._warmReset();
        setTimeout(() => {
          this.reminderController.dismissBasicReminder();
        }, REMINDER_DISMISS_DELAY_MS);
        this.refocusCanvas();
      });
    }

    // Cold reset button (top-level, full restart)
    const coldResetBtn = document.getElementById("btn-cold-reset");
    if (coldResetBtn) {
      coldResetBtn.addEventListener("click", async () => {
        if (this.inputHandler) this.inputHandler.cancelPaste();
        this.wasmModule._reset();
        await clearStateFromStorage();
        this.refocusCanvas();
      });
    }

    // Save States window button
    const saveStatesBtn = document.getElementById("btn-save-states");
    if (saveStatesBtn) {
      saveStatesBtn.addEventListener("click", () => {
        this.windowManager.toggleWindow("save-states");
        this.closeAllMenus();
        this.refocusCanvas();
      });
    }

    // Prevent system menu from closing when clicking toggle items
    const systemMenu = document.getElementById("file-menu");
    if (systemMenu) {
      systemMenu.querySelectorAll(".menu-toggle-item").forEach((item) => {
        item.addEventListener("click", (e) => {
          e.stopPropagation();
        });
      });
    }
  }

  /**
   * Set up Hardware menu action items
   */
  setupHardwareMenuActions() {
    const drivesBtn = document.getElementById("btn-drives");
    if (drivesBtn) {
      drivesBtn.addEventListener("click", () => {
        this.windowManager.toggleWindow("disk-drives");
        this.closeAllMenus();
        this.refocusCanvas();
      });
    }

    const hardDrivesBtn = document.getElementById("btn-hard-drives");
    if (hardDrivesBtn) {
      hardDrivesBtn.addEventListener("click", () => {
        this.windowManager.toggleWindow("hard-drives");
        this.closeAllMenus();
        this.refocusCanvas();
      });
    }


    const fileExplorerBtn = document.getElementById("btn-file-explorer");
    if (fileExplorerBtn) {
      fileExplorerBtn.addEventListener("click", () => {
        this.windowManager.toggleWindow("file-explorer-window");
        this.closeAllMenus();
        this.refocusCanvas();
      });
    }

    const displayBtn = document.getElementById("btn-display");
    if (displayBtn) {
      displayBtn.addEventListener("click", () => {
        this.windowManager.toggleWindow("display-settings");
        this.closeAllMenus();
        this.refocusCanvas();
      });
    }

    const joystickBtn = document.getElementById("btn-joystick");
    if (joystickBtn) {
      joystickBtn.addEventListener("click", () => {
        this.windowManager.toggleWindow("joystick");
        this.closeAllMenus();
        this.refocusCanvas();
      });
    }

    const slotsBtn = document.getElementById("btn-slots");
    if (slotsBtn) {
      slotsBtn.addEventListener("click", () => {
        this.windowManager.toggleWindow("slot-configuration");
        this.closeAllMenus();
        this.refocusCanvas();
      });
    }

    const serialBtn = document.getElementById("btn-serial-port");
    if (serialBtn) {
      serialBtn.addEventListener("click", () => {
        this.windowManager.toggleWindow("serial-connection");
        this.closeAllMenus();
        this.refocusCanvas();
      });
    }
  }


  /**
   * Set up debug menu dropdown actions
   */
  setupDebugMenuActions() {
    const debugMenu = document.getElementById("debug-menu");
    if (!debugMenu) return;

    const windowMap = {
      cpu: "cpu-debugger",
      switches: "soft-switches",
      memmap: "memory-map",
      memory: "memory-browser",
      heatmap: "memory-heatmap",
      stack: "stack-viewer",
      zeropage: "zeropage-watch",
      trace: "trace-panel",
      mockingboard: "mockingboard-debug",
      "mouse-card": "mouse-card-debug",
      "basic-debugger": "basic-debugger",
    };

    debugMenu.querySelectorAll(".header-menu-item").forEach((item) => {
      item.addEventListener("click", () => {
        const windowType = item.dataset.window;
        if (windowMap[windowType]) {
          this.windowManager.toggleWindow(windowMap[windowType]);
        }
        this.closeAllMenus();
        this.refocusCanvas();
      });
    });
  }

  /**
   * Set up dev menu dropdown actions
   */
  setupDevMenuActions() {
    const devMenu = document.getElementById("dev-menu");
    if (!devMenu) return;

    const windowMap = {
      basic: "basic-program",
      assembler: "assembler-editor",
    };

    devMenu.querySelectorAll(".header-menu-item").forEach((item) => {
      item.addEventListener("click", () => {
        const windowType = item.dataset.window;
        if (windowMap[windowType]) {
          this.windowManager.toggleWindow(windowMap[windowType]);
        }
        this.closeAllMenus();
        this.refocusCanvas();
      });
    });
  }

  /**
   * Set up Help menu actions
   */
  setupHelpMenuActions() {
    // Documentation (btn-help) is handled by DocumentationWindow via F1 and direct click
    // btn-help is now inside the help menu - DocumentationWindow binds to it in its own create()

    // Release notes menu item
    const releaseNotesMenuBtn = document.getElementById("btn-release-notes-menu");
    if (releaseNotesMenuBtn) {
      releaseNotesMenuBtn.addEventListener("click", () => {
        this.windowManager.toggleWindow("release-notes");
        this.closeAllMenus();
      });
    }

    // Update/refresh button - only visible when installed as PWA
    const updateBtn = document.getElementById("btn-update");
    const isInstalled = window.matchMedia("(display-mode: standalone)").matches || navigator.standalone;
    if (updateBtn && !isInstalled) {
      updateBtn.style.display = "none";
      const separator = updateBtn.previousElementSibling;
      if (separator && separator.classList.contains("header-menu-separator")) {
        separator.style.display = "none";
      }
    }
    if (updateBtn && isInstalled) {
      updateBtn.addEventListener("click", async () => {
        this.closeAllMenus();
        if ("serviceWorker" in navigator) {
          try {
            const registration = await navigator.serviceWorker.getRegistration();
            if (registration) {
              await registration.unregister();
              const cacheNames = await caches.keys();
              await Promise.all(cacheNames.map((name) => caches.delete(name)));
              this.showNotification("Updating... page will reload");
              setTimeout(() => window.location.reload(true), 500);
            } else {
              this.showNotification("No service worker registered");
            }
          } catch (error) {
            console.error("Update failed:", error);
            this.showNotification("Update failed: " + error.message);
          }
        } else {
          window.location.reload(true);
        }
      });
    }
  }

  /**
   * Set up theme selector buttons in View menu
   */
  setupThemeSelector() {
    const buttons = document.querySelectorAll(".theme-btn");
    if (!buttons.length || !this.themeManager) return;

    const updateActive = () => {
      const pref = this.themeManager.getPreference();
      buttons.forEach((btn) => {
        btn.classList.toggle("active", btn.dataset.theme === pref);
      });
    };

    buttons.forEach((btn) => {
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        this.themeManager.setPreference(btn.dataset.theme);
        updateActive();
      });
    });

    updateActive();
  }

  /**
   * Set up Ctrl+` shortcut to toggle the window switcher
   */
  setupWindowSwitcher() {
    if (!this.windowSwitcher) return;

    document.addEventListener('keydown', (e) => {
      if (e.ctrlKey && e.code === 'Backquote') {
        e.preventDefault();
        e.stopPropagation();

        // Exit full-page mode first so the selected window is visible
        if (this.isFullPageMode) {
          const fullscreenBtn = document.getElementById('btn-fullscreen');
          if (fullscreenBtn) fullscreenBtn.click();
        }

        this.windowSwitcher.toggle();
      }

      // Option+Tab / Option+Shift+Tab to cycle through visible windows
      if (e.altKey && e.code === 'Tab') {
        e.preventDefault();
        e.stopPropagation();
        this.windowManager.cycleWindow(e.shiftKey);
      }
    }, { capture: true });
  }

  /**
   * Sync the full-page toolbar power button with emulator state
   */
  syncFullPagePowerButton() {
    const fpPower = document.getElementById("fp-power");
    if (!fpPower) return;

    if (this.emulator.isRunning()) {
      fpPower.classList.remove("off");
      fpPower.title = "Power Off";
    } else {
      fpPower.classList.add("off");
      fpPower.title = "Power On";
    }
  }

  /**
   * Set up fullscreen/full page mode controls
   */
  setupFullPageModeControls() {
    const fullscreenBtn = document.getElementById("btn-fullscreen");
    if (!fullscreenBtn) return;

    const exitFullPageMode = () => {
      document.body.classList.remove("full-page-mode");
      this.isFullPageMode = false;

      // Restore ScreenWindow and re-attach canvas from monitor-frame
      if (this.screenWindow) {
        this.screenWindow.show();
        this.screenWindow.attachCanvas();
      }

      // Restore windows that were visible before entering full page mode
      if (this._windowsBeforeFullPage) {
        for (const id of this._windowsBeforeFullPage) {
          this.windowManager.showWindow(id);
        }
        this._windowsBeforeFullPage = null;
      }

      this.refocusCanvas();
    };

    const enterFullPageMode = () => {
      // Close any open menus
      this.closeAllMenus();

      // Remember which windows are visible before hiding them
      this._windowsBeforeFullPage = this.windowManager.getVisibleWindowIds();

      // Detach canvas from ScreenWindow into monitor-frame for full-page rendering
      if (this.screenWindow && this.screenWindow.isVisible) {
        this.screenWindow.detachCanvas();
        this.screenWindow.isVisible = false;
        this.screenWindow.element.classList.add('hidden');
      }

      this.windowManager.hideAll();
      document.body.classList.add("full-page-mode");
      this.isFullPageMode = true;
      this.syncFullPagePowerButton();
      this.refocusCanvas();
    };

    fullscreenBtn.addEventListener("click", () => {
      if (this.isFullPageMode) {
        exitFullPageMode();
      } else {
        enterFullPageMode();
      }
    });

    // Exit full page mode on Ctrl+Escape
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && e.ctrlKey && this.isFullPageMode) {
        e.preventDefault();
        exitFullPageMode();
      }
    });

    // Exit full page mode on click (on the black background area)
    const mainEl = document.querySelector("main");
    if (mainEl) {
      mainEl.addEventListener("click", (e) => {
        if (this.isFullPageMode && e.target.tagName !== "CANVAS") {
          exitFullPageMode();
        }
      });
    }

    // --- Full-page toolbar button handlers ---

    const fpPower = document.getElementById("fp-power");
    if (fpPower) {
      fpPower.addEventListener("click", (e) => {
        e.stopPropagation();
        this.reminderController.dismissPowerReminder();
        if (this.emulator.isRunning()) {
          this.emulator.stop();
        } else {
          this.emulator.start();
        }
        this.syncFullPagePowerButton();
        this.refocusCanvas();
      });
    }

    const fpWarmReset = document.getElementById("fp-warm-reset");
    if (fpWarmReset) {
      fpWarmReset.addEventListener("click", (e) => {
        e.stopPropagation();
        if (this.inputHandler) this.inputHandler.cancelPaste();
        this.wasmModule._warmReset();
        this.refocusCanvas();
      });
    }

    const fpColdReset = document.getElementById("fp-cold-reset");
    if (fpColdReset) {
      fpColdReset.addEventListener("click", async (e) => {
        e.stopPropagation();
        if (this.inputHandler) this.inputHandler.cancelPaste();
        this.wasmModule._reset();
        await clearStateFromStorage();
        this.refocusCanvas();
      });
    }

    const fpExit = document.getElementById("fp-exit");
    if (fpExit) {
      fpExit.addEventListener("click", (e) => {
        e.stopPropagation();
        exitFullPageMode();
      });
    }
  }

  /**
   * Set up sound controls popup
   */
  setupSoundControls() {
    const soundBtn = document.getElementById("btn-sound");
    const soundPopup = document.getElementById("sound-popup");
    const volumeSlider = document.getElementById("volume-slider");
    const volumeValue = document.getElementById("volume-value");
    const muteToggle = document.getElementById("mute-toggle");
    const driveSoundsToggle = document.getElementById("drive-sounds-toggle");
    if (!soundBtn || !soundPopup) return;

    // Load saved drive sounds setting
    const savedDriveSounds = localStorage.getItem("a2e-drive-sounds");
    const driveSoundsEnabled = savedDriveSounds !== "false";
    if (driveSoundsToggle) {
      driveSoundsToggle.checked = driveSoundsEnabled;
    }
    this.diskManager.setSeekSoundEnabled(driveSoundsEnabled);
    this.diskManager.setMotorSoundEnabled(driveSoundsEnabled);

    // Toggle popup on button click
    soundBtn.addEventListener("click", (e) => {
      e.stopPropagation();
      // Close header menus when opening sound popup
      this.closeAllMenus();
      soundPopup.classList.toggle("hidden");
    });

    // Close popup when clicking outside
    document.addEventListener("click", (e) => {
      if (!soundPopup.contains(e.target) && e.target !== soundBtn) {
        soundPopup.classList.add("hidden");
      }
    });

    // Prevent popup from closing when clicking inside
    soundPopup.addEventListener("click", (e) => {
      e.stopPropagation();
    });

    // Volume slider
    if (volumeSlider && volumeValue) {
      const initialVolume = Math.round(this.audioDriver.getVolume() * 100);
      volumeSlider.value = initialVolume;
      volumeValue.textContent = `${initialVolume}%`;
      this.diskManager.setMasterVolume(this.audioDriver.isMuted() ? 0 : this.audioDriver.getVolume());

      volumeSlider.addEventListener("input", (e) => {
        const volume = parseInt(e.target.value, 10);
        volumeValue.textContent = `${volume}%`;
        this.audioDriver.setVolume(volume / 100);
        if (!this.audioDriver.isMuted()) {
          this.diskManager.setMasterVolume(volume / 100);
        }
      });
    }

    // Mute toggle
    if (muteToggle) {
      muteToggle.checked = this.audioDriver.isMuted();
      muteToggle.addEventListener("change", (e) => {
        if (e.target.checked) {
          this.audioDriver.mute();
          this.diskManager.setMasterVolume(0);
        } else {
          this.audioDriver.unmute();
          this.diskManager.setMasterVolume(this.audioDriver.getVolume());
        }
        this.updateSoundButton();
      });
    }

    // Sync sound button icon with persisted mute state
    this.updateSoundButton();

    // Drive sounds toggle
    if (driveSoundsToggle) {
      driveSoundsToggle.addEventListener("change", (e) => {
        const enabled = e.target.checked;
        this.diskManager.setSeekSoundEnabled(enabled);
        this.diskManager.setMotorSoundEnabled(enabled);
        localStorage.setItem("a2e-drive-sounds", enabled);
      });
    }

    // Character set toggle (UK/US) - screen window header
    const screenWindowCharsetToggle = document.getElementById("screen-window-charset-toggle");

    const syncCharsetToggle = (isUK) => {
      this.wasmModule._setUKCharacterSet(isUK);
      localStorage.setItem("a2e-charset", isUK ? "uk" : "us");
      if (screenWindowCharsetToggle) screenWindowCharsetToggle.checked = !isUK;
    };

    // Initialize from saved setting
    const savedCharset = localStorage.getItem("a2e-charset");
    const isUKInitial = savedCharset === "uk";
    this.wasmModule._setUKCharacterSet(isUKInitial);
    if (screenWindowCharsetToggle) screenWindowCharsetToggle.checked = !isUKInitial;

    // Screen window header toggle listener
    if (screenWindowCharsetToggle) {
      screenWindowCharsetToggle.addEventListener("change", (e) => {
        syncCharsetToggle(!e.target.checked);
      });
    }
  }

  /**
   * Update sound button icon based on mute state
   */
  updateSoundButton() {
    const soundBtn = document.getElementById("btn-sound");
    if (!soundBtn) return;

    const iconUnmuted = soundBtn.querySelector(".icon-unmuted");
    const iconMuted = soundBtn.querySelector(".icon-muted");

    if (this.audioDriver.isMuted()) {
      iconUnmuted?.classList.add("hidden");
      iconMuted?.classList.remove("hidden");
    } else {
      iconUnmuted?.classList.remove("hidden");
      iconMuted?.classList.add("hidden");
    }
  }

  /**
   * Update power button appearance based on running state
   * @param {boolean} isRunning - Whether the emulator is running
   */
  updatePowerButton(isRunning) {
    const powerBtn = document.getElementById("btn-power");

    if (isRunning) {
      powerBtn?.classList.remove("off");
      if (powerBtn) powerBtn.title = "Power Off";
    } else {
      powerBtn?.classList.add("off");
      if (powerBtn) powerBtn.title = "Power On";
    }

    this.syncFullPagePowerButton();
  }

  /**
   * Flash the system menu trigger to indicate saving
   */
  flashStateButton() {
    const trigger = document.getElementById("btn-file-menu");
    if (!trigger) return;

    trigger.classList.add("saving");
    setTimeout(() => {
      trigger.classList.remove("saving");
    }, STATE_BUTTON_FLASH_MS);
  }

  /**
   * Show a notification message
   * @param {string} message - The message to display
   */
  showNotification(message) {
    let notification = document.getElementById("state-notification");
    if (!notification) {
      notification = document.createElement("div");
      notification.id = "state-notification";
      notification.className = "state-notification";
      document.body.appendChild(notification);
    }

    notification.textContent = message;
    notification.classList.add("visible");

    setTimeout(() => {
      notification.classList.remove("visible");
    }, NOTIFICATION_DISPLAY_MS);
  }

  /**
   * Check if in full page mode
   * @returns {boolean}
   */
  isInFullPageMode() {
    return this.isFullPageMode;
  }

  /**
   * Get the ID of the window that currently has focus, or null if none
   * @returns {string|null}
   */
  get hasFocus() {
    for (const [id, win] of this.windowManager.windows) {
      if (win.isVisible && win.element.classList.contains('focused')) {
        return id;
      }
    }
    return null;
  }

  /**
   * Bring a window to the front and give it focus
   * @param {string} id - The window ID to focus
   */
  focusWindow(id) {
    const win = this.windowManager.getWindow(id);
    if (win) {
      if (!win.isVisible) {
        this.windowManager.showWindow(id);
      } else {
        this.windowManager.bringToFront(id);
      }
    }
  }
}
