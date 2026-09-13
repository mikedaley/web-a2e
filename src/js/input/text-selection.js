/*
 * text-selection.js - Screen text selection and copy support
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

import { machineDisplay, machineTextArea } from "../machine/machine-profile.js";

/**
 * TextSelection - Enable text selection and copying from the Apple II screen
 *
 * Handles mouse selection on the canvas and converts screen memory to text.
 * Character encoding and screen memory reading are handled by C++ core.
 */

export class TextSelection {
  constructor(canvas, wasmModule, renderer) {
    this.canvas = canvas;
    this.wasmModule = wasmModule;
    this.renderer = renderer || null;

    // Selection state
    this.isSelecting = false;
    this.selectionStart = null;  // {row, col}
    this.selectionEnd = null;    // {row, col}

    // Overlay canvas for selection highlight
    this.overlay = null;
    this.overlayCtx = null;

    // Screen dimensions
    // Cell sizes follow the text area: 40 columns and 24 rows across it,
    // which is 14x16 on a //e and 16x16 on a IIgs, whose text is stretched to
    // its Super Hi-Res width and sits inside a border.
    const text = machineTextArea();
    this.charWidth40 = text.width / 40;
    this.charWidth80 = text.width / 80;
    this.charHeight = text.height / 24;
    this.rows = 24;

    // Bind event handlers for proper cleanup
    this.boundOnMouseDown = this.onMouseDown.bind(this);
    this.boundOnMouseMove = this.onMouseMove.bind(this);
    this.boundOnMouseUp = this.onMouseUp.bind(this);
    this.boundOnKeyDown = this.onKeyDown.bind(this);
    this.boundOnContextMenu = this.onContextMenu.bind(this);

    this.setupOverlay();
    this.setupEventListeners();
  }

  setupOverlay() {
    const { width, height } = machineDisplay();
    this.overlay = document.createElement('canvas');
    this.overlay.width = width;
    this.overlay.height = height;
    this.overlayCtx = this.overlay.getContext('2d');
  }

  /** The machine changed: a new frame size, and a new text area inside it. */
  onMachineChanged() {
    const text = machineTextArea();
    this.charWidth40 = text.width / 40;
    this.charWidth80 = text.width / 80;
    this.charHeight = text.height / 24;
    this.clearSelection?.();
    this.setupOverlay();
  }

  setupEventListeners() {
    // mousedown on canvas starts selection
    this.canvas.addEventListener('mousedown', this.boundOnMouseDown);
    // keydown in capture phase so we can intercept before input handler
    document.addEventListener('keydown', this.boundOnKeyDown, true);
    this.canvas.addEventListener('contextmenu', this.boundOnContextMenu);
  }

  /**
   * Get soft switch state once and extract all mode flags
   * Soft switch bits from emulator (see emulator.cpp getSoftSwitchState):
   *   Bit 0: TEXT mode
   *   Bit 1: MIXED mode
   *   Bit 2: PAGE2
   *   Bit 3: HIRES
   *   Bit 4: 80COL
   *   Bit 5: ALTCHARSET
   * @returns {{textMode: boolean, col80: boolean, page2: boolean}}
   */
  async getDisplayMode() {
    if (!this.wasmModule._getSoftSwitchState) {
      return { textMode: false, col80: false, page2: false };
    }

    const state = await this.wasmModule._getSoftSwitchState();
    return {
      textMode: (state & 0x01) !== 0,  // Bit 0: TEXT mode
      col80: (state & 0x10) !== 0,     // Bit 4: 80COL mode
      page2: (state & 0x04) !== 0      // Bit 2: PAGE2 (NOT bit 3 which is HIRES!)
    };
  }

  /**
   * Check if the emulator is in text mode
   */
  async isTextMode() {
    return (await this.getDisplayMode()).textMode;
  }

  /**
   * Check if 80-column mode is active
   */
  async is80ColumnMode() {
    return (await this.getDisplayMode()).col80;
  }

  /**
   * Convert canvas pixel coordinates to character position.
   * Mirrors the shader transform pipeline (curveUV → applyOverscan → applyScreenMargin)
   * so the mouse mapping matches the curved on-screen content.
   */
  async pixelToChar(x, y) {
    const rect = this.canvas.getBoundingClientRect();
    const params = this.renderer && this.renderer.crtParams || {};

    // Convert mouse position to normalised UV (0–1)
    let u = (x - rect.left) / rect.width;
    let v = (y - rect.top) / rect.height;

    // Apply CRT curvature (matches curveUV in crt.glsl)
    const curvature = params.curvature || 0;
    if (curvature > 0.001) {
      const cx = u - 0.5;
      const cy = v - 0.5;
      const dist = cx * cx + cy * cy;
      const distortion = dist * curvature * 0.5;
      u += cx * distortion;
      v += cy * distortion;
    }

    // Apply overscan (matches applyOverscan in crt.glsl)
    const overscan = params.overscan || 0;
    if (overscan > 0.001) {
      const borderSize = overscan * 0.1;
      const scale = 1.0 - borderSize * 2.0;
      u = (u - 0.5) / scale + 0.5;
      v = (v - 0.5) / scale + 0.5;
    }

    // Apply screen margin (matches applyScreenMargin in crt.glsl)
    const margin = params.screenMargin || 0;
    if (margin > 0.001) {
      const scale = 1.0 / (1.0 - margin * 2.0);
      u = (u - 0.5) * scale + 0.5;
      v = (v - 0.5) * scale + 0.5;
    }

    // u,v are now contentUV — map to the machine's framebuffer space, and
    // from there into the text area, which on a IIgs is inside a border
    const { width: fbWidth, height: fbHeight } = machineDisplay();
    const text = machineTextArea();
    const contentX = u * fbWidth - text.left;
    const contentY = v * fbHeight - text.top;

    const mode = await this.getDisplayMode();
    const cols = mode.col80 ? 80 : 40;
    const charWidth = mode.col80 ? this.charWidth80 : this.charWidth40;

    const col = Math.floor(contentX / charWidth);
    const row = Math.floor(contentY / this.charHeight);

    return {
      row: Math.max(0, Math.min(this.rows - 1, row)),
      col: Math.max(0, Math.min(cols - 1, col))
    };
  }

