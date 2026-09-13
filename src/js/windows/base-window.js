/*
 * base-window.js - Base window class for all windows
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { formatMachineAddress } from "../machine/machine-profile.js";

/**
 * Returns true when the header is effectively hidden (auto-hide mode).
 */
function isHeaderHidden() {
  const header = document.querySelector("header");
  return header && header.classList.contains("auto-hide");
}

export class BaseWindow {
  constructor(config) {
    this.id = config.id;
    this.title = config.title;
    this.minWidth = config.minWidth || 280;
    this.minHeight = config.minHeight || 200;
    this.maxWidth = config.maxWidth || Infinity;
    this.maxHeight = config.maxHeight || Infinity;
    this.defaultWidth = config.defaultWidth || 400;
    this.defaultHeight = config.defaultHeight || 300;
    this.defaultPosition = config.defaultPosition || null;
    this.closable = config.closable !== false;
    this.focusCanvas = config.focusCanvas || false;

    // Customizable CSS class names (defaults to debug-window style)
    this.cssClasses = {
      window: config.cssClasses?.window || "debug-window",
      header: config.cssClasses?.header || "debug-window-header",
      title: config.cssClasses?.title || "debug-window-title",
      close: config.cssClasses?.close || "debug-window-close",
      content: config.cssClasses?.content || "debug-window-content",
      resizeHandle: config.cssClasses?.resizeHandle || "debug-resize-handle",
    };

    // Resize directions to create handles for
    this.resizeDirections = config.resizeDirections || [
      "n",
      "e",
      "s",
      "w",
      "ne",
      "nw",
      "se",
      "sw",
    ];

    // Optional localStorage key for persisting window state
    this.storageKey = config.storageKey || null;

    this.element = null;
    this.headerElement = null;
    this.contentElement = null;
    this.zIndex = 1000;
    this.isVisible = false;
    this.isDragging = false;
    this.isResizing = false;
    this.dragOffset = { x: 0, y: 0 };
    this.resizeStart = { x: 0, y: 0, width: 0, height: 0, left: 0, top: 0 };
    this.resizeDirection = null;
    this._isPaneled = false;

    // Track current position/size (needed because getBoundingClientRect returns zeros for hidden elements)
    this.currentX = config.defaultPosition?.x ?? 0;
    this.currentY = config.defaultPosition?.y ?? 0;
    this.currentWidth = config.defaultWidth || 400;
    this.currentHeight = config.defaultHeight || 300;

    // Track distance from right/bottom edges for maintaining position on resize
    this.distanceFromRight = null;
    this.distanceFromBottom = null;
    this.lastViewportWidth = window.innerWidth;
    this.lastViewportHeight = window.innerHeight;

    // Bind event handlers
    this.handleMouseDown = this.handleMouseDown.bind(this);
    this.handleMouseMove = this.handleMouseMove.bind(this);
    this.handleMouseUp = this.handleMouseUp.bind(this);
  }

  /**
   * Create the window DOM structure
   */
  create() {
    // Calculate centered position if no explicit default was provided
    if (!this.defaultPosition) {
      const header = document.querySelector("header");
      const footer = document.querySelector("footer");
      const minTop = header ? header.offsetHeight : 0;
      const footerHeight = footer ? footer.offsetHeight : 0;
      const availHeight = window.innerHeight - minTop - footerHeight;
      this.defaultPosition = {
        x: Math.max(0, Math.round((window.innerWidth - this.defaultWidth) / 2)),
        y: Math.max(minTop, Math.round(minTop + (availHeight - this.defaultHeight) / 2)),
      };
      this.currentX = this.defaultPosition.x;
      this.currentY = this.defaultPosition.y;
    }

    // Create main window element
    this.element = document.createElement("div");
    this.element.id = this.id;
    this.element.className = `${this.cssClasses.window} hidden`;
    this.element.style.width = `${this.defaultWidth}px`;
    this.element.style.height = `${this.defaultHeight}px`;
    this.element.style.left = `${this.defaultPosition.x}px`;
    this.element.style.top = `${this.defaultPosition.y}px`;

    // Header (draggable area)
    this.headerElement = document.createElement("div");
    this.headerElement.className = this.cssClasses.header;
    this.headerElement.innerHTML = `
      <span class="${this.cssClasses.title}">${this.title}</span>
      ${this.closable ? `<button class="${this.cssClasses.close}" title="Close">&times;</button>` : ""}
    `;

    // Content area
    this.contentElement = document.createElement("div");
    this.contentElement.className = this.cssClasses.content;
    this.contentElement.innerHTML = this.renderContent();

    // Resize handles
    this.resizeDirections.forEach((dir) => {
      const handle = document.createElement("div");
      handle.className = `${this.cssClasses.resizeHandle} ${dir}`;
      handle.dataset.direction = dir;
      this.element.appendChild(handle);
    });

    // Assemble
    this.element.appendChild(this.headerElement);
    this.element.appendChild(this.contentElement);
    document.body.appendChild(this.element);

    // Set up event listeners
    this.setupEventListeners();

    // Call hook for subclasses to set up after content is rendered
    if (typeof this.onContentRendered === "function") {
      this.onContentRendered();
    }
  }

