/*
 * main.js - Main entry point and AppleIIeEmulator class
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

// CSS imports - bundled by Vite with content hashes for cache busting
import "../css/base.css";
import "../css/layout.css";
import "../css/monitor.css";
import "../css/disk-drives.css";
import "../css/hard-drive.css";
import "../css/controls.css";
import "../css/modals.css";
import "../css/debug-base.css";
import "../css/cpu-debugger.css";
import "../css/memory-windows.css";
import "../css/settings-windows.css";
import "../css/save-states.css";
import "../css/rule-builder.css";
import "../css/basic-editor.css";
import "../css/basic-debugger.css";
import "../css/assembler-editor.css";
import "../css/release-notes.css";
import "../css/file-explorer.css";
import "../css/printer.css";
import "../css/documentation.css";
import "../css/window-switcher.css";
import "../css/docking.css";
import "../css/fullscreen-popouts.css";
import "../css/responsive.css";

import { VERSION } from "./config/version.js";
import { featureFlags } from "./config/feature-flags.js";
import { DEFAULT_LAYOUT } from "./config/default-layout.js";
import { WebGLRenderer } from "./display/webgl-renderer.js";
import { AudioDriver } from "./audio/audio-driver.js";
import { WasmProxy } from "./worker/wasm-proxy.js";
import {
  allocateSharedBuffers,
  FB_BYTES,
  CTRL_FRAME_READY,
  CTRL_FRAME_INDEX,
} from "./worker/shared-buffers.js";
import { InputHandler, TextSelection, JoystickWindow, MouseHandler, GamepadHandler } from "./input/index.js";
import { DiskManager } from "./disk-manager/index.js";
import { DiskDrivesWindow } from "./disk-manager/disk-drives-window.js";
import { HardDriveManager } from "./disk-manager/hard-drive-manager.js";
import { unzipSync } from "fflate";

// Monkey patch HardDriveManager to support on-the-fly decompression for Cloudflare Pages limits
const originalGetLibraryImageData = HardDriveManager.prototype._getLibraryImageData;
if (originalGetLibraryImageData) {
  HardDriveManager.prototype._getLibraryImageData = async function (entry) {
    const data = await originalGetLibraryImageData.apply(this, arguments);
    // Check for ZIP signature (PK\x03\x04)
    if (data && data.length > 4 && data[0] === 0x50 && data[1] === 0x4b && data[2] === 0x03 && data[3] === 0x04) {
      try {
        const unzipped = unzipSync(data);
        const filenames = Object.keys(unzipped);
        if (filenames.length > 0) {
          console.log(`MonkeyPatch: Unzipped ${entry.file} content on the fly`);
          return unzipped[filenames[0]];
        }
      } catch (e) {
        console.error("MonkeyPatch: Failed to unzip", e);
      }
    }
    return data;
  };
}
import { HardDriveWindow } from "./disk-manager/hard-drive-window.js";
import { readUrlMedia, loadUrlMedia } from "./disk-manager/url-media-loader.js";
import { FileExplorerWindow } from "./file-explorer/index.js";
import { DisplaySettingsWindow, ScreenWindow } from "./display/index.js";
import { DocumentationWindow, ReleaseNotesWindow } from "./help/index.js";
import { ReminderController } from "./ui/reminder-controller.js";
import { UIController } from "./ui/ui-controller.js";
import { ThemeManager } from "./ui/theme-manager.js";
import { showToast } from "./ui/toast.js";
import { SlotConfigurationWindow } from "./ui/slot-configuration-window.js";
import { SerialConnectionWindow } from "./serial/serial-connection-window.js";
import { PrinterWindow } from "./printer/printer-window.js";
import { PrinterManager } from "./printer/printer-manager.js";
import { PrintBrowserWindow } from "./printer/print-browser-window.js";
import { HayesModem } from "./serial/hayes-modem.js";
import { WindowSwitcher } from "./ui/window-switcher.js";
import { EmulationSpeed, clockLabel } from "./ui/emulation-speed.js";
import { StateManager } from "./state/state-manager.js";
import { SaveStatesWindow } from "./state/save-states-window.js";
import { AgentManager } from "./agent/index.js";
import { SerialManager } from "./serial/serial-manager.js";
import { DockManager } from "./docking/index.js";
import {
  WindowManager,
  CPUDebuggerWindow,
  SoftSwitchWindow,
  MemoryBrowserWindow,
  MemoryHeatMapWindow,
  MemoryMapWindow,
  StackViewerWindow,
  ZeroPageWatchWindow,
  MockingboardWindow,
  MouseCardWindow,
  BasicProgramWindow,
  RuleBuilderWindow,
  AssemblerEditorWindow,
  TracePanelWindow,
} from "./debug/index.js";

class AppleIIeEmulator {
  constructor() {
    this.wasmModule = null;
    this.renderer = null;
    this.audioDriver = null;
    this.inputHandler = null;
    this.diskManager = null;
    this.hardDriveManager = null;
    this.fileExplorer = null;
    this.windowManager = null;
    this.displaySettings = null;
    this.textSelection = null;
    this.reminderController = null;
    this.documentationWindow = null;
    this.uiController = null;
    this.stateManager = null;
    this.mouseHandler = null;
    this.themeManager = null;
    this.agentManager = null;
    this.serialManager = null;
    this.modem = null;
    this.dockManager = null;

    this.running = false;
  }

  async init() {
    // Apply theme before any rendering to prevent flash of wrong theme
    this.themeManager = new ThemeManager();

    this.showLoading(true);

    try {
      // Load WASM module in a Web Worker via proxy.
      // Cache-bust the loader AND the .wasm fetch: in dev use a per-load token
      // so rebuilds are always picked up; in prod key off the app version.
      this.wasmModule = new WasmProxy();
      const wasmBust = import.meta.env.DEV ? Date.now() : VERSION;
      await this.wasmModule.init(`/a2e.js?v=${wasmBust}`);

      // Set up renderer
      const canvas = document.getElementById("screen");
      this.renderer = new WebGLRenderer(canvas);
      await this.renderer.init();

      // Set up audio driver (relay mode — Worker generates samples)
      this.audioDriver = new AudioDriver(this.wasmModule);

      // Audio-driven timing: AudioWorklet requests samples → Worker generates
      this.audioDriver.onSamplesRequested = (count) => {
        this.wasmModule.requestSamples(count);
      };

      // ...and while audio is still asleep, the Worker paces itself instead.
      this.audioDriver.onFreeRunChange = (enabled) => {
        this.wasmModule.setFreeRun(enabled);
      };

      this.frameReady = false;
      this.setupSharedBuffers();

      // Fallback transport, used when SharedArrayBuffer is unavailable (no
      // COOP/COEP headers). Both paths end up setting _lastFramebuffer.
      this.wasmModule.onAudioSamples = (samples) => {
        this.audioDriver.relaySamples(samples);
      };
      this.wasmModule.onFrameReady = (fbData) => {
        this._lastFramebuffer = fbData;
        this.frameReady = true;
      };

      // Set up input handler
      this.inputHandler = new InputHandler(this.wasmModule);
      this.inputHandler.init();

      // Set up the user-selectable CPU speed. Applied before the machine is
      // started so a saved speed is live from the first cycle.
      this.emulationSpeed = new EmulationSpeed({
        wasmModule: this.wasmModule,
        inputHandler: this.inputHandler,
      });
      this.emulationSpeed.apply();

      // Set up mouse handler for Apple Mouse Interface Card
      this.mouseHandler = new MouseHandler(this.wasmModule);
      this.mouseHandler.init();

      // Set up window manager
      this.windowManager = new WindowManager();

      // Set up file explorer (registered with window manager for proper z-index/focus)
      this.fileExplorer = new FileExplorerWindow(this.wasmModule);
      this.fileExplorer.create();
      this.windowManager.register(this.fileExplorer);

      // Create disk drives window first so DiskManager can find its DOM elements
      const diskDrivesWindow = new DiskDrivesWindow();
      diskDrivesWindow.create();
      this.windowManager.register(diskDrivesWindow);

      // Read any ?disk=/?hd= parameters before the managers restore their
      // persisted images, so the units a link claims are left alone rather than
      // being loaded and then immediately replaced.
      this.urlMedia = readUrlMedia(window.location);

      // Set up disk manager (must be after disk drives window is created)
      this.diskManager = new DiskManager(this.wasmModule);
      this.diskManager.isRunningCallback = () => this.running;
      this.diskManager.urlOwnedDrives = new Set(
        this.urlMedia.floppies.map((f) => f.unit),
      );
      this.diskManager.init();
      this.diskManager.onDiskLoaded = () => {
        this.reminderController?.dismissBasicReminder();
      };

      // Create hard drive window and manager
      const hardDriveWindow = new HardDriveWindow();
      hardDriveWindow.create();
      this.windowManager.register(hardDriveWindow);

      this.diskManager.fileExplorer = this.fileExplorer;

      this.hardDriveManager = new HardDriveManager(this.wasmModule);
      this.hardDriveManager.isRunningCallback = () => this.running;
      this.hardDriveManager.fileExplorer = this.fileExplorer;
      this.hardDriveManager.urlOwnedDevices = new Set(
        this.urlMedia.hardDrives.map((h) => h.unit),
      );
      this.hardDriveManager.init();


      const cpuWindow = new CPUDebuggerWindow(this.wasmModule, () => this.isRunning());
      cpuWindow.create();
      this.windowManager.register(cpuWindow);
      this.cpuDebuggerWindow = cpuWindow;

      const ruleBuilderWindow = new RuleBuilderWindow();
      ruleBuilderWindow.create();
      this.windowManager.register(ruleBuilderWindow);

      ruleBuilderWindow.onApply = (addr, condStr, rules) => {
        cpuWindow.bpManager.setCondition(addr, condStr);
        cpuWindow.bpManager.setConditionRules(addr, rules);
      };
      cpuWindow.setRuleBuilder(ruleBuilderWindow);

      // BASIC breakpoint condition callback - will be wired after basicProgramWindow is created
      ruleBuilderWindow.onApplyBasic = (key, condStr, rules) => {
        if (this.basicProgramWindow) {
          const bpMgr = this.basicProgramWindow.breakpointManager;
          if (key === "__new_rule__") {
            // New condition-only rule
            bpMgr.addConditionRule(condStr, rules);
          } else {
            const [lineStr, stmtStr] = key.split(":");
            const lineNum = parseInt(lineStr, 10);
            const stmtIdx = parseInt(stmtStr, 10);
            bpMgr.setCondition(lineNum, stmtIdx, condStr);
            bpMgr.setConditionRules(lineNum, stmtIdx, rules);
          }
          this.basicProgramWindow.renderBreakpointList();
        }
      };

      const switchWindow = new SoftSwitchWindow(this.wasmModule);
      switchWindow.create();
      this.windowManager.register(switchWindow);

      // Set up display settings window (pass renderer for shader control, wasmModule for video settings)
      this.displaySettings = new DisplaySettingsWindow(
        this.renderer,
        this.wasmModule,
      );
      this.displaySettings.create();
      this.windowManager.register(this.displaySettings);

      // Set up screen window (hosts the emulator canvas)
      this.screenWindow = new ScreenWindow(this.renderer, null); // textSelection set later
      this.screenWindow.create();
      this.windowManager.register(this.screenWindow);

      // Set up new debug windows
      const memBrowserWindow = new MemoryBrowserWindow(this.wasmModule);
      memBrowserWindow.create();
      this.windowManager.register(memBrowserWindow);

      const memHeatMapWindow = new MemoryHeatMapWindow(this.wasmModule);
      memHeatMapWindow.create();
      this.windowManager.register(memHeatMapWindow);

      // Connect heat map to memory browser for click-to-jump
      memHeatMapWindow.setJumpCallback((addr) => {
        memBrowserWindow.jumpToAddress(addr);
        this.windowManager.showWindow("memory-browser");
      });

      const memMapWindow = new MemoryMapWindow(this.wasmModule);
      memMapWindow.create();
      this.windowManager.register(memMapWindow);

      const stackWindow = new StackViewerWindow(this.wasmModule);
      stackWindow.create();
      this.windowManager.register(stackWindow);

      const zpWatchWindow = new ZeroPageWatchWindow(this.wasmModule);
      zpWatchWindow.create();
      this.windowManager.register(zpWatchWindow);

      const joystickWindow = new JoystickWindow(this.wasmModule);
      joystickWindow.create();
      this.windowManager.register(joystickWindow);

      this.gamepadHandler = new GamepadHandler(this.wasmModule, joystickWindow);
      joystickWindow.gamepadHandler = this.gamepadHandler;
      this.inputHandler.joystickWindow = joystickWindow;

      // Show accelerated speeds in the monitor title bar
      this.emulationSpeed.onChange((multiplier) => {
        this.screenWindow.setSpeedState(multiplier, clockLabel(multiplier));
      });

      // Wire monitor header toggle to joystick cursor keys
      this.screenWindow.setCursorKeysState(joystickWindow.cursorKeysEnabled);
      this.screenWindow.onCursorKeysToggle((enabled) => {
        joystickWindow.setCursorKeysEnabled(enabled);
      });
      joystickWindow.onCursorKeysChanged = (enabled) => {
        this.screenWindow.setCursorKeysState(enabled);
        this.uiController?.setCursorKeysMenuState(enabled);
      };

      const mockingboardWindow = new MockingboardWindow(this.wasmModule);
      mockingboardWindow.create();
      this.windowManager.register(mockingboardWindow);

      const mouseCardWindow = new MouseCardWindow(this.wasmModule);
      mouseCardWindow.create();
      this.windowManager.register(mouseCardWindow);

      const tracePanelWindow = new TracePanelWindow(this.wasmModule);
      tracePanelWindow.create();
      this.windowManager.register(tracePanelWindow);

      const basicProgramWindow = new BasicProgramWindow(
        this.wasmModule,
        this.inputHandler,
        () => this.isRunning(),
      );
      basicProgramWindow.create();
      this.windowManager.register(basicProgramWindow);
      this.basicProgramWindow = basicProgramWindow;
      basicProgramWindow.setRuleBuilder(ruleBuilderWindow);

      const assemblerWindow = new AssemblerEditorWindow(this.wasmModule, cpuWindow.bpManager, () => this.isRunning(), cpuWindow);
      // A DSK directive writes into the image the drive is holding, so the
      // stored copy has to be refreshed or the file vanishes on reload.
      assemblerWindow.onObjectFileWritten = async (driveNum) => {
        await this.diskManager?.persistDriveImage(driveNum);
        const explorer = this.fileExplorer;
        if (explorer?.sourceType === "floppy" && explorer.selectedDrive === driveNum) {
          await explorer.loadDisk();
        }
      };
      assemblerWindow.create();
      this.windowManager.register(assemblerWindow);

      // Slot configuration window
      const slotConfigWindow = new SlotConfigurationWindow(
        this.wasmModule,
        async () => {
          await this.wasmModule._reset();
          await this.updateMouseHandlerState();
          if (this.hardDriveManager) {
            this.hardDriveManager.syncWithEmulatorState();
          }
        },
      );
      // Awaited: create() restores the saved slot cards into WASM via
      // applyInitialSettings(). updateMouseHandlerState() below reads those slots
      // to arm ⌥-click capture — if we don't wait, it races the restore, sees an
      // empty slot 4, and leaves mouse capture disabled until the next slot edit.
      await slotConfigWindow.create();
      this.windowManager.register(slotConfigWindow);

      // Release notes window
      this.releaseNotesWindow = new ReleaseNotesWindow();
      this.releaseNotesWindow.create();
      this.windowManager.register(this.releaseNotesWindow);

      // Release notes button in footer
      const releaseNotesBtn = document.getElementById("btn-release-notes");
      if (releaseNotesBtn) {
        releaseNotesBtn.addEventListener("click", () => {
          this.windowManager.toggleWindow("release-notes");
        });
      }

      // Set up documentation window
      this.documentationWindow = new DocumentationWindow();
      this.documentationWindow.create();
      this.windowManager.register(this.documentationWindow);

      // Load saved window states (must be after all windows are registered)
      this.windowManager.loadState();

      // Save window states when page is closed (unless a reset is in progress)
      window.addEventListener("beforeunload", () => {
        if (this.windowManager && !window._resettingDefaults) {
          this.windowManager.saveState();
        }
      });

      // Start with TV static "no signal" since emulator is off
      this.renderer.setNoSignal(true);

      // Set up text selection for copying screen contents
      this.textSelection = new TextSelection(canvas, this.wasmModule, this.renderer);

      // Wire textSelection into screen window
      this.screenWindow.textSelection = this.textSelection;

      // Set up reminder controller
      this.reminderController = new ReminderController();

      // Apply display settings
      this.displaySettings.applyAllSettings();

      // Ensure ScreenWindow is visible (loadState may have already shown it)
      if (!this.screenWindow.isVisible) {
        this.screenWindow.show();
      }
      this.screenWindow.attachCanvas();

      // Position/size windows for first-time users (no saved state)
      this.windowManager.applyDefaultLayout(DEFAULT_LAYOUT);

      // Set up window switcher (Ctrl+`)
      this.windowSwitcher = new WindowSwitcher(this.windowManager);
      this.windowSwitcher.create();

      // Set up agent manager for MCP server connection
      window.emulator = this;
      this.agentManager = new AgentManager();

      // Set up serial manager and Hayes modem for Super Serial Card
      this.serialManager = new SerialManager(this.wasmModule);
      this.modem = new HayesModem(this.wasmModule, this.serialManager);
      await this.wasmModule._setSerialTxCallback();

      // Serial connection window
      const serialConnectionWindow = new SerialConnectionWindow(this.modem);
      serialConnectionWindow.create();
      this.windowManager.register(serialConnectionWindow);

      // Printer window
      const printerManager = new PrinterManager(
        this.wasmModule,
        () => this.audioDriver?.audioContext || null,
      );
      const printerWindow  = new PrinterWindow(printerManager);
      printerWindow.create();
      this.windowManager.register(printerWindow);
      this.printerManager = printerManager;
      this.printerWindow  = printerWindow;
      // Install the printer output callback at startup so PR#n capture works
      // even when the Printer window is closed. The worker WASM is already
      // ready here (wasmModule.init() was awaited earlier), so the RPC lands.
      printerManager.init().catch((e) => console.warn("printer init failed:", e));
      // Sync interface availability from the already-applied slot config, then
      // keep it live as the user changes cards in Expansion Slots.
      printerManager.updateSlots(slotConfigWindow.slotAssignments);
      slotConfigWindow.onSlotsApplied = (assignments) => printerManager.updateSlots(assignments);

      // Print Browser — manages the pages auto-captured to IndexedDB by the
      // printer window. Reads the store; can also send a stored job back to the
      // printer window's paper (re-preview / extend), hence the window ref.
      const printBrowserWindow = new PrintBrowserWindow(printerWindow);
      printBrowserWindow.create();
      this.windowManager.register(printBrowserWindow);
      this.printBrowserWindow = printBrowserWindow;

      // Set up UI controller
      this.uiController = new UIController({
        emulator: this,
        wasmModule: this.wasmModule,
        audioDriver: this.audioDriver,
        diskManager: this.diskManager,
        fileExplorer: this.fileExplorer,
        windowManager: this.windowManager,
        screenWindow: this.screenWindow,
        reminderController: this.reminderController,
        inputHandler: this.inputHandler,
        themeManager: this.themeManager,
        windowSwitcher: this.windowSwitcher,
        emulationSpeed: this.emulationSpeed,
      });
      this.uiController.init();

      // Set up state manager
      this.stateManager = new StateManager({
        emulator: this,
        wasmModule: this.wasmModule,
        uiController: this.uiController,
        diskManager: this.diskManager,
        reminderController: this.reminderController,
        cpuDebuggerWindow: cpuWindow,
        basicProgramWindow: this.basicProgramWindow,
      });
      this.stateManager.init();

      // Save States window
      const saveStatesWindow = new SaveStatesWindow(this.stateManager, this.uiController);
      saveStatesWindow.create();
      this.windowManager.register(saveStatesWindow);

      // Keep autosave row current when the window is open
      this.stateManager.onAutosave = () => {
        if (saveStatesWindow.isVisible) {
          saveStatesWindow.refreshAutosaveRow();
        }
      };

      // Apply feature flags
      if (!featureFlags.isEnabled('serialPort')) {
        const el = document.getElementById('btn-serial-port');
        if (el) el.style.display = 'none';
      }

      // Enable mouse handler if a mouse card is configured
      this.updateMouseHandlerState();

      // Set up dock manager (after all windows registered so drag hooks cover everything)
      this.dockManager = new DockManager(this.windowManager);
      this.windowManager.dockManager = this.dockManager;
      this.dockManager.init();

      // Start render loop
      this.startRenderLoop();

      // Fetch anything the URL asked for. Done last so a slow or dead host
      // delays only the disks, not the rest of the UI coming up.
      await this.loadUrlMedia();

      this.showLoading(false);
      this.reminderController.showPowerReminder(true);
      this.autostart();

      console.log("Apple //e Emulator initialized");
    } catch (error) {
      console.error("Failed to initialize emulator:", error);
      this.showLoading(false);
      showToast("Failed to initialize emulator: " + error.message, "error");
    }
  }

  /**
   * Fetch and insert the disk images named by URL parameters.
   *
   * These loads are transient by design (see DiskManager.loadDiskFromUrlData).
   * Autosave is switched off for the session once one lands, because otherwise
   * the periodic save would fold a stranger's disk into the visitor's own
   * autosave slot — persisting through the back door what the drives went out
   * of their way not to persist. The stored preference is left untouched, so
   * autosave returns to normal on the next plain visit.
   */
  async loadUrlMedia() {
    if (!this.urlMedia) return;

    const loaded = await loadUrlMedia({
      media: this.urlMedia,
      diskManager: this.diskManager,
      hardDriveManager: this.hardDriveManager,
    });

    if (loaded > 0) {
      this.stateManager?.suspendAutoSave("disks were loaded from the URL");
    }
  }

  /**
   * Power on for `?autostart=`, with no interaction at all.
   *
   * The machine runs immediately: the Worker paces itself from a timer while
   * the AudioContext is still suspended (see AudioDriver.setFreeRun), so a
   * link boots its disk in front of the visitor rather than waiting to be
   * touched. The one thing a browser genuinely will not allow is sound before
   * a gesture, so the machine starts silent and the speaker joins in the
   * moment the visitor clicks or types anything.
   */
  autostart() {
    if (!this.urlMedia?.autostart || this.isRunning()) return;

    this.uiController?.powerOn({
      // A URL disk is about to boot, so the BASIC hint would be wrong.
      showBasicHint: this.urlMedia.floppies.length === 0,
    });
  }

  /**
   * Check slot configuration and enable/disable mouse handler accordingly
   */
  async updateMouseHandlerState() {
    if (!this.mouseHandler) return;
    let mousePresent = false;
    for (let slot = 1; slot <= 7; slot++) {
      const ptr = await this.wasmModule._getSlotCard(slot);
      if (ptr) {
        const name = await this.wasmModule.UTF8ToString(ptr);
        if (name === "mouse") {
          mousePresent = true;
          break;
        }
      }
    }
    if (mousePresent) {
      this.mouseHandler.enable();
    } else {
      this.mouseHandler.disable();
    }
  }

  /**
   * Check if the emulator is running
   * @returns {boolean}
   */
  isRunning() {
    return this.running;
  }

  async start() {
    if (this.running) return;

    if (this.inputHandler) this.inputHandler.cancelPaste();
    await this.wasmModule._reset();
    this.running = true;
    this.renderer.setNoSignal(false);
    this.audioDriver.start();
    if (this.uiController) {
      this.uiController.updatePowerButton(true);
    }
    console.log("Emulator powered on");
  }

  async stop() {
    if (!this.running) return;

    this.running = false;
    this.audioDriver.stop();

    this.wasmModule._stopDiskMotor();

    this.renderer.setNoSignal(true);
    if (this.uiController) {
      this.uiController.updatePowerButton(false);
    }
    console.log("Emulator powered off");
  }

  captureScreenshot() {
    if (!this._lastFramebuffer) return null;

    const width = 560;
    const height = 384;

    if (!this._screenshotCanvas) {
      this._screenshotCanvas = document.createElement("canvas");
      this._screenshotCanvas.width = width;
      this._screenshotCanvas.height = height;
      this._screenshotCtx = this._screenshotCanvas.getContext("2d");
    }

    const copy = new Uint8ClampedArray(this._lastFramebuffer);
    const imageData = new ImageData(copy, width, height);
    this._screenshotCtx.putImageData(imageData, 0, 0);
    return this._screenshotCanvas.toDataURL("image/png");
  }

  /**
   * Set up the SharedArrayBuffer transport for audio and video, when the page
   * is cross-origin isolated enough to allow it.
   *
   * Without this the Worker allocates and posts a fresh 860KB framebuffer every
   * frame, and every audio sample batch is relayed worklet → main → Worker →
   * main → worklet, putting the main thread in the audio critical path. With
   * it, both travel through shared memory and neither allocates.
   *
   * Failure is not fatal: leaving _sharedControl null keeps the postMessage
   * path, which stays fully functional.
   */
  setupSharedBuffers() {
    this._sharedControl = null;
    this._sharedFrameViews = null;

    const buffers = allocateSharedBuffers();
    if (!buffers) {
      console.info("SharedArrayBuffer unavailable — using postMessage transport");
      return;
    }

    this._sharedControl = new Int32Array(buffers.control);
    // One view per slot, created once, so picking up a frame costs no allocation.
    this._sharedFrameViews = [
      new Uint8Array(buffers.framebuffer, 0, FB_BYTES),
      new Uint8Array(buffers.framebuffer, FB_BYTES, FB_BYTES),
    ];

    this.audioDriver.setSharedAudioBuffer(buffers.audio);
    this.wasmModule.configureSharedAudio(buffers.audio);
    this.wasmModule.configureSharedBuffers(
      buffers.framebuffer,
      buffers.control,
      FB_BYTES,
    );
  }

  /**
   * Pick up a completed frame from the shared framebuffer, if one is waiting.
   * Clearing the flag with exchange means we never re-upload the same frame.
   */
  pollSharedFrame() {
    if (!this._sharedControl) return;
    if (Atomics.exchange(this._sharedControl, CTRL_FRAME_READY, 0) !== 1) return;
    const slot = Atomics.load(this._sharedControl, CTRL_FRAME_INDEX);
    this._lastFramebuffer = this._sharedFrameViews[slot] || this._sharedFrameViews[0];
    this.frameReady = true;
  }

  renderFrame() {
    if (!this._lastFramebuffer) return;
    this.renderer.updateTexture(this._lastFramebuffer);
    this.renderer.draw();
  }

  /**
   * Refresh the beam crosshair overlay parameters.
   *
   * Deliberately not awaited by the render loop: this is a Worker round-trip,
   * and the result only feeds two shader uniforms that are read on the *next*
   * draw anyway. Awaiting it would put a round-trip in front of the draw for
   * the sake of a one-frame-fresher crosshair.
   */
  _refreshBeamOverlay() {
    if (this._beamQueryInFlight) return;
    this._beamQueryInFlight = true;
    this.wasmModule
      .batch([['_getBeamScanline'], ['_getBeamHPos']])
      .then(([scanline, hPos]) => {
        this.renderer.setParam("beamY", scanline < 192 ? (scanline + 0.5) / 192.0 : -1.0);
        this.renderer.setParam("beamX", hPos >= 25 ? (hPos - 25) / 40.0 : -1.0);
      })
      .catch(() => { /* transient RPC error — crosshair just stays put */ })
      .finally(() => { this._beamQueryInFlight = false; });
  }

  startRenderLoop() {
    this._renderFrameCount = 0;
    this._beamQueryInFlight = false;

    // This callback is SYNCHRONOUS by design. It used to await _isPaused() (and
    // the beam registers) before drawing, which meant the actual
    // texSubImage2D/drawArrays ran in a microtask after a Worker round-trip
    // rather than inside the rAF task — so a busy Worker, which is exactly the
    // Worker running the emulator, pushed frames past their vsync deadline.
    // Pause state now arrives pushed from the Worker (MSG_PAUSE_STATE) and
    // everything else here is either fire-and-forget or fired without awaiting.
    //
    // A throw would skip the reschedule below and kill the loop for good — a
    // black, unrecoverable screen from one transient error — so keep drawing.
    const render = () => {
      try {
        this._renderFrameCount++;
        this.pollSharedFrame();
        this.windowManager.updateAll(this.wasmModule);

        // Throttle disk LED updates to ~15fps (every 4th frame)
        if (this._renderFrameCount % 4 === 0) {
          this.diskManager.drivesWindowVisible = this.windowManager.isWindowVisible('disk-drives');
          this.diskManager.updateLEDs();
          if (this.hardDriveManager) {
            this.hardDriveManager.updateLEDs();
          }
        }

        const isPaused = this.running && this.wasmModule.isPaused;

        // Beam crosshair overlay — only when CPU debugger is open and CPU is paused
        if (isPaused && this.cpuDebuggerWindow && this.cpuDebuggerWindow.isVisible) {
          this._refreshBeamOverlay();
        } else {
          this.renderer.setParam("beamY", -1.0);
          this.renderer.setParam("beamX", -1.0);
        }

        if (this.frameReady) {
          this.frameReady = false;
          this.renderFrame();
        } else if (!this.running || isPaused) {
          if (isPaused) {
            // Fire-and-forget: the Worker answers with a framebuffer via
            // onFrameReady, which the next frame picks up. Redraw what we have.
            this.wasmModule._forceRenderFrame();
            this.renderFrame();
          } else {
            this.renderer.draw();
          }
        }
      } catch (error) {
        console.error("Render loop error:", error);
      }

      requestAnimationFrame(render);
    };

    requestAnimationFrame(render);
  }

  showLoading(show) {
    const loading = document.getElementById("loading");
    if (show) {
      loading.classList.remove("hidden");
    } else {
      loading.classList.add("hidden");
    }
  }

  /**
   * Clean up resources and remove event listeners.
   */
  async destroy() {
    if (this.running) {
      await this.stop();
    }

    if (this.stateManager) {
      this.stateManager.destroy();
      this.stateManager = null;
    }

    if (this.textSelection) {
      this.textSelection.destroy();
      this.textSelection = null;
    }

    if (this.windowManager) {
      this.windowManager.saveState();
      this.windowManager = null;
    }

    if (this.audioDriver) {
      this.audioDriver.stop();
      this.audioDriver = null;
    }

    if (this.themeManager) {
      this.themeManager.destroy();
      this.themeManager = null;
    }

    if (this.dockManager) {
      this.dockManager.destroy();
      this.dockManager = null;
    }

    if (this.agentManager) {
      this.agentManager.disconnect();
      this.agentManager = null;
    }

    if (this.gamepadHandler) {
      this.gamepadHandler.destroy();
      this.gamepadHandler = null;
    }
    
    if (this.wasmModule && this.wasmModule.destroy) {
      this.wasmModule.destroy();
    }

    this.renderer = null;
    this.diskManager = null;
    this.hardDriveManager = null;
    if (this.fileExplorer) {
      this.fileExplorer.destroy();
      this.fileExplorer = null;
    }
    this.inputHandler = null;
    this.reminderController = null;
    this.uiController = null;

    console.log("Apple //e Emulator destroyed");
  }
}

