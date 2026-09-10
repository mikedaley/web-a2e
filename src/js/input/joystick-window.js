/*
 * joystick-window.js - Virtual joystick and paddle configuration window
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import {
  GAME_PORT_APPLE,
  GAME_PORT_CODE,
  GAME_PORT_DEVICES,
  GAME_PORT_JOYPORT,
  JOYPORT_STICKS,
  SWITCH,
  describeSwitches,
  isGamePortDevice,
  switchesFromDirections,
  loadStoredGamePort,
  storeGamePort,
} from "./game-port.js";

// Soft switch state bits for the Apple buttons, matching soft-switch-window.js.
const BTN0_BIT = 24; // $C061 Open Apple
const BTN1_BIT = 25; // $C062 Closed Apple

// Fast enough to feel instant on a key press without polling every frame.
const BUTTON_POLL_MS = 50;

// The four directions of a digital stick, in the order the D-pad draws them.
const JOYPORT_DIRECTIONS = [
  { bit: SWITCH.UP, name: "up", glyph: "\u25B2", area: "u" },
  { bit: SWITCH.LEFT, name: "left", glyph: "\u25C0", area: "l" },
  { bit: SWITCH.RIGHT, name: "right", glyph: "\u25B6", area: "r" },
  { bit: SWITCH.DOWN, name: "down", glyph: "\u25BC", area: "d" },
];

/**
 * Extract the two Apple button states from a soft switch word.
 *
 * @param {number} state  Low 32 bits from _getSoftSwitchState()
 * @returns {{button0: boolean, button1: boolean}}
 */
export function buttonsFromSoftSwitchState(state) {
  return {
    button0: (state & (1 << BTN0_BIT)) !== 0,
    button1: (state & (1 << BTN1_BIT)) !== 0,
  };
}

export class JoystickWindow extends BaseWindow {
  constructor(wasmModule) {
    super({
      id: "joystick",
      title: "Joystick",
      defaultWidth: 260,
      defaultHeight: 540,
      minWidth: 260,
      minHeight: 540,
      resizeDirections: [],
    });
    this.wasmModule = wasmModule;
    this.isDraggingKnob = false;
    this.knobX = 0.5; // 0-1 range, 0.5 = center
    this.knobY = 0.5;
    this.button0Pressed = false; // mouse is holding this button down
    this.button1Pressed = false;
    this.buttonPollTimer = null;
    this.gamepadHandler = null;
    this.cursorKeysEnabled = localStorage.getItem("joystick-cursor-keys") === "true";
    this.cursorKeysState = { left: false, right: false, up: false, down: false };
    this.onCursorKeysChanged = null; // callback when toggle changes

    // What is plugged into the game I/O connector. The Joyport drives the same
    // three pushbutton inputs the Apple keys do, and drives them inverted, so
    // the two devices replace each other rather than coexisting.
    this.gamePort = loadStoredGamePort();
    this.joyportSticks = new Array(JOYPORT_STICKS).fill(0);
    // Which switch the mouse is currently holding, so leaving the button
    // releases exactly that one.
    this.joyportHeld = null;
    this.onGamePortChanged = null;
  }

  /**
   * Reflect the emulator's real button state on the PB0/PB1 LEDs.
   *
   * The LEDs used to be driven purely by mouse events on these buttons, so
   * pressing Open or Closed Apple on the keyboard — or a gamepad button — lit
   * nothing. Reading $C061/$C062 back means the indicators show the actual
   * hardware state whatever pressed it.
   *
   * Bits 24 and 25 of the soft switch state are BTN0 and BTN1, the same source
   * the Soft Switch Monitor reads, so this needs no new WASM export.
   */
  async updateButtonIndicators() {
    if (!this.isVisible || !this.wasmModule) return;

    const { button0, button1 } = buttonsFromSoftSwitchState(
      await this.wasmModule._getSoftSwitchState(),
    );

    this.button0Element?.classList.toggle("pressed", button0);
    this.button1Element?.classList.toggle("pressed", button1);
  }