  /**
   * Set up drag, resize, and close event listeners
   */
  setupEventListeners() {
    // Close button
    const closeBtn = this.headerElement.querySelector(
      `.${this.cssClasses.close}`,
    );
    if (closeBtn) {
      closeBtn.addEventListener("click", () => this.hide());
    }

    // Drag start on header
    this.headerElement.addEventListener("mousedown", (e) => {
      if (e.target.classList.contains(this.cssClasses.close)) return;
      this.startDrag(e);
    });

    // Resize start on handles
    this.element
      .querySelectorAll(`.${this.cssClasses.resizeHandle}`)
      .forEach((handle) => {
        handle.addEventListener("mousedown", (e) => {
          this.startResize(e, handle.dataset.direction);
        });
      });

    // Bring to front on click — if the window isn't focused, consume the
    // first click so it only brings the window to front without interacting
    // with content like text areas. Buttons and interactive controls are
    // allowed through so they respond on the first click.
    this._consumeNextClick = false;
    this.element.addEventListener("mousedown", (e) => {
      const wasFocused = this.element.classList.contains("focused");
      if (this.onFocus) this.onFocus(this.id);
      if (this.focusCanvas && !this.headerElement.contains(e.target)) {
        // Focus the emulator canvas so keystrokes go to the emulator.
        // Blur first to release any focused element (e.g. BASIC textarea),
        // then focus the canvas on next frame after browser focus handling.
        if (document.activeElement && document.activeElement !== document.body) {
          document.activeElement.blur();
        }
        e.preventDefault();
        const canvas = document.getElementById("screen");
        if (canvas) setTimeout(() => canvas.focus(), 0);
        return;
      }
      if (!wasFocused && !this.headerElement.contains(e.target)) {
        const clickable = e.target.closest("button, input, select, label, a, .toggle-switch");
        if (!clickable) {
          this._consumeNextClick = true;
          e.preventDefault();
          e.stopPropagation();
        }
      }
    }, true);
    this.element.addEventListener("click", (e) => {
      if (this._consumeNextClick) {
        this._consumeNextClick = false;
        e.preventDefault();
        e.stopPropagation();
      }
    }, true);

    // Drag/resize listeners are attached only for the duration of a gesture —
    // see _attachGestureListeners(). They used to be registered on document at
    // construction and left there for the life of the session, so with ~26
    // windows every single mousemove event fanned out to 26 handlers that
    // almost always had nothing to do.
  }

  /**
   * Start listening for the mouse events that drive an in-progress
   * drag or resize.
   */
  _attachGestureListeners() {
    if (this._gestureListenersAttached) return;
    this._gestureListenersAttached = true;
    document.addEventListener("mousemove", this.handleMouseMove);
    document.addEventListener("mouseup", this.handleMouseUp);
  }

  _detachGestureListeners() {
    if (!this._gestureListenersAttached) return;
    this._gestureListenersAttached = false;
    document.removeEventListener("mousemove", this.handleMouseMove);
    document.removeEventListener("mouseup", this.handleMouseUp);
  }

  /**
   * Snapshot everything a drag or resize needs to clamp against, once, at the
   * start of the gesture.
   *
   * drag() and resize() used to re-query header/footer and read offsetHeight /
   * offsetWidth on every mousemove, interleaved with style writes — a forced
   * synchronous layout per pointer event, at pointer rate. None of these values
   * can change mid-gesture, so reading them once removes the thrash entirely.
   */
  _captureGestureBounds() {
    const header = document.querySelector("header");
    const footer = document.querySelector("footer");
    this._bounds = {
      minTop: (!header || isHeaderHidden()) ? 0 : header.offsetHeight,
      footerHeight: footer ? footer.offsetHeight : 0,
      viewportWidth: window.innerWidth,
      viewportHeight: window.innerHeight,
      elementWidth: this.element.offsetWidth,
      elementHeight: this.element.offsetHeight,
    };
  }

