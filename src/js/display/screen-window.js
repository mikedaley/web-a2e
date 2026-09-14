/*
 * screen-window.js - Main emulator screen window
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { BaseWindow } from "../windows/base-window.js";
import { machineAspect } from "../machine/machine-profile.js";

export class ScreenWindow extends BaseWindow {
  constructor(renderer, textSelection) {
    super({
      id: "screen-window",
      title: "Screen",
      minWidth: 284, // 280 min canvas + 4px padding/border
      minHeight: 244, // 210 min canvas + ~34px header
      defaultWidth: 480,
      defaultHeight: 394,
      closable: false,
      focusCanvas: true,
    });

    this.renderer = renderer;
    this.textSelection = textSelection;
    this._viewportLocked = false;
    // The shape the monitor shows the frame at, not the frame's pixel count:
    // a IIgs's raster is 736x448 and is shown at 4:3.
    this._aspect = machineAspect();
  }

  renderContent() {
    return '<div class="screen-window-content"></div>';
  }

  /** The machine changed, and with it the shape of its picture. */
  setAspect(aspect) {
    this._aspect = aspect;
    this.fitToViewport();
    this._fitCanvas();
  }

  /**
   * After create(), inject the charset toggle into the header and
   * set up a ResizeObserver so the canvas tracks container size.
   */
  onContentRendered() {
    const charsetSwitch = document.createElement("div");
    charsetSwitch.className = "screen-window-charset-switch";
    charsetSwitch.title = "Character Set (US/UK)";
    charsetSwitch.innerHTML = `
      <span class="charset-label">US</span>
      <label class="charset-toggle">
        <input type="checkbox" id="screen-window-charset-toggle" />
        <span class="charset-slider"></span>
      </label>
      <span class="charset-label">UK</span>
    `;

    // Viewport lock button (expand/compress arrows)
    this._lockBtn = document.createElement("button");
    this._lockBtn.className = "screen-window-lock";
    this._lockBtn.title = "Fit to viewport";
    this._lockBtn.innerHTML = `
      <svg class="lock-icon-expand" viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" stroke-width="2.5">
        <path d="M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7"/>
      </svg>
      <svg class="lock-icon-compress" viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" stroke-width="2.5">
        <path d="M4 14h6v6M20 10h-6V4M14 10l7-7M3 21l7-7"/>
      </svg>
    `;

    // Cursor keys joystick toggle
    this._cursorKeysSwitch = document.createElement("div");
    this._cursorKeysSwitch.className = "screen-window-cursor-keys-switch";
    this._cursorKeysSwitch.title = "Use cursor keys as joystick";
    this._cursorKeysSwitch.innerHTML = `
      <span class="cursor-keys-label">&#x2734;</span>
      <label class="cursor-keys-toggle">
        <input type="checkbox" id="screen-window-cursor-keys-toggle" />
        <span class="cursor-keys-slider"></span>
      </label>
      <span class="cursor-keys-label cursor-keys-label-text">JOY</span>
    `;
    this._cursorKeysCheckbox = this._cursorKeysSwitch.querySelector(
      "#screen-window-cursor-keys-toggle",
    );

    // Accelerated-clock indicator. Hidden at 1x — at real //e speed there is
    // nothing to warn about, and an always-on "1x" is just noise.
    this._speedChip = document.createElement("span");
    this._speedChip.className = "screen-window-speed-chip";
    this._speedChip.style.display = "none";

    // Insert charset switch and cursor keys toggle into header
    this.headerElement.appendChild(this._speedChip);
    this.headerElement.appendChild(charsetSwitch);
    this.headerElement.appendChild(this._cursorKeysSwitch);
    // Lock button appended to the window element so it stays visible in chromeless mode
    this.element.appendChild(this._lockBtn);

    // Prevent clicks from starting a window drag
    charsetSwitch.addEventListener("mousedown", (e) => {
      e.stopPropagation();
    });
    this._cursorKeysSwitch.addEventListener("mousedown", (e) => {
      e.stopPropagation();
    });
    this._lockBtn.addEventListener("mousedown", (e) => {
      e.stopPropagation();
    });
    this._lockBtn.addEventListener("click", () => {
      this.setViewportLocked(!this._viewportLocked);
    });

    // Observe the content container so _fitCanvas runs whenever
    // the window is resized, restored, arranged, etc.
    const container = this.contentElement.querySelector(
      ".screen-window-content",
    );
    if (container) {
      this._resizeObserver = new ResizeObserver(() => this._fitCanvas());
      this._resizeObserver.observe(container);
    }
  }

  /**
   * Set viewport-lock state and immediately resize if locking on.
   */
  setViewportLocked(locked) {
    this._viewportLocked = locked;
    if (this._lockBtn) {
      this._lockBtn.classList.toggle("active", locked);
      this._lockBtn.title = locked ? "Unlock from viewport" : "Fit to viewport";
    }

    // Hide/show window furniture (header, border, radius, resize handles)
    if (this.element) {
      this.element.classList.toggle("chromeless", locked);
    }

    if (locked) {
      this.constrainToViewport();
    } else {
      // Exiting chromeless: the header is now visible again and takes up space.
      // Adjust window height so the content area + header fits the aspect ratio.
      requestAnimationFrame(() => {
        const headerH = this.headerElement ? this.headerElement.offsetHeight : 0;
        const contentW = this.currentWidth;
        const contentH = Math.round(contentW / this._aspect);
        const newHeight = contentH + headerH;

        this.element.style.height = `${newHeight}px`;
        this.currentHeight = newHeight;

        // Re-centre vertically
        const headerEl = document.querySelector("header");
        const minTop = headerEl ? headerEl.offsetHeight : 0;
        const vpH = window.innerHeight;
        const y = Math.round(minTop + (vpH - minTop - newHeight) / 2);
        this.element.style.top = `${y}px`;
        this.currentY = y;

        this._fitCanvas();
      });
    }

    if (this.onStateChange) this.onStateChange();
  }

  /**
   * Show the current CPU speed in the title bar, or hide the chip at 1x.
   */
  setSpeedState(multiplier, clockText = "") {
    if (!this._speedChip) return;
    const accelerated = multiplier > 1;
    this._speedChip.textContent = `${multiplier}x`;
    this._speedChip.title = clockText
      ? `CPU running at ${clockText}`
      : "Accelerated CPU speed";
    this._speedChip.style.display = accelerated ? "" : "none";
  }

  /**
   * Set the cursor keys toggle state and optional change handler.
   */
  setCursorKeysState(enabled) {
    if (this._cursorKeysCheckbox) {
      this._cursorKeysCheckbox.checked = enabled;
    }
  }

  onCursorKeysToggle(callback) {
    if (this._cursorKeysCheckbox) {
      this._cursorKeysCheckbox.addEventListener("change", () => {
        callback(this._cursorKeysCheckbox.checked);
      });
    }
  }

  /**
   * Move #screen canvas from #monitor-frame into this window's content area.
   */
  attachCanvas() {
    const canvas = document.getElementById("screen");
    if (!canvas) return;

    const container = this.contentElement.querySelector(
      ".screen-window-content",
    );
    if (!container) return;

    // Clear any inline sizing
    canvas.style.width = "";
    canvas.style.height = "";
    canvas.style.marginTop = "";

    container.appendChild(canvas);

    if (this.textSelection) {
      this.textSelection.reattach();
    }

    this._fitCanvas();
  }

  /**
   * Move #screen canvas back into #monitor-frame (used by full-page mode).
   */
  detachCanvas() {
    const canvas = document.getElementById("screen");
    if (!canvas) return;

    const frame = document.getElementById("monitor-frame");
    if (!frame) return;

    frame.appendChild(canvas);

    // Clear inline styles
    canvas.style.width = "";
    canvas.style.height = "";

    if (this.textSelection) {
      this.textSelection.reattach();
    }
  }

  /**
   * After showing, fit the canvas to the content area.
   */
  show() {
    super.show();
    requestAnimationFrame(() => this._fitCanvas());
  }

  /**
   * Get window state for persistence (adds viewportLocked).
   */
  getState() {
    const base = super.getState();
    base.viewportLocked = this._viewportLocked;
    return base;
  }

  /**
   * Restore persisted state.
   */
  restoreState(state) {
    if (state.viewportLocked) {
      this.setViewportLocked(true);
    }
    super.restoreState(state);
  }

  /**
   * Scale the window down (if needed) so it fits within the browser
   * viewport while maintaining aspect ratio, then centre it.
   */
  fitToViewport() {
    if (!this.element || this._viewportLocked) return;

    const vpW = window.innerWidth;
    const vpH = window.innerHeight;
    const headerEl = document.querySelector("header");
    const minTop = headerEl ? headerEl.offsetHeight : 0;
    const margin = 24;
    const headerH = this.headerElement ? this.headerElement.offsetHeight : 0;

    const availW = vpW - margin * 2;
    const availH = vpH - minTop - margin * 2;

    let w = this.currentWidth;
    let contentH = w / this._aspect;
    let h = contentH + headerH;

    // Scale down if too large
    if (w > availW || h > availH) {
      if (availW / this._aspect + headerH <= availH) {
        w = availW;
      } else {
        w = Math.round((availH - headerH) * this._aspect);
      }
      contentH = w / this._aspect;
      h = Math.round(contentH) + headerH;
      w = Math.round(w);
    }

    const x = Math.round((vpW - w) / 2);
    const y = Math.round(minTop + (availH - h) / 2 + margin);

    this.element.style.width = `${w}px`;
    this.element.style.height = `${h}px`;
    this.element.style.left = `${x}px`;
    this.element.style.top = `${y}px`;
    this.currentWidth = w;
    this.currentHeight = h;
    this.currentX = x;
    this.currentY = y;

    this._fitCanvas();
  }

  /**
   * Constrain to viewport.  When viewport-locked, fill the available
   * area and centre.  The canvas handles its own 4:3 aspect ratio
   * via _fitCanvas.
   */
  constrainToViewport() {
    if (!this.element) return;

    if (this._viewportLocked) {
      const vpW = window.innerWidth;
      const vpH = window.innerHeight;
      const headerEl = document.querySelector("header");
      // Ignore the app header when it's hidden (auto-hide mode)
      const headerHidden = !headerEl || headerEl.classList.contains("auto-hide");
      const minTop = headerHidden ? 0 : headerEl.offsetHeight;
      const margin = 24;

      // Available space below the app header
      const availW = vpW - margin * 2;
      const availH = vpH - minTop - margin * 2;

      // Chromeless hides the window header
      const windowHeaderH = 0;
      const availContentH = availH;

      // Fit the largest content rectangle that maintains aspect ratio
      let contentW, contentH;
      if (availW / this._aspect <= availContentH) {
        contentW = availW;
        contentH = Math.round(availW / this._aspect);
      } else {
        contentH = availContentH;
        contentW = Math.round(availContentH * this._aspect);
      }

      const w = contentW;
      const h = contentH + windowHeaderH;

      // Centre in the available space below the app header
      const x = Math.round((vpW - w) / 2);
      const y = Math.round(minTop + margin + (availH - h) / 2);

      this.element.style.width = `${w}px`;
      this.element.style.height = `${h}px`;
      this.element.style.left = `${x}px`;
      this.element.style.top = `${y}px`;
      this.currentWidth = w;
      this.currentHeight = h;
      this.currentX = x;
      this.currentY = y;

      this.lastViewportWidth = vpW;
      this.lastViewportHeight = vpH;
    } else {
      // Shrink to fit viewport if too large, maintaining aspect ratio
      const vpW = window.innerWidth;
      const vpH = window.innerHeight;
      const headerEl = document.querySelector("header");
      const minTop = headerEl ? headerEl.offsetHeight : 0;
      const windowHeaderH = this.headerElement ? this.headerElement.offsetHeight : 0;
      const availH = vpH - minTop;

      if (this.currentHeight > availH || this.currentWidth > vpW) {
        const maxContentH = availH - windowHeaderH;
        const maxContentW = vpW;
        let w = this.currentWidth;
        let contentH = w / this._aspect;

        if (contentH + windowHeaderH > availH || w > maxContentW) {
          if (maxContentW / this._aspect <= maxContentH) {
            w = maxContentW;
          } else {
            w = Math.round(maxContentH * this._aspect);
          }
          contentH = w / this._aspect;
        }

        const h = Math.round(contentH) + windowHeaderH;
        w = Math.round(w);

        this.element.style.width = `${w}px`;
        this.element.style.height = `${h}px`;
        this.currentWidth = w;
        this.currentHeight = h;
      }

      // Ensure top is below the app header
      if (this.currentY < minTop) {
        this.currentY = minTop;
        this.element.style.top = `${minTop}px`;
      }

      super.constrainToViewport();
    }
  }

  /**
   * Override resize to enforce 4:3 aspect ratio on the content area.
   * Whichever dimension the user drags drives the other so the window
   * always holds an exact 4:3 content rectangle.
   */
  resize(e) {
    const dir = this.resizeDirection;

    // Let the base class compute unconstrained new dimensions
    super.resize(e);

    // Enforce aspect ratio on the content area (window height = content + header).
    // Header height is captured once per gesture rather than measured on every
    // mousemove — see BaseWindow._captureGestureBounds().
    const headerHeight = this._bounds?.headerHeight ?? 0;

    // Anchor edges: resizing from n keeps bottom fixed, from w keeps right fixed
    const bottom = this.currentY + this.currentHeight;
    const right = this.currentX + this.currentWidth;

    let newWidth = this.currentWidth;
    let newHeight;

    if (dir === "n" || dir === "s") {
      // Pure vertical drag: width follows height
      const contentHeight = this.currentHeight - headerHeight;
      newWidth = Math.round(contentHeight * this._aspect);
      newHeight = this.currentHeight;
    } else {
      // Has horizontal component (e, w, corners): height follows width
      const targetContentHeight = Math.round(this.currentWidth / this._aspect);
      newHeight = targetContentHeight + headerHeight;
    }

    // Enforce minimums while maintaining ratio
    if (newWidth < this.minWidth) {
      newWidth = this.minWidth;
      newHeight = Math.round(newWidth / this._aspect) + headerHeight;
    }
    if (newHeight < this.minHeight) {
      newHeight = this.minHeight;
      newWidth = Math.round((newHeight - headerHeight) * this._aspect);
    }

    // Adjust position so the anchored edge stays fixed
    let newLeft = this.currentX;
    let newTop = this.currentY;

    if (dir.includes("n")) {
      newTop = bottom - newHeight;
    }
    if (dir.includes("w")) {
      newLeft = right - newWidth;
    }

    this.currentWidth = newWidth;
    this.currentHeight = newHeight;
    this.currentX = newLeft;
    this.currentY = newTop;

    // Replaces the write super.resize() scheduled with its unconstrained
    // dimensions — _scheduleGestureWrite keeps only the latest write, so the
    // aspect-corrected values are the ones that land. _fitCanvas() rides along
    // inside the same frame so its clientWidth read happens once per frame
    // immediately after the style write, instead of once per pointer event.
    this._scheduleGestureWrite(() => {
      this.element.style.width = `${newWidth}px`;
      this.element.style.height = `${newHeight}px`;
      this.element.style.left = `${newLeft}px`;
      this.element.style.top = `${newTop}px`;
      this._fitCanvas();
    });
  }

  /**
   * Header height is needed by the aspect-ratio maths on every resize step.
   */
  _captureGestureBounds() {
    super._captureGestureBounds();
    this._bounds.headerHeight = this.headerElement
      ? this.headerElement.offsetHeight
      : 0;
  }

  /**
   * After resize completes, do a final canvas fit.
   */
  handleMouseUp(e) {
    const wasResizing = this.isResizing;
    super.handleMouseUp(e);
    if (wasResizing) {
      this._fitCanvas();
    }
  }

  /**
   * Compute the largest 4:3 rectangle that fits within the content
   * container and apply it to the canvas display size and renderer.
   */
  _fitCanvas() {
    const container = this.contentElement?.querySelector(
      ".screen-window-content",
    );
    const canvas = document.getElementById("screen");
    if (!container || !canvas) return;

    const cw = container.clientWidth;
    const ch = container.clientHeight;
    if (cw <= 0 || ch <= 0) return;

    let w, h;
    if (cw / ch <= this._aspect) {
      w = cw;
      h = cw / this._aspect;
    } else {
      h = ch;
      w = ch * this._aspect;
    }
    w = Math.floor(w);
    h = Math.floor(h);

    canvas.style.width = `${w}px`;
    canvas.style.height = `${h}px`;

    if (this.renderer) {
      this.renderer.resize(w, h);
    }
    if (this.textSelection) {
      this.textSelection.resize();
    }
  }

  /**
   * Clean up ResizeObserver.
   */
  destroy() {
    if (this._resizeObserver) {
      this._resizeObserver.disconnect();
      this._resizeObserver = null;
    }
    super.destroy();
  }
}