  startButtonPolling() {
    if (this.buttonPollTimer) return;
    this.buttonPollTimer = setInterval(() => this.updateButtonIndicators(), BUTTON_POLL_MS);
  }

  stopButtonPolling() {
    if (!this.buttonPollTimer) return;
    clearInterval(this.buttonPollTimer);
    this.buttonPollTimer = null;
  }

  show() {
    super.show();
    if (!this.isJoyport()) this.startButtonPolling();
  }

  hide() {
    // Nothing to poll for while hidden, and a held button would otherwise stay
    // lit behind the scenes.
    this.stopButtonPolling();
    super.hide();
  }

  renderContent() {
    const options = GAME_PORT_DEVICES.map(
      (d) =>
        `<option value="${d.id}" title="${d.description}">${d.label}</option>`,
    ).join("");

    return `
      <div class="joystick-container">
        <div class="game-port-row">
          <span class="game-port-label">Game port</span>
          <select class="settings-select game-port-select">${options}</select>
        </div>
        <div class="joystick-apple-panel">
        <div class="joystick-area-wrapper">
          <span class="joystick-axis-label joystick-axis-l">L</span>
          <span class="joystick-axis-label joystick-axis-r">R</span>
          <span class="joystick-axis-label joystick-axis-u">U</span>
          <span class="joystick-axis-label joystick-axis-d">D</span>
          <div class="joystick-area">
            <div class="joystick-ring joystick-ring-75"></div>
            <div class="joystick-ring joystick-ring-50"></div>
            <div class="joystick-ring joystick-ring-25"></div>
            <div class="joystick-crosshair"></div>
            <div class="joystick-home-dot"></div>
            <div class="joystick-knob"><div class="joystick-knob-highlight"></div></div>
          </div>
        </div>
        <div class="joystick-gauges">
          <div class="joystick-gauge-row">
            <span class="joystick-gauge-label">PDL0</span>
            <div class="joystick-gauge-track joystick-gauge-x-track">
              <div class="joystick-gauge-fill joystick-gauge-x-fill"></div>
            </div>
            <span class="joystick-gauge-value joystick-x-value">128</span>
          </div>
          <div class="joystick-gauge-row">
            <span class="joystick-gauge-label">PDL1</span>
            <div class="joystick-gauge-track joystick-gauge-y-track">
              <div class="joystick-gauge-fill joystick-gauge-y-fill"></div>
            </div>
            <span class="joystick-gauge-value joystick-y-value">128</span>
          </div>
        </div>
        <div class="joystick-buttons">
          <button class="joystick-btn joystick-btn-0" data-button="0">
            <span class="joystick-btn-led"></span>
            <span class="joystick-btn-label">PB0</span>
          </button>
          <button class="joystick-btn joystick-btn-1" data-button="1">
            <span class="joystick-btn-led"></span>
            <span class="joystick-btn-label">PB1</span>
          </button>
        </div>
        <div class="joystick-center-btn-container">
          <button class="joystick-center-btn" title="Center (reset to 128,128)">&#x2316;</button>
        </div>
        </div>
        <div class="joyport-panel">
          ${this.renderJoyportSticks()}
        </div>
        <div class="gamepad-section">
          <div class="gamepad-status-row">
            <label class="gamepad-toggle-label">
              <div class="gamepad-toggle-switch">
                <input type="checkbox" class="gamepad-toggle" />
                <span class="gamepad-toggle-slider"></span>
              </div>
              <span class="gamepad-toggle-text">Gamepad</span>
            </label>
            <span class="gamepad-status"><span class="gamepad-status-dot"></span><span class="gamepad-status-text">No controller</span></span>
          </div>
          <div class="gamepad-deadzone-row">
            <span class="gamepad-deadzone-label">Deadzone</span>
            <input type="range" class="gamepad-deadzone-slider" min="0" max="50" step="1" value="10" />
            <span class="gamepad-deadzone-value">0.10</span>
          </div>
        </div>
      </div>
    `;
  }