  /**
   * Handle mouse down for drag/resize detection
   */
  handleMouseDown(e) {
    // Handled by specific listeners
  }

  /**
   * Handle mouse move for dragging and resizing
   */
  handleMouseMove(e) {
    if (this.isDragging) {
      this.drag(e);
    } else if (this.isResizing) {
      this.resize(e);
    }
  }

  /**
   * Handle mouse up to end drag/resize
   */
  handleMouseUp(e) {
    this._detachGestureListeners();
    if (this.isDragging || this.isResizing) {
      const wasDragging = this.isDragging;
      // Commit the last position before clearing gesture state, otherwise a
      // mousemove that landed between animation frames would be dropped and
      // the window would settle a few pixels behind the cursor.
      this._flushGestureWrite();
      this.isDragging = false;
      this.isResizing = false;
      this._bounds = null;
      this.element.classList.remove("dragging", "resizing");

      // Notify dock manager of drag end (before clearing state)
      if (wasDragging && this.onDragEnd) {
        this.onDragEnd(e.clientX, e.clientY);
      }

      if (this.onStateChange) this.onStateChange();
      this.saveSettings();
    }
  }

  /**
   * Start dragging the window
   */
  startDrag(e) {
    this.isDragging = true;
    this.element.classList.add("dragging");
    const rect = this.element.getBoundingClientRect();
    this.dragOffset = {
      x: e.clientX - rect.left,
      y: e.clientY - rect.top,
    };
    this._captureGestureBounds();
    this._attachGestureListeners();
    e.preventDefault();
  }

  /**
   * Begin a drag that was started elsewhere — undocking a tab hands the
   * in-progress gesture over to the newly floating window.
   *
   * This exists because drag listeners are now attached per-gesture: setting
   * isDragging from outside is no longer enough to make the window follow the
   * cursor, it also has to pick up the listeners and bounds that startDrag()
   * would have established.
   */
  beginExternalDrag(offsetX, offsetY) {
    this.isDragging = true;
    this.element.classList.add("dragging");
    this.dragOffset = { x: offsetX, y: offsetY };
    this._captureGestureBounds();
    this._attachGestureListeners();
  }

  /**
   * Handle drag movement.
   *
   * Positions are computed here from cached bounds but written in a
   * requestAnimationFrame, so a high-rate pointer (120Hz+ trackpads are
   * routine) produces at most one style write and one layout per displayed
   * frame rather than one per event.
   */
  drag(e) {
    const b = this._bounds;
    let x = e.clientX - this.dragOffset.x;
    let y = e.clientY - this.dragOffset.y;

    // Keep window on screen, below header, and above footer
    const maxX = b.viewportWidth - b.elementWidth;
    const maxY = b.viewportHeight - b.footerHeight - b.elementHeight;
    x = Math.max(0, Math.min(x, maxX));
    y = Math.max(b.minTop, Math.min(y, maxY));

    this.currentX = x;
    this.currentY = y;

    this._scheduleGestureWrite(() => {
      this.element.style.left = `${x}px`;
      this.element.style.top = `${y}px`;
    });

    // Update edge distances after drag
    this.updateEdgeDistances();

    // Notify dock manager of drag movement
    if (this.onDragMove) this.onDragMove(e.clientX, e.clientY);
  }

  /**
   * Coalesce the style writes of a gesture into one per animation frame.
   */
  _scheduleGestureWrite(write) {
    this._pendingGestureWrite = write;
    if (this._gestureRAF) return;
    this._gestureRAF = requestAnimationFrame(() => {
      this._gestureRAF = null;
      const pending = this._pendingGestureWrite;
      this._pendingGestureWrite = null;
      if (pending) pending();
    });
  }

  /**
   * Flush any coalesced gesture write immediately. Called when the gesture
   * ends so the final position is committed even if the last mousemove landed
   * between animation frames.
   */
  _flushGestureWrite() {
    if (this._gestureRAF) {
      cancelAnimationFrame(this._gestureRAF);
      this._gestureRAF = null;
    }
    const pending = this._pendingGestureWrite;
    this._pendingGestureWrite = null;
    if (pending) pending();
  }

