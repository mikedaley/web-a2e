/*
 * gamepad-handler.js - Physical game controller support via Gamepad API
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import {
  DIGITAL_THRESHOLD,
  GAME_PORT_JOYPORT,
  JOYPORT_STICKS,
  switchesFromDirections,
} from "./game-port.js";

const STORAGE_KEY_ENABLED = "gamepad-enabled";
const STORAGE_KEY_DEADZONE = "gamepad-deadzone";
const DEFAULT_DEADZONE = 0.1;

// Standard-mapping D-pad button indices.
const DPAD_UP = 12;
const DPAD_DOWN = 13;
const DPAD_LEFT = 14;
const DPAD_RIGHT = 15;

export class GamepadHandler {
  constructor(wasmModule, joystickWindow, gamePort = null) {
    this.wasmModule = wasmModule;
    this.joystickWindow = joystickWindow;
    this.enabled = localStorage.getItem(STORAGE_KEY_ENABLED) !== "false";
    this.deadzone = parseFloat(localStorage.getItem(STORAGE_KEY_DEADZONE)) || DEFAULT_DEADZONE;
    // Connected pads in connection order. The Joyport takes two sticks, so a
    // single index is no longer enough; index 0 is stick 1, index 1 is stick 2.
    this.gamepadIndices = [];
    this.gamePort = gamePort;
    this.rafId = null;

    // Track previous button state to detect edges
    this.prevButtons = [false, false];
    // Last mask pushed per Joyport stick, so an idle stick costs no RPCs.
    this.prevSticks = new Array(JOYPORT_STICKS).fill(0);

    this._onConnected = this._onConnected.bind(this);
    this._onDisconnected = this._onDisconnected.bind(this);
    this._poll = this._poll.bind(this);

    window.addEventListener("gamepadconnected", this._onConnected);
    window.addEventListener("gamepaddisconnected", this._onDisconnected);

    // Check if a gamepad is already connected
    const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (let i = 0; i < gamepads.length; i++) {
      if (gamepads[i]) this.gamepadIndices.push(gamepads[i].index);
    }
    if (this.gamepadIndices.length) {
      this._notifyWindow();
      if (this.enabled) this._startPolling();
    }
  }

  /** Called when the user changes what is plugged into the game port. */
  setGamePort(device) {
    this.gamePort = device;
    this._releaseAll();
    this._notifyWindow();
  }

  _isJoyport() {
    return this.gamePort === GAME_PORT_JOYPORT;
  }

  _onConnected(e) {
    if (!this.gamepadIndices.includes(e.gamepad.index)) {
      this.gamepadIndices.push(e.gamepad.index);
    }
    this._notifyWindow();
    if (this.enabled) {
      this._startPolling();
    }
  }

  _onDisconnected(e) {
    const at = this.gamepadIndices.indexOf(e.gamepad.index);
    if (at !== -1) {
      this.gamepadIndices.splice(at, 1);
      // A pad unplugged mid-game leaves whatever it was holding held.
      this._releaseAll();
      this._notifyWindow();
    }
  }

  _notifyWindow() {
    if (this.joystickWindow && this.joystickWindow.updateGamepadStatus) {
      const names = this._getGamepads().map((gp) => gp.id);
      this.joystickWindow.updateGamepadStatus(names, this.enabled);
    }
  }

  /** Connected pads, in stick order. */
  _getGamepads() {
    const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
    return this.gamepadIndices
      .map((index) => gamepads[index])
      .filter((gp) => !!gp);
  }

  _getGamepad(slot = 0) {
    return this._getGamepads()[slot] || null;
  }

  _applyDeadzone(value) {
    if (Math.abs(value) < this.deadzone) return 0;
    // Rescale so the range beyond deadzone maps to 0..1
    const sign = value > 0 ? 1 : -1;
    return sign * (Math.abs(value) - this.deadzone) / (1 - this.deadzone);
  }

  /** Let go of everything either device is holding. */
  _releaseAll() {
    for (let i = 0; i < JOYPORT_STICKS; i++) {
      if (this.prevSticks[i] !== 0) {
        this.prevSticks[i] = 0;
        this.wasmModule?._setJoyportStick?.(i, 0);
      }
      this.joystickWindow?.setJoyportStick?.(i, 0);
    }
    for (let i = 0; i < 2; i++) {
      if (this.prevButtons[i]) {
        this.prevButtons[i] = false;
        this.wasmModule?._setButton?.(i, false);
        this.joystickWindow?.setExternalButton?.(i, false);
      }
    }
  }

  _poll() {
    const pads = this._getGamepads();
    if (!pads.length) {
      this.rafId = null;
      return;
    }
    if (this.enabled) {
      if (this._isJoyport()) {
        this._pollJoyport(pads);
      } else {
        this._pollAppleJoystick(pads[0]);
      }
    }

    this.rafId = requestAnimationFrame(this._poll);
  }

  _pollAppleJoystick(gp) {
    // Left stick axes (0 = X, 1 = Y), range -1..1
    const rawX = gp.axes[0] || 0;
    const rawY = gp.axes[1] || 0;
    const adjX = this._applyDeadzone(rawX);
    const adjY = this._applyDeadzone(rawY);

    // Map -1..1 to 0..1
    const normX = (adjX + 1) / 2;
    const normY = (adjY + 1) / 2;

    // Map to 0-255 for paddle values
    const paddleX = Math.round(normX * 255);
    const paddleY = Math.round(normY * 255);

    if (this.wasmModule._setPaddleValue) {
      this.wasmModule._setPaddleValue(0, paddleX);
      this.wasmModule._setPaddleValue(1, paddleY);
    }

    // Update the joystick window knob to reflect controller position
    if (this.joystickWindow) {
      this.joystickWindow.setExternalPosition(normX, normY);
    }

    // Buttons 0 and 1 (A/B on standard controllers)
    for (let i = 0; i < 2; i++) {
      const pressed = gp.buttons[i] ? gp.buttons[i].pressed : false;
      if (pressed !== this.prevButtons[i]) {
        this.prevButtons[i] = pressed;
        if (this.wasmModule._setButton) {
          this.wasmModule._setButton(i, pressed);
        }
        if (this.joystickWindow) {
          this.joystickWindow.setExternalButton(i, pressed);
        }
      }
    }
  }

  /**
   * A CX40 has four switches and one button, so both the D-pad and the left
   * stick drive the same five lines — a thumbstick pushed past the threshold
   * closes the same switch the D-pad does, and either will do.
   *
   * With one pad connected it drives both sticks. A single-player game that
   * happens to read joystick 2 then still plays, which is worth more than the
   * dead second stick the alternative gives.
   */
  _pollJoyport(pads) {
    const mirror = pads.length === 1;
    for (let stick = 0; stick < JOYPORT_STICKS; stick++) {
      const gp = mirror ? pads[0] : pads[stick];
      const mask = gp ? this._maskFromGamepad(gp) : 0;
      if (mask !== this.prevSticks[stick]) {
        this.prevSticks[stick] = mask;
        this.wasmModule?._setJoyportStick?.(stick, mask);
        this.joystickWindow?.setJoyportStick?.(stick, mask);
      }
    }
  }

  _maskFromGamepad(gp) {
    const pressed = (index) =>
      gp.buttons[index] ? gp.buttons[index].pressed : false;
    const x = this._applyDeadzone(gp.axes[0] || 0);
    const y = this._applyDeadzone(gp.axes[1] || 0);
    const threshold = DIGITAL_THRESHOLD;

    return switchesFromDirections({
      up: pressed(DPAD_UP) || y <= -threshold,
      down: pressed(DPAD_DOWN) || y >= threshold,
      left: pressed(DPAD_LEFT) || x <= -threshold,
      right: pressed(DPAD_RIGHT) || x >= threshold,
      fire: pressed(0) || pressed(1),
    });
  }

  _startPolling() {
    if (this.rafId !== null) return;
    this.rafId = requestAnimationFrame(this._poll);
  }

  _stopPolling() {
    if (this.rafId !== null) {
      cancelAnimationFrame(this.rafId);
      this.rafId = null;
    }
  }

  start() {
    if (this.enabled && this.gamepadIndices.length) {
      this._startPolling();
    }
  }

  stop() {
    this._stopPolling();
  }

  setEnabled(enabled) {
    this.enabled = enabled;
    localStorage.setItem(STORAGE_KEY_ENABLED, enabled);
    this._notifyWindow();
    if (enabled && this.gamepadIndices.length) {
      this._startPolling();
    } else if (!enabled) {
      this._releaseAll();
      this._stopPolling();
    }
  }

  setDeadzone(value) {
    this.deadzone = Math.max(0, Math.min(0.5, value));
    localStorage.setItem(STORAGE_KEY_DEADZONE, this.deadzone);
  }

  isConnected() {
    return this._getGamepads().length > 0;
  }

  /** How many pads are connected — the Joyport panel shows one row per stick. */
  connectedCount() {
    return this._getGamepads().length;
  }

  destroy() {
    this.stop();
    window.removeEventListener("gamepadconnected", this._onConnected);
    window.removeEventListener("gamepaddisconnected", this._onDisconnected);
  }
}