  /**
   * One card per Joyport stick: a D-pad of the four switches and a fire
   * button, each holdable with the mouse and each lit by whatever is driving
   * the stick — mouse, gamepad or cursor keys.
   */
  renderJoyportSticks() {
    let html = "";
    for (let stick = 0; stick < JOYPORT_STICKS; stick++) {
      const pad = JOYPORT_DIRECTIONS.map(
        (dir) => `
            <button class="joyport-dir joyport-dir-${dir.area}"
                    data-stick="${stick}" data-switch="${dir.bit}"
                    title="${dir.name}">${dir.glyph}</button>`,
      ).join("");

      html += `
        <div class="joyport-stick" data-stick="${stick}">
          <div class="joyport-stick-header">
            <span class="joyport-stick-name">Stick ${stick + 1}</span>
            <span class="joyport-stick-readout" data-stick="${stick}">Centred</span>
          </div>
          <div class="joyport-stick-body">
            <div class="joyport-dpad">${pad}
              <span class="joyport-dpad-hub"></span>
            </div>
            <button class="joyport-fire" data-stick="${stick}" data-switch="${SWITCH.FIRE}">
              <span class="joystick-btn-led"></span>
              <span class="joystick-btn-label">FIRE</span>
            </button>
          </div>
        </div>`;
    }
    return html;
  }

  onContentRendered() {
    this.joystickArea = this.contentElement.querySelector(".joystick-area");
    this.knobElement = this.contentElement.querySelector(".joystick-knob");
    this.xValueSpan = this.contentElement.querySelector(".joystick-x-value");
    this.yValueSpan = this.contentElement.querySelector(".joystick-y-value");
    this.xGaugeFill = this.contentElement.querySelector(
      ".joystick-gauge-x-fill",
    );
    this.yGaugeFill = this.contentElement.querySelector(
      ".joystick-gauge-y-fill",
    );
    this.button0Element = this.contentElement.querySelector(".joystick-btn-0");
    this.button1Element = this.contentElement.querySelector(".joystick-btn-1");
    this.centerBtn = this.contentElement.querySelector(".joystick-center-btn");

    // Gamepad UI elements
    this.gamepadToggle = this.contentElement.querySelector(".gamepad-toggle");
    this.gamepadStatusText = this.contentElement.querySelector(
      ".gamepad-status-text",
    );
    this.gamepadStatusDot = this.contentElement.querySelector(
      ".gamepad-status-dot",
    );
    this.gamepadDeadzoneSlider = this.contentElement.querySelector(
      ".gamepad-deadzone-slider",
    );
    this.gamepadDeadzoneValue = this.contentElement.querySelector(
      ".gamepad-deadzone-value",
    );

    this.applePanel = this.contentElement.querySelector(".joystick-apple-panel");
    this.joyportPanel = this.contentElement.querySelector(".joyport-panel");
    this.gamePortSelect = this.contentElement.querySelector(".game-port-select");

    this.setupJoystickEventListeners();
    this.setupJoyportEventListeners();
    this.setupGamePortSelector();
    this.setupGamepadEventListeners();
    this.updateKnobPosition();
    this.updatePaddleValues();

    // Watch for resize and update knob position
    this.resizeObserver = new ResizeObserver(() => {
      this.updateKnobPosition();
    });
    this.resizeObserver.observe(this.joystickArea);
  }