  /**
   * Update tracked distances from right and bottom edges
   */
  updateEdgeDistances() {
    const viewportWidth = window.innerWidth;
    const viewportHeight = window.innerHeight;
    // Mid-gesture the cached footer height is authoritative and avoids a
    // forced layout on every pointer event.
    let footerHeight;
    if (this._bounds && (this.isDragging || this.isResizing)) {
      footerHeight = this._bounds.footerHeight;
    } else {
      const footer = document.querySelector("footer");
      footerHeight = footer ? footer.offsetHeight : 0;
    }
    const maxBottom = viewportHeight - footerHeight;
    const centerX = this.currentX + this.currentWidth / 2;
    const centerY = this.currentY + this.currentHeight / 2;

    // Track distance from right edge if window is on the right half
    if (centerX > viewportWidth / 2) {
      this.distanceFromRight =
        viewportWidth - (this.currentX + this.currentWidth);
    } else {
      this.distanceFromRight = null;
    }

    // Track distance from bottom edge (above footer) if window is on the bottom half
    if (centerY > viewportHeight / 2) {
      this.distanceFromBottom =
        maxBottom - (this.currentY + this.currentHeight);
    } else {
      this.distanceFromBottom = null;
    }

    this.lastViewportWidth = viewportWidth;
    this.lastViewportHeight = viewportHeight;
  }

  /**
   * Start resizing the window
   */
  startResize(e, direction) {
    if (this.onFocus) this.onFocus(this.id);
    this.isResizing = true;
    this.resizeDirection = direction;
    this.element.classList.add("resizing");
    const rect = this.element.getBoundingClientRect();
    this.resizeStart = {
      x: e.clientX,
      y: e.clientY,
      width: rect.width,
      height: rect.height,
      left: rect.left,
      top: rect.top,
    };
    this._captureGestureBounds();
    this._attachGestureListeners();
    e.preventDefault();
    e.stopPropagation();
  }

  /**
   * Handle resize movement
   */
  resize(e) {
    const dx = e.clientX - this.resizeStart.x;
    const dy = e.clientY - this.resizeStart.y;
    const dir = this.resizeDirection;

    // Bounds were captured once at startResize — see _captureGestureBounds().
    const b = this._bounds;
    const minTop = b.minTop;
    const maxBottom = b.viewportHeight - b.footerHeight;

    let newWidth = this.resizeStart.width;
    let newHeight = this.resizeStart.height;
    let newLeft = this.resizeStart.left;
    let newTop = this.resizeStart.top;

    // Calculate new dimensions based on direction
    if (dir.includes("e")) {
      newWidth = Math.min(this.maxWidth, Math.max(this.minWidth, this.resizeStart.width + dx));
    }
    if (dir.includes("w")) {
      const proposedWidth = Math.min(this.maxWidth, this.resizeStart.width - dx);
      if (proposedWidth >= this.minWidth) {
        newWidth = proposedWidth;
        newLeft = this.resizeStart.left + (this.resizeStart.width - proposedWidth);
      }
    }
    if (dir.includes("s")) {
      newHeight = Math.min(this.maxHeight, Math.max(this.minHeight, this.resizeStart.height + dy));
    }
    if (dir.includes("n")) {
      const proposedHeight = Math.min(this.maxHeight, this.resizeStart.height - dy);
      if (proposedHeight >= this.minHeight) {
        newHeight = proposedHeight;
        newTop = this.resizeStart.top + (this.resizeStart.height - proposedHeight);
      }
    }

    // Keep on screen (respect header and footer)
    newLeft = Math.max(0, newLeft);
    newTop = Math.max(minTop, newTop);
    if (newLeft + newWidth > window.innerWidth) {
      newWidth = window.innerWidth - newLeft;
    }
    if (newTop + newHeight > maxBottom) {
      newHeight = maxBottom - newTop;
    }

    // Re-apply minimum constraints after viewport clamping, shifting
    // position if needed so the window stays on screen at its min size
    if (newWidth < this.minWidth) {
      newWidth = this.minWidth;
      newLeft = Math.max(0, Math.min(newLeft, window.innerWidth - newWidth));
    }
    if (newHeight < this.minHeight) {
      newHeight = this.minHeight;
      newTop = Math.max(minTop, Math.min(newTop, maxBottom - newHeight));
    }

    this.currentWidth = newWidth;
    this.currentHeight = newHeight;
    this.currentX = newLeft;
    this.currentY = newTop;

    this._scheduleGestureWrite(() => {
      this.element.style.width = `${newWidth}px`;
      this.element.style.height = `${newHeight}px`;
      this.element.style.left = `${newLeft}px`;
      this.element.style.top = `${newTop}px`;
      // Content that needs to react to its new box (canvas rasters, rulers)
      // listens via ResizeObserver, which fires off the write above.
    });
  }