  /**
   * Normalize selection so start is always before end
   * @returns {{startRow, startCol, endRow, endCol}} Normalized coordinates
   */
  normalizeSelection() {
    if (!this.selectionStart || !this.selectionEnd) {
      return null;
    }

    let startRow = this.selectionStart.row;
    let startCol = this.selectionStart.col;
    let endRow = this.selectionEnd.row;
    let endCol = this.selectionEnd.col;

    if (startRow > endRow || (startRow === endRow && startCol > endCol)) {
      [startRow, endRow] = [endRow, startRow];
      [startCol, endCol] = [endCol, startCol];
    }

    return { startRow, startCol, endRow, endCol };
  }

  /**
   * Get selected text as a string (delegates to C++ for screen memory reading and decoding)
   */
  async getSelectedText() {
    const sel = this.normalizeSelection();
    if (!sel) return '';

    const resultPtr = await this.wasmModule._readScreenText(
      sel.startRow, sel.startCol, sel.endRow, sel.endCol
    );
    return resultPtr ? await this.wasmModule.UTF8ToString(resultPtr) : '';
  }

  /**
   * Copy selected text to clipboard
   */
  async copyToClipboard() {
    const text = await this.getSelectedText();
    if (!text) return false;

    try {
      await navigator.clipboard.writeText(text);
      this.showCopyFeedback();
      return true;
    } catch (err) {
      console.error('Failed to copy text:', err);
      return false;
    }
  }

  /**
   * Show visual feedback when text is copied
   */
  showCopyFeedback() {
    const ctx = this.overlayCtx;
    const copyFlashColor = getComputedStyle(document.documentElement)
      .getPropertyValue('--selection-copy-flash').trim() || 'rgba(63, 185, 80, 0.5)';
    ctx.fillStyle = copyFlashColor;
    this.drawSelectionHighlight().then(() => {
      setTimeout(() => {
        this.drawSelectionHighlight();
      }, 150);
    });
  }

  /**
   * Draw the selection highlight on the overlay
   */
  async drawSelectionHighlight() {
    const ctx = this.overlayCtx;
    ctx.clearRect(0, 0, this.overlay.width, this.overlay.height);

    const mode = await this.getDisplayMode();
    if (!mode.textMode) { this.uploadOverlay(); return; }

    const sel = this.normalizeSelection();
    if (!sel) { this.uploadOverlay(); return; }

    const cols = mode.col80 ? 80 : 40;
    const charWidth = mode.col80 ? this.charWidth80 : this.charWidth40;

    const highlightColor = getComputedStyle(document.documentElement)
      .getPropertyValue('--selection-highlight').trim() || 'rgba(88, 166, 255, 0.35)';
    ctx.fillStyle = highlightColor;
    const text = machineTextArea();

    for (let row = sel.startRow; row <= sel.endRow; row++) {
      const colStart = (row === sel.startRow) ? sel.startCol : 0;
      const colEnd = (row === sel.endRow) ? sel.endCol : cols - 1;

      const x = text.left + colStart * charWidth;
      const y = text.top + row * this.charHeight;
      const width = (colEnd - colStart + 1) * charWidth;
      const height = this.charHeight;

      ctx.fillRect(x, y, width, height);
    }

    this.uploadOverlay();
  }

  uploadOverlay() {
    if (this.renderer && this.renderer.updateSelectionTexture) {
      this.renderer.updateSelectionTexture(this.overlay);
    }
  }

  /**
   * Move overlay to the canvas's current parent and re-measure.
   * Called when the canvas is reparented (e.g. for full-page mode).
   */
  reattach() {
    // Overlay is offscreen; nothing to reparent
  }

  /**
   * Clear the selection
   */
  clearSelection() {
    this.selectionStart = null;
    this.selectionEnd = null;
    this.isSelecting = false;

    if (this.overlayCtx) {
      this.overlayCtx.clearRect(0, 0, this.overlay.width, this.overlay.height);
      this.uploadOverlay();
    }
  }