  setupJoystickEventListeners() {
    // Knob dragging
    this.knobElement.addEventListener("mousedown", (e) => {
      this.isDraggingKnob = true;
      this.knobElement.classList.add("dragging");
      e.preventDefault();
      e.stopPropagation();
    });

    // Also start drag when clicking anywhere in the joystick area
    this.joystickArea.addEventListener("mousedown", (e) => {
      if (
        e.target === this.joystickArea ||
        e.target.classList.contains("joystick-crosshair") ||
        e.target.classList.contains("joystick-ring") ||
        e.target.classList.contains("joystick-home-dot")
      ) {
        this.isDraggingKnob = true;
        this.knobElement.classList.add("dragging");
        this.updateKnobFromMouse(e);
        e.preventDefault();
      }
    });

    document.addEventListener("mousemove", (e) => {
      if (this.isDraggingKnob) {
        this.updateKnobFromMouse(e);
      }
    });

    document.addEventListener("mouseup", () => {
      if (this.isDraggingKnob) {
        this.isDraggingKnob = false;
        this.knobElement.classList.remove("dragging");
        // Snap back to center when released
        this.knobX = 0.5;
        this.knobY = 0.5;
        this.updateKnobPosition();
        this.updatePaddleValues();
      }
    });

    // Button handling with mousedown/mouseup for proper hold behavior
    this.button0Element.addEventListener("mousedown", () => {
      this.button0Pressed = true;
      this.button0Element.classList.add("pressed");
      this.wasmModule._setButton(0, true);
    });

    this.button0Element.addEventListener("mouseup", () => {
      this.button0Pressed = false;
      this.button0Element.classList.remove("pressed");
      this.wasmModule._setButton(0, false);
    });

    this.button0Element.addEventListener("mouseleave", () => {
      if (this.button0Pressed) {
        this.button0Pressed = false;
        this.button0Element.classList.remove("pressed");
        this.wasmModule._setButton(0, false);
      }
    });

    this.button1Element.addEventListener("mousedown", () => {
      this.button1Pressed = true;
      this.button1Element.classList.add("pressed");
      this.wasmModule._setButton(1, true);
    });

    this.button1Element.addEventListener("mouseup", () => {
      this.button1Pressed = false;
      this.button1Element.classList.remove("pressed");
      this.wasmModule._setButton(1, false);
    });

    this.button1Element.addEventListener("mouseleave", () => {
      if (this.button1Pressed) {
        this.button1Pressed = false;
        this.button1Element.classList.remove("pressed");
        this.wasmModule._setButton(1, false);
      }
    });

    // Center button
    this.centerBtn.addEventListener("click", () => {
      this.knobX = 0.5;
      this.knobY = 0.5;
      this.updateKnobPosition();
      this.updatePaddleValues();
    });
  }

  // ==========================================================================
  // Game port device
  // ==========================================================================

  setupGamePortSelector() {
    if (!this.gamePortSelect) return;
    this.gamePortSelect.value = this.gamePort;
    this.gamePortSelect.addEventListener("change", () => {
      this.setGamePort(this.gamePortSelect.value);
    });
    this.reflectGamePort();
  }

  /**
   * Choose what is on the game connector and tell the core.
   *
   * Kept separate from `reflectGamePort` so startup can push the remembered
   * choice into a freshly built core — after a machine switch, say — without
   * rewriting storage or re-firing the change callback.
   */
  setGamePort(device, { persist = true } = {}) {
    const next = isGamePortDevice(device) ? device : GAME_PORT_APPLE;
    this.gamePort = next;
    if (persist) storeGamePort(next);
    if (this.gamePortSelect) this.gamePortSelect.value = next;

    // Nothing held on the old device is held on the new one.
    this.joyportHeld = null;
    for (let stick = 0; stick < JOYPORT_STICKS; stick++) {
      this.joyportSticks[stick] = 0;
      this.paintJoyportStick(stick);
    }

    this.applyGamePort();
    this.reflectGamePort();
    this.gamepadHandler?.setGamePort?.(next);
    if (this.onGamePortChanged) this.onGamePortChanged(next);
  }

  /** Push the selected device into WASM. */
  applyGamePort() {
    this.wasmModule?._setGamePortDevice?.(GAME_PORT_CODE[this.gamePort] ?? 0);
  }

