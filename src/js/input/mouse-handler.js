/*
 * mouse-handler.js - Mouse input handling via Pointer Lock API
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/**
 * MouseHandler - Browser mouse capture for Apple Mouse Interface Card
 *
 * Uses the Pointer Lock API to capture mouse movement and send deltas
 * to the WASM emulator. Click on the canvas to engage pointer lock;
 * press Escape to release it (browser default behavior).
 */
export class MouseHandler {
  constructor(wasmModule) {
    this.wasmModule = wasmModule;
    this.canvas = null;
    this.enabled = false;
    this.locked = false;
    /** Called with the new lock state whenever capture is taken or released. */
    this.onLockChanged = null;
    /** Called with the new enabled state when a mouse appears or goes away. */
    this.onEnabledChanged = null;

    this._onMouseMove = this._onMouseMove.bind(this);
    this._onMouseDown = this._onMouseDown.bind(this);
    this._onMouseUp = this._onMouseUp.bind(this);
    this._onPointerLockChange = this._onPointerLockChange.bind(this);
    this._onCanvasClick = this._onCanvasClick.bind(this);
  }

  init() {
    this.canvas = document.getElementById("screen");
    if (!this.canvas) return;

    document.addEventListener("pointerlockchange", this._onPointerLockChange);

    this.canvas.addEventListener("click", this._onCanvasClick);
  }

  enable() {
    const changed = !this.enabled;
    this.enabled = true;
    if (changed) this.onEnabledChanged?.(true);
  }

  disable() {
    const changed = this.enabled;
    this.enabled = false;
    if (changed) this.onEnabledChanged?.(false);
    if (this.locked) {
      document.exitPointerLock();
    }
  }

  _onCanvasClick(event) {
    if (!event.altKey) return;              // only ⌥-click engages capture
    if (this.locked) return;                // already captured
    if (!this.enabled) {
      console.warn("[mouse] ⌥-click ignored — capture disabled (no Mouse card in a slot?). enabled=false");
      return;
    }
    if (!this.canvas || !this.canvas.isConnected) {
      console.warn("[mouse] ⌥-click ignored — canvas missing/detached:", this.canvas);
      return;
    }
    // requestPointerLock rejects silently on a Promise in modern browsers; surface it.
    let p;
    try { p = this.canvas.requestPointerLock(); }
    catch (err) { console.warn("[mouse] requestPointerLock threw:", err); return; }
    if (p && typeof p.then === "function") {
      p.then(() => console.info("[mouse] pointer lock engaged"))
       .catch((err) => console.warn("[mouse] pointer lock rejected:", err?.message || err));
    }
  }

  _onPointerLockChange() {
    if (document.pointerLockElement === this.canvas) {
      this.locked = true;
      document.addEventListener("mousemove", this._onMouseMove);
      document.addEventListener("mousedown", this._onMouseDown);
      document.addEventListener("mouseup", this._onMouseUp);
    } else {
      this.locked = false;
      document.removeEventListener("mousemove", this._onMouseMove);
      document.removeEventListener("mousedown", this._onMouseDown);
      document.removeEventListener("mouseup", this._onMouseUp);
    }
    this.onLockChanged?.(this.locked);
  }

  _onMouseMove(event) {
    if (!this.enabled || !this.locked) return;
    const dx = event.movementX;
    const dy = event.movementY;
    if (dx !== 0 || dy !== 0) {
      this.wasmModule._mouseMove(dx, dy);
    }
  }

  _onMouseDown(event) {
    if (!this.enabled || !this.locked) return;
    if (event.button === 0) {
      this.wasmModule._mouseButton(1);
    }
  }

  _onMouseUp(event) {
    if (!this.enabled || !this.locked) return;
    if (event.button === 0) {
      this.wasmModule._mouseButton(0);
    }
  }

  destroy() {
    this.disable();
    document.removeEventListener("pointerlockchange", this._onPointerLockChange);
    if (this.canvas) {
      this.canvas.removeEventListener("click", this._onCanvasClick);
    }
  }
}