  /**
   * Show the window
   */
  show() {
    this.element.classList.remove("hidden");
    this.isVisible = true;
    // Ensure window is within viewport when shown
    this.constrainToViewport();
    if (this.onFocus) this.onFocus(this.id);
  }

  /**
   * Hide the window
   */
  hide() {
    // Set visibility flag first so getState() returns correct value
    this.isVisible = false;
    // Save state BEFORE adding hidden class, since getBoundingClientRect returns zeros for display:none
    if (this.onStateChange) this.onStateChange();
    this.saveSettings();
    this.element.classList.add("hidden");
    // Refocus canvas for keyboard input
    const canvas = document.getElementById("screen");
    if (canvas) {
      setTimeout(() => canvas.focus(), 0);
    }
  }

  /**
   * Toggle window visibility
   */
  toggle() {
    if (this.isVisible) {
      this.hide();
    } else {
      this.show();
    }
  }

  /**
   * Set window z-index
   */
  setZIndex(z) {
    this.zIndex = z;
    this.element.style.zIndex = z;
  }

  /**
   * Whether this window supports resizing
   */
  get isResizable() {
    return this.resizeDirections.length > 0;
  }

  /**
   * Get window state for persistence
   */
  getState() {
    // Use tracked values instead of getBoundingClientRect which returns zeros for hidden elements
    const state = {
      x: this.currentX,
      y: this.currentY,
      visible: this.isVisible,
      zIndex: this.zIndex,
    };
    // Only persist size for resizable windows; fixed-size windows always use their defaults
    if (this.isResizable) {
      state.width = this.currentWidth;
      state.height = this.currentHeight;
    }
    return state;
  }

  /**
   * Restore window state from persistence
   */
  restoreState(state) {
    if (state.x !== undefined) {
      this.element.style.left = `${state.x}px`;
      this.currentX = state.x;
    }
    if (state.y !== undefined) {
      this.element.style.top = `${state.y}px`;
      this.currentY = state.y;
    }
    // Only restore size for resizable windows; fixed-size windows keep their defaults
    if (this.isResizable) {
      if (state.width !== undefined) {
        const width = Math.min(this.maxWidth, Math.max(state.width, this.minWidth));
        this.element.style.width = `${width}px`;
        this.currentWidth = width;
      }
      if (state.height !== undefined) {
        const height = Math.min(this.maxHeight, Math.max(state.height, this.minHeight));
        this.element.style.height = `${height}px`;
        this.currentHeight = height;
      }
    }

    // Ensure window is within current viewport bounds
    this.constrainToViewport();

    // Calculate edge distances based on restored position
    this.updateEdgeDistances();

    if (state.visible && !this.skipVisibilityRestore) {
      this.show();
    }
  }

  /**
   * Constrain window position to keep it within the visible viewport
   * Maintains distance from right/bottom edges for windows on those sides
   */
  constrainToViewport() {
    if (!this.element) return;

    const viewportWidth = window.innerWidth;
    const viewportHeight = window.innerHeight;
    const width = this.currentWidth;
    const height = this.currentHeight;

    // Get header and footer heights to prevent windows going under/over them
    const header = document.querySelector("header");
    const footer = document.querySelector("footer");
    const minTop = (!header || isHeaderHidden()) ? 0 : header.offsetHeight;
    const footerHeight = footer ? footer.offsetHeight : 0;
    const maxBottom = viewportHeight - footerHeight;

    let newLeft = this.currentX;
    let newTop = this.currentY;
    let changed = false;

    // If window was on the right side, maintain distance from right edge
    if (this.distanceFromRight !== null) {
      const targetLeft = viewportWidth - width - this.distanceFromRight;
      if (targetLeft !== newLeft) {
        newLeft = targetLeft;
        changed = true;
      }
    }

    // If window was on the bottom side, maintain distance from bottom edge
    if (this.distanceFromBottom !== null) {
      const targetTop = maxBottom - height - this.distanceFromBottom;
      if (targetTop !== newTop) {
        newTop = targetTop;
        changed = true;
      }
    }

    // Ensure window stays within viewport bounds
    if (width >= viewportWidth) {
      newLeft = 0;
      changed = true;
    } else if (newLeft + width > viewportWidth) {
      newLeft = viewportWidth - width;
      changed = true;
    } else if (newLeft < 0) {
      newLeft = 0;
      changed = true;
    }

    if (height >= maxBottom - minTop) {
      newTop = minTop;
      changed = true;
    } else if (newTop + height > maxBottom) {
      newTop = maxBottom - height;
      changed = true;
    } else if (newTop < minTop) {
      newTop = minTop;
      changed = true;
    }

    if (changed) {
      this.element.style.left = `${newLeft}px`;
      this.element.style.top = `${newTop}px`;
      this.currentX = newLeft;
      this.currentY = newTop;
    }

    // Update viewport tracking
    this.lastViewportWidth = viewportWidth;
    this.lastViewportHeight = viewportHeight;
  }

