/*
 * window-manager.js - Window manager for all windows
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

export class WindowManager {
  constructor() {
    this.windows = new Map();
    this.highestZIndex = 1000;
    this.storageKey = 'a2e-debug-windows';
    this._paneledWindows = new Set();
    this.dockManager = null;

    // Bind and set up window resize listener to keep windows in viewport
    this.handleWindowResize = this.handleWindowResize.bind(this);
    window.addEventListener('resize', this.handleWindowResize);
  }

  /**
   * Handle browser window resize - constrain all windows to viewport
   */
  handleWindowResize() {
    this.constrainAllToViewport();
  }

  /**
   * Register a window with the manager
   */
  /**
   * Tell every window the machine changed.
   *
   * A debug view is shaped by the machine it is looking at — how wide an
   * address is, which registers exist, how many scanlines there are — so
   * switching machines has to reach all of them. Broadcasting beats naming
   * each window at the call site: a window added later is included without
   * anyone remembering to add it.
   */
  notifyMachineChanged() {
    for (const window of this.windows.values()) {
      try {
        window.onMachineChanged?.();
      } catch (error) {
        console.warn(`${window.id}: machine change handler failed`, error);
      }
    }
  }

  register(window) {
    this.windows.set(window.id, window);

    // Set up callbacks
    window.onFocus = (id) => this.bringToFront(id);
    window.onStateChange = () => this.saveState();
  }

  /**
   * Get a window by ID
   */
  getWindow(id) {
    return this.windows.get(id);
  }

  /**
   * Show a specific window
   */
  showWindow(id) {
    // If docked, activate the tab instead of showing as floating
    if (this.dockManager && this.dockManager.isDocked(id)) {
      this.dockManager.activateTab(id);
      return;
    }
    const window = this.windows.get(id);
    if (window) {
      window.show();
      this.bringToFront(id);
      this.saveState();
      if (typeof umami !== 'undefined') {
        umami.track(`window-open-${id}`);
      }
    }
  }

  /**
   * Hide a specific window
   */
  hideWindow(id) {
    const window = this.windows.get(id);
    if (window) {
      window.hide();
      window.element.classList.remove('focused');
      this.focusTopWindow();
      this.saveState();
      if (typeof umami !== 'undefined') {
        umami.track(`window-close-${id}`);
      }
    }
  }

  /**
   * Toggle a window's visibility
   */
  toggleWindow(id) {
    // If docked, activate the tab instead of toggling
    if (this.dockManager && this.dockManager.isDocked(id)) {
      this.dockManager.activateTab(id);
      return;
    }
    const window = this.windows.get(id);
    if (window) {
      window.toggle();
      if (window.isVisible) {
        this.bringToFront(id);
        if (typeof umami !== 'undefined') {
          umami.track(`window-open-${id}`);
        }
      } else {
        window.element.classList.remove('focused');
        this.focusTopWindow();
        if (typeof umami !== 'undefined') {
          umami.track(`window-close-${id}`);
        }
      }
      this.saveState();
    }
  }

  /**
   * Check if a window is visible
   */
  isWindowVisible(id) {
    const window = this.windows.get(id);
    if (this.dockManager && this.dockManager.isDocked(id)) return true;
    return window ? window.isVisible : false;
  }

  /**
   * Check if a window is currently docked.
   */
  isDocked(id) {
    return this.dockManager ? this.dockManager.isDocked(id) : false;
  }

  /**
   * Hide all windows
   */
  hideAll() {
    for (const window of this.windows.values()) {
      window.hide();
      window.element.classList.remove('focused');
    }
    this.saveState();
  }

  /**
   * Bring a window to the front.
   * Caps z-index below 2000 so header dropdown menus always render on top.
   */
  bringToFront(id) {
    this.highestZIndex++;
    if (this.highestZIndex >= 1900) {
      this.normalizeZIndices(id);
    } else {
      const window = this.windows.get(id);
      if (window) {
        window.setZIndex(this.highestZIndex);
      }
    }
    this.setFocused(id);
  }

  /**
   * Mark a window as focused, removing focus from all others
   */
  setFocused(id) {
    for (const [winId, win] of this.windows) {
      if (winId === id) {
        win.element.classList.add('focused');
      } else {
        win.element.classList.remove('focused');
      }
    }
  }

  /**
   * Focus the topmost visible window by z-index
   */
  focusTopWindow() {
    let topWin = null;
    let topZ = -1;
    for (const win of this.windows.values()) {
      if (win.isVisible && (win.zIndex || 0) > topZ) {
        topZ = win.zIndex || 0;
        topWin = win;
      }
    }
    if (topWin) {
      topWin.element.classList.add('focused');
    }
  }

  /**
   * Reassign z-indices starting from 1000, preserving the current stacking order.
   * The window identified by frontId is placed on top.
   */
  normalizeZIndices(frontId) {
    // Collect windows that have a z-index (visible or not) and sort by current z
    const ordered = [...this.windows.entries()]
      .filter(([id]) => id !== frontId)
      .sort((a, b) => (a[1].zIndex || 0) - (b[1].zIndex || 0));

    let z = 1000;
    for (const [, win] of ordered) {
      win.setZIndex(z++);
    }
    const front = this.windows.get(frontId);
    if (front) {
      front.setZIndex(z);
    }
    this.highestZIndex = z;
  }

  /**
   * Save all window states to localStorage
   */
  saveState() {
    try {
      const state = {};
      for (const [id, window] of this.windows) {
        state[id] = window.getState();
      }
      localStorage.setItem(this.storageKey, JSON.stringify(state));
    } catch (e) {
      console.warn('Could not save debug window state:', e);
    }
  }

  /**
   * Load window states from localStorage
   */
  loadState() {
    try {
      const saved = localStorage.getItem(this.storageKey);
      if (saved) {
        const state = JSON.parse(saved);
        // First pass: restore positions, sizes, and visibility
        // (show() calls bringToFront() which assigns temporary z-indices)
        for (const [id, windowState] of Object.entries(state)) {
          const window = this.windows.get(id);
          if (window) {
            window.restoreState(windowState);
          }
        }
        // Second pass: restore saved z-indices, overriding those assigned by show()
        for (const [id, windowState] of Object.entries(state)) {
          const window = this.windows.get(id);
          if (window && windowState.zIndex !== undefined) {
            window.setZIndex(windowState.zIndex);
            if (windowState.zIndex > this.highestZIndex) {
              this.highestZIndex = windowState.zIndex;
            }
          }
        }
        this.focusTopWindow();
      }
    } catch (e) {
      console.warn('Could not load debug window state:', e);
    }
  }

  /**
   * Clear all saved window state (useful for debugging)
   */
  clearState() {
    try {
      localStorage.removeItem(this.storageKey);
      console.log('Debug window state cleared');
    } catch (e) {
      console.warn('Could not clear debug window state:', e);
    }
  }

  /**
   * Set which windows are currently paneled in a workspace grid.
   * Paneled windows get update() calls even when not "visible" as floating windows.
   * @param {Set<string>} windowIds
   */
  setPaneledWindows(windowIds) {
    this._paneledWindows = windowIds || new Set();
  }

  /**
   * Update all visible and paneled windows.
   *
   * update() is async and is deliberately not awaited — the render loop must
   * not block on a Worker round-trip. That makes an overlap guard essential:
   * if a window's update takes longer than its interval, the next frame would
   * start another one before the first finished and the backlog would grow
   * without limit, each pending update holding its own DOM work. Windows are
   * skipped while one of their updates is still in flight.
   */
  updateAll(wasmModule) {
    this._frameCounter = (this._frameCounter || 0) + 1;
    if (!this._updatesInFlight) this._updatesInFlight = new Set();

    // Ensure docked active-tab windows have correct isVisible each frame,
    // since other code paths (restoreState, hide) may have reset it.
    if (this.dockManager) {
      this.dockManager.syncDockedVisibility();
    }

    for (const window of this.windows.values()) {
      if (!window.isVisible && !this._paneledWindows.has(window.id)) continue;

      const interval = window.updateEveryNFrames || 4;
      if (interval !== 1 && this._frameCounter % interval !== 0) continue;
      if (this._updatesInFlight.has(window.id)) continue;

      let result;
      try {
        result = window.update(wasmModule);
      } catch (e) {
        console.error(`Window "${window.id}" update failed:`, e);
        continue;
      }

      // Synchronous updates return undefined and need no tracking.
      if (result && typeof result.then === 'function') {
        this._updatesInFlight.add(window.id);
        result
          .catch((e) => console.error(`Window "${window.id}" update failed:`, e))
          .finally(() => this._updatesInFlight.delete(window.id));
      }
    }
  }

  /**
   * Get IDs of all visible windows
   */
  getVisibleWindowIds() {
    const ids = [];
    for (const [id, window] of this.windows) {
      if (window.isVisible) {
        ids.push(id);
      }
    }
    return ids;
  }

  /**
   * Cycle focus to the next visible window in z-index order.
   * If reverse is true, cycle to the previous window instead.
   */
  cycleWindow(reverse = false) {
    const visible = [...this.windows.values()]
      .filter(w => w.isVisible)
      .sort((a, b) => (a.zIndex || 0) - (b.zIndex || 0));
    if (visible.length === 0) return;

    // The focused window is the one with the highest z-index (last in sorted order)
    const focusedIndex = visible.length - 1;

    // Cycling forward brings the bottom window to the top;
    // cycling in reverse brings the second-from-top to the top
    let nextIndex;
    if (reverse) {
      nextIndex = focusedIndex - 1;
      if (nextIndex < 0) nextIndex = visible.length - 1;
    } else {
      nextIndex = 0;
    }

    this.bringToFront(visible[nextIndex].id);
  }

  /**
   * Constrain all windows to the visible viewport
   * Call this on window resize to prevent windows from being off-screen
   */
  constrainAllToViewport() {
    for (const window of this.windows.values()) {
      window.constrainToViewport();
    }
  }

  /**
   * Apply default layout for first-time users (no saved state).
   * Each entry: { id, x, y, width, height, visible, position, viewportLocked }
   * Use position: "viewport-fill" for a window that should fill the viewport.
   */
  applyDefaultLayout(layout) {
    const savedState = localStorage.getItem(this.storageKey);
    if (savedState) {
      try {
        const parsed = JSON.parse(savedState);
        if (parsed && Object.keys(parsed).length > 0) return;
      } catch (e) { /* proceed with defaults */ }
    }

    for (const entry of layout) {
      const win = this.windows.get(entry.id);
      if (!win) continue;

      if (entry.position === 'viewport-fill') {
        const header = document.querySelector('header');
        const footer = document.querySelector('footer');
        const headerH = header ? header.offsetHeight : 0;
        const footerH = footer ? footer.offsetHeight : 0;
        const margin = 8;

        const w = window.innerWidth - margin * 2;
        const h = window.innerHeight - headerH - footerH - margin * 2;
        const x = margin;
        const y = headerH + margin;

        win.element.style.left = `${x}px`;
        win.element.style.top = `${y}px`;
        win.element.style.width = `${w}px`;
        win.element.style.height = `${h}px`;
        win.currentX = x;
        win.currentY = y;
        win.currentWidth = w;
        win.currentHeight = h;
      } else {
        if (entry.x !== undefined) {
          win.element.style.left = `${entry.x}px`;
          win.currentX = entry.x;
        }
        if (entry.y !== undefined) {
          win.element.style.top = `${entry.y}px`;
          win.currentY = entry.y;
        }
        if (entry.width !== undefined) {
          const w = Math.min(win.maxWidth, Math.max(entry.width, win.minWidth));
          win.element.style.width = `${w}px`;
          win.currentWidth = w;
        }
        if (entry.height !== undefined) {
          const h = Math.min(win.maxHeight, Math.max(entry.height, win.minHeight));
          win.element.style.height = `${h}px`;
          win.currentHeight = h;
        }
      }

      if (entry.viewportLocked && typeof win.setViewportLocked === 'function') {
        win.setViewportLocked(true);
      }

      if (entry.visible) {
        win.show();
        this.bringToFront(entry.id);
      }
    }
  }

}