  isJoyport() {
    return this.gamePort === GAME_PORT_JOYPORT;
  }

  /** Show the panel that belongs to the selected device. */
  reflectGamePort() {
    const joyport = this.isJoyport();
    if (this.applePanel) this.applePanel.hidden = joyport;
    if (this.joyportPanel) this.joyportPanel.hidden = !joyport;
    // The PB0/PB1 LEDs belong to the Apple panel, and in Joyport mode those
    // lines read inverted, so there is nothing worth polling for.
    if (joyport) this.stopButtonPolling();
    else if (this.isVisible) this.startButtonPolling();
  }

  // ==========================================================================
  // Joyport sticks
  // ==========================================================================

  setupJoyportEventListeners() {
    const switches = this.contentElement.querySelectorAll("[data-switch]");
    for (const el of switches) {
      const stick = Number(el.dataset.stick);
      const bit = Number(el.dataset.switch);

      el.addEventListener("mousedown", (e) => {
        e.preventDefault();
        this.joyportHeld = { stick, bit };
        this.holdJoyportSwitch(stick, bit, true);
      });
      el.addEventListener("mouseup", () => this.releaseHeldJoyportSwitch());
      el.addEventListener("mouseleave", () => this.releaseHeldJoyportSwitch());
    }
  }

  releaseHeldJoyportSwitch() {
    if (!this.joyportHeld) return;
    const { stick, bit } = this.joyportHeld;
    this.joyportHeld = null;
    this.holdJoyportSwitch(stick, bit, false);
  }

  /** Close or open one switch on one stick and push the whole mask across. */
  holdJoyportSwitch(stick, bit, closed) {
    if (stick < 0 || stick >= JOYPORT_STICKS) return;
    const mask = closed
      ? this.joyportSticks[stick] | bit
      : this.joyportSticks[stick] & ~bit;
    this.setJoyportStick(stick, mask, { push: true });
  }

  /**
   * Show a stick's switch state, and optionally send it.
   *
   * The gamepad handler has already written to WASM by the time it calls here,
   * so it leaves `push` alone; the mouse and the cursor keys need the write.
   */
  setJoyportStick(stick, mask, { push = false } = {}) {
    if (stick < 0 || stick >= JOYPORT_STICKS) return;
    this.joyportSticks[stick] = mask;
    if (push) this.wasmModule?._setJoyportStick?.(stick, mask);
    this.paintJoyportStick(stick);
  }

  paintJoyportStick(stick) {
    if (!this.contentElement) return;
    const mask = this.joyportSticks[stick];
    const controls = this.contentElement.querySelectorAll(
      `[data-switch][data-stick="${stick}"]`,
    );
    for (const el of controls) {
      el.classList.toggle("pressed", (mask & Number(el.dataset.switch)) !== 0);
    }
    const readout = this.contentElement.querySelector(
      `.joyport-stick-readout[data-stick="${stick}"]`,
    );
    if (readout) readout.textContent = describeSwitches(mask);
  }

  updateKnobFromMouse(e) {
    const rect = this.joystickArea.getBoundingClientRect();
    const centerX = rect.left + rect.width / 2;
    const centerY = rect.top + rect.height / 2;
    const radius = Math.min(rect.width, rect.height) / 2;

    // Calculate offset from center
    let dx = e.clientX - centerX;
    let dy = e.clientY - centerY;

    // Clamp to circle
    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist > radius) {
      dx = (dx / dist) * radius;
      dy = (dy / dist) * radius;
    }

    // Convert to 0-1 range
    this.knobX = Math.max(0, Math.min(1, 0.5 + dx / (radius * 2)));
    this.knobY = Math.max(0, Math.min(1, 0.5 + dy / (radius * 2)));

