/*
 * input-handler.js - Keyboard input handling for the emulator
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

// How often to ask the core whether the paste buffer has drained. The host no
// longer meters the characters out — it only needs to notice the end, so that
// it can drop the speed boost and fire the completion callback. Long enough
// not to spam the worker, short enough that the boost ends promptly.
const PASTE_POLL_MS = 60;

import { commandKeyIsOpenApple } from "./apple-keys.js";

// Right Alt reports location 2; left reports 1, and 0 means the browser did not
// say, which we treat as left. Both sides share keyCode 18, so this is the only
// thing separating Open Apple from Closed Apple.
const KEY_ALT = 18;
const KEY_META_LEFT = 91;
const KEY_META_RIGHT = 93;
const LOCATION_LEFT = 1;
const LOCATION_RIGHT = 2;
// The Pause/Break key, and what Chrome reports for Ctrl+Pause.
const KEY_PAUSE = 19;
const KEY_CANCEL = 3;

export class InputHandler {
  constructor(wasmModule) {
    this.wasmModule = wasmModule;

    // Whether ⌘ is the Open Apple key (see apple-keys.js). Read once and again
    // on every machine change rather than on every keystroke.
    this.commandIsOpenApple = commandKeyIsOpenApple();
    // Keys pressed while ⌘ was held. macOS delivers no key-up for a key let go
    // while ⌘ is down, so they are released when ⌘ is, or they would hold the
    // Apple II's "any key down" line forever.
    this.heldUnderCommand = new Set();

    // Canvas element for focus management
    this.canvas = null;

    // Hidden input for mobile keyboard
    this.mobileInput = null;
    this.isMobile = false;

    // Paste state. The characters themselves live in the core's keyboard
    // type-ahead buffer; what is left here is only the bookkeeping around it —
    // the optional speed boost and the completion callback.
    this.pasteTimer = null;
    this.pasteSpeedUp = false; // whether we've set a speed multiplier for paste
    this.pasteMultiplier = 1;  // boost the current paste asked for
    this.savedSpeedMultiplier = 1; // speed before paste started
    this.pasteOnComplete = null;

    // Reference to joystick window for cursor-keys-as-joystick feature
    this.joystickWindow = null;
  }

  init() {
    // Detect mobile/touch devices
    this.isMobile = this.detectMobile();

    // Get canvas and make it focusable
    this.canvas = document.getElementById("screen");
    this.canvas.tabIndex = 1; // Make canvas focusable

    // Create hidden input for mobile keyboard
    if (this.isMobile) {
      this.createMobileInput();
    }

    // Focus canvas on click (or mobile input on mobile)
    this.canvas.addEventListener("click", () => {
      if (this.isMobile && this.mobileInput) {
        this.mobileInput.focus();
      } else {
        this.canvas.focus();
      }
    });

    // Also handle touch events for mobile
    this.canvas.addEventListener("touchend", (e) => {
      if (this.isMobile && this.mobileInput) {
        // Small delay to ensure touch event completes
        setTimeout(() => {
          this.mobileInput.focus();
        }, 50);
      }
    });

    // Focus canvas initially (not on mobile - wait for user tap)
    if (!this.isMobile) {
      setTimeout(() => this.canvas.focus(), 100);
    }

    // A single document-level pair of listeners, deliberately. Listening on the
    // canvas as well would double every keystroke: a key press targets the
    // focused canvas, runs the canvas listener in the target phase, then bubbles
    // to document where the guard below is true by definition — so every key
    // reached the emulator twice, and any per-key side effect (the Cursor Keys
    // joystick update, for one) ran twice with it.
    //
    // Document is the one that has to stay: it also covers the case where focus
    // has drifted to <body>, which the canvas listener never sees.
    const focusIsOurs = () =>
      document.activeElement === this.canvas ||
      document.activeElement === document.body;

    document.addEventListener("keydown", (e) => {
      if (focusIsOurs()) this.handleKeyDown(e);
    });

    document.addEventListener("keyup", (e) => {
      if (focusIsOurs()) this.handleKeyUp(e);
    });

    // A key held while switching away never delivers its keyup, which would
    // otherwise leave an Apple button latched until it was pressed again.
    window.addEventListener("blur", () => this.wasmModule._releaseModifiers());

    // Paste event listener
    document.addEventListener("paste", (e) => {
      if (
        document.activeElement === this.canvas ||
        document.activeElement === document.body
      ) {
        this.handlePaste(e);
      }
    });
  }

  /** The machine changed, and with it which host key is Open Apple. */
  applyAppleKeys() {
    this.commandIsOpenApple = commandKeyIsOpenApple();
    this.heldUnderCommand.clear();
    this.wasmModule._releaseModifiers();
  }

  /**
   * The key as the core should see it.
   *
   * The core knows the Apple keys as the two Alt keys: keycode 18, left for
   * Open and right for Closed. On a machine whose ⌘ is Open Apple, ⌘ is sent
   * as the left Alt and either Option as the right, and `alt` says whether
   * any of them is still held — which is what the core's key-up relies on.
   * Nothing about ⌘ reaches the core as ⌘, so no key is mistaken for a
   * browser shortcut.
   */
  translateAppleKeys(event, keyCode) {
    if (!this.commandIsOpenApple) {
      return { keyCode, alt: event.altKey, meta: event.metaKey, location: event.location || 0 };
    }
    if (keyCode === KEY_META_LEFT || keyCode === KEY_META_RIGHT) {
      return { keyCode: KEY_ALT, alt: true, meta: false, location: LOCATION_LEFT };
    }
    if (keyCode === KEY_ALT) {
      return { keyCode: KEY_ALT, alt: true, meta: false, location: LOCATION_RIGHT };
    }
    return {
      keyCode,
      alt: event.altKey || event.metaKey,
      meta: false,
      location: event.location || 0,
    };
  }

  handleKeyDown(event) {
    const rawKeyCode = event.keyCode || event.which;

    // Get modifier states
    const shift = event.shiftKey;
    const ctrl = event.ctrlKey;
    const capsLock = event.getModifierState && event.getModifierState('CapsLock');

    // Don't interfere with browser shortcuts
    if (ctrl && rawKeyCode === 82) {
      // Ctrl+R for refresh
      return;
    }

    // Ctrl+Pause/Break is Ctrl+Reset, for keyboards that have the key. A
    // Pause key reports 19, and Chrome reports Ctrl+Pause as 3 (Cancel).
    if (ctrl && (rawKeyCode === KEY_PAUSE || rawKeyCode === KEY_CANCEL)) {
      event.preventDefault();
      this.cancelPaste();
      this.wasmModule._warmReset();
      return;
    }

    // A machine that has ⌘ as its Open Apple takes every ⌘ combination the
    // browser will let go of: ⌘-letter is a menu shortcut on a IIgs, and
    // handing it to the browser instead would open a bookmark or a find bar
    // in the middle of it.
    if (this.commandIsOpenApple && (event.metaKey || rawKeyCode === KEY_META_LEFT || rawKeyCode === KEY_META_RIGHT)) {
      event.preventDefault();
      if (event.metaKey && rawKeyCode !== KEY_META_LEFT && rawKeyCode !== KEY_META_RIGHT) {
        this.heldUnderCommand.add(rawKeyCode);
      }
    }

    const { keyCode, alt, meta, location } = this.translateAppleKeys(event, rawKeyCode);

    // Alt on its own would otherwise focus the browser menu bar. Combinations
    // are left alone: Ctrl+Alt is AltGr on European layouts, and swallowing it
    // would interfere with typing accented characters.
    if (keyCode === KEY_ALT && !ctrl && !meta) {
      event.preventDefault();
    }
    // Win / Context Menu → block, do nothing. On a machine that takes ⌘ they
    // have already been translated to an Apple key and do not reach here.
    if (keyCode === KEY_META_LEFT || keyCode === KEY_META_RIGHT) {
      event.preventDefault();
      return;
    }

    // Prevent default for these keys when not using modifiers
    if (this.shouldPreventDefault(event)) {
      event.preventDefault();
    }

    // Always prevent default for printable characters when canvas has focus
    // to stop them from triggering button shortcuts
    if (document.activeElement === this.canvas) {
      event.preventDefault();
    }

    // Cursor Keys mode makes the arrows drive the joystick *as well as* the
    // keyboard — it must not swallow them. ProDOS selectors, catalog menus and
    // BASIC line editing all navigate with the arrows, and consuming them here
    // left those unusable whenever the toggle was on.
    if (this.joystickWindow) {
      this.joystickWindow.handleCursorKey(keyCode, true);
    }

    // Send raw keycode to WASM - C++ handles the translation
    this.wasmModule._handleRawKeyDown(
      keyCode, shift, ctrl, alt, meta, capsLock, location,
    );
  }

  handleKeyUp(event) {
    const rawKeyCode = event.keyCode || event.which;

    // Get modifier states
    const shift = event.shiftKey;
    const ctrl = event.ctrlKey;

    const isMetaKey = rawKeyCode === KEY_META_LEFT || rawKeyCode === KEY_META_RIGHT;
    if (this.commandIsOpenApple && isMetaKey) {
      // See heldUnderCommand: the keys let go under ⌘ never said so.
      for (const held of this.heldUnderCommand) {
        this.wasmModule._handleRawKeyUp(held, shift, ctrl, event.altKey, false, 0);
      }
      this.heldUnderCommand.clear();
    }

    // Win / Context Menu → ignore release, unless it is an Apple key here
    if (isMetaKey && !this.commandIsOpenApple) {
      return;
    }

    let { keyCode, alt, meta, location } = this.translateAppleKeys(event, rawKeyCode);
    if (this.commandIsOpenApple) {
      // Whether any Apple key is still down once this one is up. ⌘'s own
      // key-up still reports metaKey, so it is not asked about itself.
      const metaStillHeld = event.metaKey && !isMetaKey;
      const altStillHeld = event.altKey && rawKeyCode !== KEY_ALT;
      alt = metaStillHeld || altStillHeld;
    }

    // Release the joystick axis, then let the key reach the emulator as normal
    // (see the matching note in handleKeyDown).
    if (this.joystickWindow) {
      this.joystickWindow.handleCursorKey(keyCode, false);
    }

    // Send raw keycode to WASM
    this.wasmModule._handleRawKeyUp(
      keyCode, shift, ctrl, alt, meta, location,
    );
  }

  shouldPreventDefault(event) {
    const keyCode = event.keyCode || event.which;

    // Prevent default for these keys when not using modifiers
    const preventKeys = [
      8, // Backspace
      9, // Tab
      27, // Escape
      32, // Space (prevent page scroll)
      37, 38, 39, 40, // Arrow keys
    ];

    if (preventKeys.includes(keyCode) && !event.ctrlKey && !event.metaKey) {
      return true;
    }

    return false;
  }

  // Handle paste event - queues text for input at accelerated speed
  handlePaste(event) {
    event.preventDefault();

    const text = (event.clipboardData || window.clipboardData).getData("text");
    if (!text) return;

    this.queueTextInput(text, { speedMultiplier: 8 });
  }

  // Convert character to Apple II key code (for paste only)
  async charToAppleKey(char) {
    const result = await this.wasmModule._charToAppleKey(char.charCodeAt(0));
    return result >= 0 ? result : null;
  }

  // Watch the core's type-ahead buffer and tidy up when it empties.
  //
  // This is all the host does during a paste now. The characters are already
  // inside the emulator, and the machine loads the next one each time the
  // program clears the keyboard strobe, so nothing here paces the typing:
  // it only drops the speed boost and reports completion. The old version
  // ran the CPU itself in 500-cycle bursts from this thread while polling
  // _isKeyboardReady() once per character, which competed with the
  // audio-paced worker for the same emulation and cost a round trip per key.
  async pollPasteDrain() {
    // Nothing drains while the emulator is paused, so hand the boost back
    // rather than leaving the machine set to sprint when it continues.
    const paused =
      this.wasmModule._isPaused && (await this.wasmModule._isPaused());
    if (paused) {
      this.restorePasteSpeed();
    } else if (!this.pasteSpeedUp && this.pasteMultiplier > 1) {
      await this.setPasteSpeed(this.pasteMultiplier);
    }

    const pending = this.wasmModule._pastePending
      ? await this.wasmModule._pastePending()
      : 0;

    if (pending > 0) {
      this.pasteTimer = setTimeout(() => this.pollPasteDrain(), PASTE_POLL_MS);
      return;
    }

    this.finishPaste(false);
  }

  // Common exit path for a paste: drop the boost, stop polling, notify.
  finishPaste(cancelled) {
    if (typeof this.pasteTimer === "number") clearTimeout(this.pasteTimer);
    this.pasteTimer = null;
    this.pasteMultiplier = 1;
    this.restorePasteSpeed();
    if (this.pasteOnComplete) {
      const done = this.pasteOnComplete;
      this.pasteOnComplete = null;
      done(cancelled);
    }
  }

  // Set emulation speed multiplier for fast paste/input
  async setPasteSpeed(multiplier) {
    if (!this.pasteSpeedUp && this.wasmModule._setSpeedMultiplier) {
      this.savedSpeedMultiplier = this.wasmModule._getSpeedMultiplier
        ? await this.wasmModule._getSpeedMultiplier()
        : 1;
      this.wasmModule._setSpeedMultiplier(multiplier);
      this.pasteSpeedUp = true;
    }
  }

  // Set the user's baseline emulation speed (the View menu's CPU Speed).
  // While a paste boost is live the multiplier in WASM belongs to the paste,
  // so record the new baseline as the value to restore rather than writing it
  // through — otherwise restorePasteSpeed() would undo the change.
  setBaseSpeed(multiplier) {
    if (this.pasteSpeedUp) {
      this.savedSpeedMultiplier = multiplier;
      return;
    }
    if (this.wasmModule._setSpeedMultiplier) {
      this.wasmModule._setSpeedMultiplier(multiplier);
    }
  }

  // Restore emulation speed after paste completes
  restorePasteSpeed() {
    if (this.pasteSpeedUp && this.wasmModule._setSpeedMultiplier) {
      this.wasmModule._setSpeedMultiplier(this.savedSpeedMultiplier);
      this.pasteSpeedUp = false;
    }
  }

  // Cancel any pending paste operation
  cancelPaste() {
    if (this.wasmModule._clearPasteBuffer) this.wasmModule._clearPasteBuffer();
    if (this.pasteTimer === null && !this.pasteOnComplete) return;
    this.finishPaste(true); // true = cancelled
  }

  // Queue text for programmatic input (used by BasicProgramWindow)
  // speedMultiplier: emulation speed during input (1=normal, 8=8x)
  // onStart: callback when pasting begins
  // onComplete: callback when pasting finishes (or is cancelled)
  // parseTokens: when true, recognize {token} special keys (arrows, esc,
  //   enter, tab, del, backspace, space, ctrl combos). Default false so
  //   ordinary paste passes braces through literally.
  //
  // The text is handed to the core's keyboard type-ahead buffer in whole runs
  // rather than a character at a time: translation to Apple II key codes lives
  // in keyboard.cpp, which is the one place that knows the mapping, and a
  // paste of any size costs a fixed handful of round trips instead of one per
  // character.
  async queueTextInput(text, { speedMultiplier = 8, onStart = null, onComplete = null, parseTokens = false } = {}) {
    if (!text) return;

    // A second paste while one is running joins the queue behind it, but its
    // completion callback replaces the first — fire the old one as cancelled
    // rather than dropping it, so a caller awaiting it is never left hanging.
    if (this.pasteOnComplete && this.pasteOnComplete !== onComplete) {
      const stale = this.pasteOnComplete;
      this.pasteOnComplete = null;
      stale(true);
    }
    this.pasteOnComplete = onComplete;
    this.pasteMultiplier = speedMultiplier;
    await this.setPasteSpeed(speedMultiplier);

    if (parseTokens) {
      // Literal text accumulates into runs so a whole line is one call; only
      // a {token} interrupts the run and goes across as a single key code.
      let run = "";
      const flush = async () => {
        if (run) {
          await this.pushPasteText(run);
          run = "";
        }
      };

      let i = 0;
      while (i < text.length) {
        if (text[i] === "{") {
          // "{{" is a literal "{"
          if (text[i + 1] === "{") {
            run += "{";
            i += 2;
            continue;
          }
          const end = text.indexOf("}", i + 1);
          if (end > i) {
            const code = this.specialKeyToAppleCode(text.slice(i + 1, end));
            if (code !== null) {
              await flush();
              this.wasmModule._pasteKey(code);
              i = end + 1;
              continue;
            }
          }
          // Unrecognized token — fall through and treat "{" literally
        }
        run += text[i];
        i++;
      }
      await flush();
    } else {
      await this.pushPasteText(text);
    }

    // Start watching for the buffer to drain if we are not already
    if (this.pasteTimer === null) {
      if (onStart) onStart();
      this.pollPasteDrain();
    }
  }

  // Copy one run of text into the core's type-ahead buffer. Characters with
  // no Apple II equivalent are dropped by charToAppleKey() inside the core.
  async pushPasteText(text) {
    if (!this.wasmModule._pasteText) return;
    const byteLength = new TextEncoder().encode(text).length + 1;
    const ptr = await this.wasmModule._malloc(byteLength);
    if (!ptr) return;
    try {
      await this.wasmModule.stringToUTF8(text, ptr, byteLength);
      await this.wasmModule._pasteText(ptr);
    } finally {
      this.wasmModule._free(ptr);
    }
  }

  // Map a {token} name to an Apple II key code, mirroring keyboard.cpp.
  // Supports arrows, esc, enter/return/cr, tab, del, backspace, space, and
  // Ctrl combos ({ctrl-c}, {ctrl+c}, {^c}). Returns null if unrecognized.
  specialKeyToAppleCode(token) {
    const t = token.trim().toLowerCase();
    const named = {
      left: 0x08, right: 0x15, up: 0x0b, down: 0x0a,
      esc: 0x1b, escape: 0x1b,
      enter: 0x0d, return: 0x0d, cr: 0x0d,
      tab: 0x09,
      del: 0x7f, delete: 0x7f,
      bs: 0x08, backspace: 0x08,
      space: 0x20,
    };
    if (Object.prototype.hasOwnProperty.call(named, t)) return named[t];

    // Ctrl combo: {ctrl-c}, {ctrl+c}, {^c}
    const m = t.match(/^(?:ctrl[-+]|\^)(.)$/);
    if (m) {
      const c = m[1].charCodeAt(0);
      if (c >= 0x61 && c <= 0x7a) return c - 0x60; // a-z -> 0x01-0x1A
      if (c >= 0x40 && c <= 0x5f) return c & 0x1f; // @ [ \ ] ^ _ -> 0x00-0x1F
    }

    // Raw code by value: {chr:4}, {chr:$04}, {chr:0x1b} -> CHR$(N).
    // Covers control codes (e.g. Ctrl-D = {chr:4}) and any byte by number.
    const chr = t.match(/^chr:(?:(\$|0x)([0-9a-f]+)|([0-9]+))$/);
    if (chr) {
      const v = chr[3] !== undefined ? parseInt(chr[3], 10) : parseInt(chr[2], 16);
      if (!Number.isNaN(v) && v >= 0 && v <= 0xff) return v & 0x7f;
    }
    return null;
  }

  // Check if a paste operation is in progress
  isPasting() {
    return this.pasteTimer !== null;
  }

  // Detect if we're on a mobile/touch device
  detectMobile() {
    // Check for touch capability and mobile user agent
    const hasTouch = 'ontouchstart' in window || navigator.maxTouchPoints > 0;
    const isMobileUA = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
    // Also check for small screen width as a fallback
    const isSmallScreen = window.innerWidth <= 800;

    return hasTouch && (isMobileUA || isSmallScreen);
  }

  // Create hidden input element for mobile keyboard
  createMobileInput() {
    this.mobileInput = document.createElement('input');
    this.mobileInput.type = 'text';
    this.mobileInput.id = 'mobile-keyboard-input';
    this.mobileInput.autocomplete = 'off';
    this.mobileInput.autocapitalize = 'none';
    this.mobileInput.autocorrect = 'off';
    this.mobileInput.spellcheck = false;

    // Style to be invisible but still functional
    Object.assign(this.mobileInput.style, {
      position: 'absolute',
      left: '-9999px',
      top: '0',
      width: '1px',
      height: '1px',
      opacity: '0',
      pointerEvents: 'none',
      zIndex: '-1'
    });

    document.body.appendChild(this.mobileInput);

    // Handle input events from mobile keyboard
    this.mobileInput.addEventListener('input', (e) => {
      const data = e.data;
      if (data) {
        // The whole event goes across at once — an on-screen keyboard can
        // deliver several characters in one input event.
        this.sendCharToEmulator(data);
      }
      // Clear the input to be ready for next character
      this.mobileInput.value = '';
    });

    // Handle special keys via keydown
    this.mobileInput.addEventListener('keydown', (e) => {
      const keyCode = e.keyCode || e.which;

      // Handle special keys that don't generate input events
      switch (keyCode) {
        case 8:  // Backspace
        case 13: // Enter
        case 27: // Escape
        case 9:  // Tab
          e.preventDefault();
          this.handleKeyDown(e);
          break;
      }
    });

    this.mobileInput.addEventListener('keyup', (e) => {
      const keyCode = e.keyCode || e.which;

      // Handle special key releases
      switch (keyCode) {
        case 8:  // Backspace
        case 13: // Enter
        case 27: // Escape
        case 9:  // Tab
          this.handleKeyUp(e);
          break;
      }
    });

    // Handle blur - show visual feedback that keyboard is hidden
    this.mobileInput.addEventListener('blur', () => {
      this.canvas.classList.remove('keyboard-active');
    });

    // Handle focus - show visual feedback that keyboard is active
    this.mobileInput.addEventListener('focus', () => {
      this.canvas.classList.add('keyboard-active');
    });
  }

  // Send typed text to the emulator (for mobile input).
  //
  // Goes through the core's type-ahead buffer rather than writing the latch
  // directly: an on-screen keyboard can deliver several characters in one
  // input event (autocorrect, a swipe, a paste into the hidden field), and
  // writing the latch per character overwrote keys the machine had not read
  // yet. The buffer also releases AKD once the character is taken.
  async sendCharToEmulator(text) {
    await this.pushPasteText(text);
  }

  // Show mobile keyboard programmatically
  showMobileKeyboard() {
    if (this.isMobile && this.mobileInput) {
      this.mobileInput.focus();
    }
  }

  // Hide mobile keyboard
  hideMobileKeyboard() {
    if (this.isMobile && this.mobileInput) {
      this.mobileInput.blur();
    }
  }
}