  // Event handlers

  async onMouseDown(e) {
    if (!await this.isTextMode()) return;
    if (e.button !== 0) return;

    const pos = await this.pixelToChar(e.clientX, e.clientY);
    this.selectionStart = pos;
    this.selectionEnd = pos;
    this.isSelecting = true;

    // Add document-level listeners to track selection even outside canvas
    document.addEventListener('mousemove', this.boundOnMouseMove);
    document.addEventListener('mouseup', this.boundOnMouseUp);

    await this.drawSelectionHighlight();
    e.preventDefault();
  }

  async onMouseMove(e) {
    if (!this.isSelecting) return;
    if (!await this.isTextMode()) {
      this.clearSelection();
      return;
    }

    const pos = await this.pixelToChar(e.clientX, e.clientY);
    this.selectionEnd = pos;

    await this.drawSelectionHighlight();
  }

  onMouseUp(e) {
    if (!this.isSelecting) return;

    this.isSelecting = false;

    // Remove document-level listeners
    document.removeEventListener('mousemove', this.boundOnMouseMove);
    document.removeEventListener('mouseup', this.boundOnMouseUp);

    // Single click (no drag) - clear selection
    if (this.selectionStart && this.selectionEnd &&
        this.selectionStart.row === this.selectionEnd.row &&
        this.selectionStart.col === this.selectionEnd.col) {
      this.clearSelection();
    }
  }

  onKeyDown(e) {
    // Ctrl+C / Cmd+C to copy selection
    // Use e.code for reliable detection across platforms
    const isCopyKey = e.code === 'KeyC' || e.key === 'c' || e.key === 'C';
    if ((e.ctrlKey || e.metaKey) && isCopyKey) {
      if (this.selectionStart && this.selectionEnd) {
        this.copyToClipboard();
        e.preventDefault();
        e.stopPropagation();  // Prevent key from reaching emulator
        return;
      }
    }

    // Escape to clear selection
    if (e.key === 'Escape' && this.selectionStart) {
      this.clearSelection();
      e.stopPropagation();  // Prevent key from reaching emulator
    }
  }

  async onContextMenu(e) {
    if (!await this.isTextMode()) return;

    if (this.selectionStart && this.selectionEnd) {
      e.preventDefault();
      this.showContextMenu(e.clientX, e.clientY);
    }
  }

  /**
   * Show a simple context menu with copy option
   */
  showContextMenu(x, y) {
    const existing = document.querySelector('.text-select-context-menu');
    if (existing) existing.remove();

    const menu = document.createElement('div');
    menu.className = 'text-select-context-menu';
    const isMac = navigator.platform.toUpperCase().indexOf('MAC') >= 0;
    const copyShortcut = isMac ? '⌘C' : 'Ctrl+C';
    menu.innerHTML = `
      <button class="context-menu-item" data-action="copy">
        <span>Copy</span>
        <span class="shortcut">${copyShortcut}</span>
      </button>
      <button class="context-menu-item" data-action="select-all">
        <span>Select All</span>
      </button>
    `;

    // Position menu, ensuring it stays within viewport
    const menuWidth = 150;
    const menuHeight = 70;
    const left = Math.min(x, window.innerWidth - menuWidth - 10);
    const top = Math.min(y, window.innerHeight - menuHeight - 10);

    menu.style.left = left + 'px';
    menu.style.top = top + 'px';

    document.body.appendChild(menu);

    const handleClick = (e) => {
      const item = e.target.closest('.context-menu-item');
      if (!item) return;

      const action = item.dataset.action;
      if (action === 'copy') {
        this.copyToClipboard();
      } else if (action === 'select-all') {
        this.selectAll();
      }

      menu.remove();
    };

    menu.addEventListener('click', handleClick);

    const closeMenu = (e) => {
      if (!menu.contains(e.target)) {
        menu.remove();
        document.removeEventListener('mousedown', closeMenu);
      }
    };

    setTimeout(() => {
      document.addEventListener('mousedown', closeMenu);
    }, 0);
  }

  /**
   * Select all text on screen
   */
  async selectAll() {
    if (!await this.isTextMode()) return;

    const cols = await this.is80ColumnMode() ? 80 : 40;

    this.selectionStart = { row: 0, col: 0 };
    this.selectionEnd = { row: this.rows - 1, col: cols - 1 };

    await this.drawSelectionHighlight();
  }

  /**
   * Update overlay size and position when canvas resizes
   */
  resize() {
    // Overlay is offscreen; no DOM positioning needed
  }

  /**
   * Clean up resources and remove event listeners
   */
  destroy() {
    // Remove event listeners
    this.canvas.removeEventListener('mousedown', this.boundOnMouseDown);
    document.removeEventListener('mousemove', this.boundOnMouseMove);
    document.removeEventListener('mouseup', this.boundOnMouseUp);
    document.removeEventListener('keydown', this.boundOnKeyDown, true);  // capture phase
    this.canvas.removeEventListener('contextmenu', this.boundOnContextMenu);

    // Remove any open context menu
    const menu = document.querySelector('.text-select-context-menu');
    if (menu) menu.remove();
  }
}