// Register service worker for offline support only when installed as PWA
const isInstalled = window.matchMedia("(display-mode: standalone)").matches || navigator.standalone;
if ("serviceWorker" in navigator && isInstalled) {
  window.addEventListener("load", () => {
    navigator.serviceWorker
      // updateViaCache: "none" forces the browser to revalidate sw.js on every
      // navigation instead of serving it from the HTTP cache. Without this,
      // Safari (especially as an installed PWA) can keep an old worker — and its
      // stale asset cache — for a long time after a redeploy, so bumping
      // CACHE_VERSION never takes effect.
      .register("/sw.js", { updateViaCache: "none" })
      .then((registration) => {
        console.log("Service Worker registered:", registration.scope);

        // Check for updates immediately on load
        registration.update().catch((err) => {
          console.log("Service Worker update check failed:", err);
        });

        // Handle new service worker installation
        registration.addEventListener("updatefound", () => {
          const newWorker = registration.installing;
          if (newWorker) {
            newWorker.addEventListener("statechange", () => {
              if (newWorker.state === "installed") {
                if (navigator.serviceWorker.controller) {
                  // New version available - show badge on Help button
                  console.log("New version available - badge shown on Help button");
                  const helpBtn = document.getElementById("btn-help-menu");
                  if (helpBtn) {
                    helpBtn.classList.add("update-available");
                  }
                } else {
                  // First install - no reload needed
                  console.log("App cached for offline use");
                }
              }
            });
          }
        });
      })
      .catch((error) => {
        console.log("Service Worker registration failed:", error);
      });
  });
}

// Initialize when DOM is ready
document.addEventListener("DOMContentLoaded", () => {
  // Display version in header
  const versionEl = document.getElementById("app-version");
  if (versionEl) {
    versionEl.textContent = `v${VERSION}`;
  }

  const emulator = new AppleIIeEmulator();
  emulator.init();

  // Make emulator accessible globally for debugging
  window.a2e = emulator;

  // Helper to toggle Mockingboard debug logging from console
  window.mbDebug = (enabled = true) => {
    if (emulator.wasmModule && emulator.wasmModule._setMockingboardDebugLogging) {
      emulator.wasmModule._setMockingboardDebugLogging(enabled);
      console.log(`Mockingboard debug logging ${enabled ? "enabled" : "disabled"}`);
    } else {
      console.log("Mockingboard debug logging not available");
    }
  };
});