  /**
   * Detach contentElement from the BaseWindow shell for panel mode.
   * Hides the window shell but keeps the content element alive and reparentable.
   */
  detachContent() {
    if (this._isPaneled) return;
    this._isPaneled = true;

    // Remove contentElement from the window shell (but don't destroy it)
    if (this.contentElement && this.contentElement.parentNode === this.element) {
      this.element.removeChild(this.contentElement);
    }

    // Hide the window shell
    this.element.classList.add('hidden');
    this.element.classList.add('paneled');
  }

  /**
   * Return contentElement back into the BaseWindow shell (float mode).
   */
  reattachContent() {
    if (!this._isPaneled) return;
    this._isPaneled = false;

    // Put contentElement back into the window element
    if (this.contentElement && this.contentElement.parentNode !== this.element) {
      this.element.appendChild(this.contentElement);
    }

    // Remove paneled class (visibility controlled by isVisible)
    this.element.classList.remove('paneled');

    // Restore visibility based on actual state
    if (this.isVisible) {
      this.element.classList.remove('hidden');
    }
  }

  /**
   * Override in subclasses to provide window content HTML
   */
  renderContent() {
    return "<p>Override renderContent() in subclass</p>";
  }

  /**
   * Override in subclasses to update window content.
   * Only called when the window is visible.
   */
  update(wasmModule) {
    // Override in subclasses
  }

  /**
   * Override in subclasses to set up additional event listeners
   */
  setupContentEventListeners() {
    // Override in subclasses
  }

  /**
   * Helper to format a hex byte
   */
  formatHex(value, digits = 2) {
    return value.toString(16).toUpperCase().padStart(digits, "0");
  }

  /**
   * Helper to format a hex address
   */
  /**
   * As the machine being debugged writes one: four hex digits, or a bank, a
   * slash and the offset — "$E1/2000" — on a machine that has banks. Every
   * window that shows an address agrees, rather than each deciding.
   */
  formatAddr(value) {
    return "$" + formatMachineAddress(value);
  }

  /**
   * Save window state to localStorage (if storageKey is configured)
   */
  saveSettings() {
    if (!this.storageKey) return;
    try {
      const settings = {
        x: this.currentX,
        y: this.currentY,
        visible: this.isVisible,
      };
      if (this.isResizable) {
        settings.width = this.currentWidth;
        settings.height = this.currentHeight;
      }
      localStorage.setItem(this.storageKey, JSON.stringify(settings));
    } catch (e) {
      console.warn(`Failed to save ${this.storageKey} settings:`, e.message);
    }
  }

  /**
   * Load window state from localStorage (if storageKey is configured)
   */
  loadSettings() {
    if (!this.storageKey) return;
    try {
      const saved = localStorage.getItem(this.storageKey);
      if (saved) {
        const state = JSON.parse(saved);
        if (state.x !== undefined) this.currentX = state.x;
        if (state.y !== undefined) this.currentY = state.y;

        this.element.style.left = `${this.currentX}px`;
        this.element.style.top = `${this.currentY}px`;

        // Only restore size for resizable windows; fixed-size windows keep their defaults
        if (this.isResizable) {
          if (state.width !== undefined)
            this.currentWidth = Math.min(this.maxWidth, Math.max(state.width, this.minWidth));
          if (state.height !== undefined)
            this.currentHeight = Math.min(this.maxHeight, Math.max(state.height, this.minHeight));
          this.element.style.width = `${this.currentWidth}px`;
          this.element.style.height = `${this.currentHeight}px`;
        }
      }
    } catch (e) {
      console.warn(`Failed to load ${this.storageKey} settings:`, e.message);
    }
  }

  /**
   * Clean up event listeners and remove element from DOM
   */
  destroy() {
    this._detachGestureListeners();
    if (this._gestureRAF) {
      cancelAnimationFrame(this._gestureRAF);
      this._gestureRAF = null;
    }
    if (this.element && this.element.parentNode) {
      this.element.parentNode.removeChild(this.element);
    }
  }
}