    this.updateKnobPosition();
    this.updatePaddleValues();
  }

  updateKnobPosition() {
    if (!this.joystickArea || !this.knobElement) return;

    const rect = this.joystickArea.getBoundingClientRect();
    const knobSize = 28;
    const areaSize = Math.min(rect.width, rect.height);
    const radius = areaSize / 2;
    const offsetX = (rect.width - areaSize) / 2;
    const offsetY = (rect.height - areaSize) / 2;

    // Convert 0-1 to position within the circular area
    const cx = offsetX + radius + (this.knobX - 0.5) * areaSize;
    const cy = offsetY + radius + (this.knobY - 0.5) * areaSize;

    this.knobElement.style.left = `${cx - knobSize / 2}px`;
    this.knobElement.style.top = `${cy - knobSize / 2}px`;
  }

  updatePaddleValues() {
    // Convert 0-1 range to 0-255 for paddle values
    const paddleX = Math.round(this.knobX * 255);
    const paddleY = Math.round(this.knobY * 255);

    // Update display
    if (this.xValueSpan) this.xValueSpan.textContent = paddleX.toString();
    if (this.yValueSpan) this.yValueSpan.textContent = paddleY.toString();

    // Update gauge bars
    const pctX = (paddleX / 255) * 100;
    const pctY = (paddleY / 255) * 100;
    if (this.xGaugeFill) this.xGaugeFill.style.width = `${pctX}%`;
    if (this.yGaugeFill) this.yGaugeFill.style.width = `${pctY}%`;

    // Send to emulator
    if (this.wasmModule._setPaddleValue) {
      this.wasmModule._setPaddleValue(0, paddleX);
      this.wasmModule._setPaddleValue(1, paddleY);
    }
  }

  setupGamepadEventListeners() {
    if (this.gamepadToggle) {
      // Restore persisted state
      const enabled = localStorage.getItem("gamepad-enabled") !== "false";
      this.gamepadToggle.checked = enabled;

      this.gamepadToggle.addEventListener("change", () => {
        if (this.gamepadHandler) {
          this.gamepadHandler.setEnabled(this.gamepadToggle.checked);
        }
      });
    }

    if (this.gamepadDeadzoneSlider) {
      // Restore persisted deadzone
      const dz = parseFloat(localStorage.getItem("gamepad-deadzone")) || 0.1;
      this.gamepadDeadzoneSlider.value = Math.round(dz * 100);
      this.gamepadDeadzoneValue.textContent = dz.toFixed(2);

      this.gamepadDeadzoneSlider.addEventListener("input", () => {
        const value = this.gamepadDeadzoneSlider.value / 100;
        this.gamepadDeadzoneValue.textContent = value.toFixed(2);
        if (this.gamepadHandler) {
          this.gamepadHandler.setDeadzone(value);
        }
      });
    }
  }

  setCursorKeysEnabled(enabled) {
    this.cursorKeysEnabled = enabled;
    localStorage.setItem("joystick-cursor-keys", enabled);
    if (!enabled) {
      this.cursorKeysState = { left: false, right: false, up: false, down: false };
      this.updateCursorKeysPaddle();
    }
    if (this.onCursorKeysChanged) this.onCursorKeysChanged(enabled);
  }

  /**
   * Handle a cursor key press/release for joystick emulation.
   * Returns true if the key was consumed, false otherwise.
   */
  handleCursorKey(keyCode, pressed) {
    if (!this.cursorKeysEnabled) return false;

    switch (keyCode) {
      case 37: this.cursorKeysState.left = pressed; break;   // Left
      case 39: this.cursorKeysState.right = pressed; break;  // Right
      case 38: this.cursorKeysState.up = pressed; break;     // Up
      case 40: this.cursorKeysState.down = pressed; break;   // Down
      default: return false;
    }

    this.updateCursorKeysPaddle();
    return true;
  }

  updateCursorKeysPaddle() {
    const s = this.cursorKeysState;
    if (this.isJoyport()) {
      // The Joyport's switches are exactly what the cursor keys already are,
      // so they drive stick 1 directly rather than through a paddle position.
      this.setJoyportStick(
        0,
        switchesFromDirections({
          up: s.up,
          down: s.down,
          left: s.left,
          right: s.right,
          fire: (this.joyportSticks[0] & SWITCH.FIRE) !== 0,
        }),
        { push: true },
      );
      return;
    }

    // Map to 0, 0.5, or 1 per axis
    let x = 0.5;
    let y = 0.5;
    if (s.left && !s.right) x = 0;
    else if (s.right && !s.left) x = 1;
    if (s.up && !s.down) y = 0;
    else if (s.down && !s.up) y = 1;

    this.knobX = x;
    this.knobY = y;
    this.updateKnobPosition();
    this.updatePaddleValues();
  }

  /**
   * Called by GamepadHandler to move the knob from external input.
   * @param {number} normX - 0..1 position
   * @param {number} normY - 0..1 position
   */
  setExternalPosition(normX, normY) {
    this.knobX = normX;
    this.knobY = normY;
    this.updateKnobPosition();

    // Update value display and gauge bars (paddle values set by GamepadHandler)
    const paddleX = Math.round(normX * 255);
    const paddleY = Math.round(normY * 255);
    if (this.xValueSpan) this.xValueSpan.textContent = paddleX.toString();
    if (this.yValueSpan) this.yValueSpan.textContent = paddleY.toString();

    const pctX = (paddleX / 255) * 100;
    const pctY = (paddleY / 255) * 100;
    if (this.xGaugeFill) this.xGaugeFill.style.width = `${pctX}%`;
    if (this.yGaugeFill) this.yGaugeFill.style.width = `${pctY}%`;
  }

  /**
   * Called by GamepadHandler to reflect physical button state in the UI.
   * @param {number} button - 0 or 1
   * @param {boolean} pressed
   */
  setExternalButton(button, pressed) {
    const el = button === 0 ? this.button0Element : this.button1Element;
    if (!el) return;
    if (pressed) {
      el.classList.add("pressed");
    } else {
      el.classList.remove("pressed");
    }
  }

  /**
   * Called by GamepadHandler when connection state changes.
   *
   * The Joyport takes two sticks, so this is a list rather than one name: with
   * two pads connected it says so instead of naming only the first.
   *
   * @param {string[]|string|null} names - Connected controller names
   * @param {boolean} enabled - Whether gamepad input is enabled
   */
  updateGamepadStatus(names, enabled) {
    const list = Array.isArray(names) ? names : names ? [names] : [];
    if (this.gamepadStatusText) {
      let text = "No controller";
      if (list.length === 1) text = list[0].substring(0, 30);
      else if (list.length > 1) text = `${list.length} controllers`;
      this.gamepadStatusText.textContent = text;
    }
    if (this.gamepadStatusDot) {
      this.gamepadStatusDot.classList.toggle("connected", list.length > 0);
    }
    if (this.gamepadToggle) {
      this.gamepadToggle.checked = enabled;
    }
  }

  update(wasmModule) {
    // No periodic update needed - values are set directly on drag
  }

  getState() {
    const baseState = super.getState();
    return {
      ...baseState,
      knobX: this.knobX,
      knobY: this.knobY,
      cursorKeysEnabled: this.cursorKeysEnabled,
      gamePort: this.gamePort,
    };
  }

  restoreState(state) {
    super.restoreState(state);
    if (state.knobX !== undefined) this.knobX = state.knobX;
    if (state.knobY !== undefined) this.knobY = state.knobY;
    if (state.cursorKeysEnabled !== undefined) {
      this.cursorKeysEnabled = state.cursorKeysEnabled;
      if (this.onCursorKeysChanged) this.onCursorKeysChanged(this.cursorKeysEnabled);
    }
    if (isGamePortDevice(state.gamePort)) {
      this.setGamePort(state.gamePort);
    }
    // Update visuals after restoring
    if (this.knobElement) {
      this.updateKnobPosition();
      this.updatePaddleValues();
    }
  }
}
