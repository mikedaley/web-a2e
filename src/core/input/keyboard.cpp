/*
 * keyboard.cpp - Browser keycode to Apple II ASCII translation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "keyboard.hpp"

namespace a2e {

Keyboard::Keyboard() {}

int Keyboard::handleKeyDown(int browserKeycode, bool shift, bool ctrl,
                            bool alt, bool meta, bool capsLock,
                            int keyLocation) {
  (void)alt;  // Only the key-up needs to know whether any Alt is still held.
  (void)meta; // The host decides what ⌘ is, and never sends it as ⌘.
  // Track modifier keys (Apple buttons). Both Alt keys report keycode 18, so
  // the location is what separates Open Apple from Closed Apple. An
  // unspecified location is treated as the left key, which keeps Open Apple
  // working rather than silently producing Closed Apple.
  if (browserKeycode == 18) { // Alt
    if (keyLocation == LOCATION_RIGHT) {
      altRightDown_ = true;
    } else {
      altLeftDown_ = true;
    }
    syncAppleButtons();
    return -1; // Don't generate a key
  }
  // Skip pure modifier keys. The Meta keys (91/93) are among them: which host
  // key is an Apple key is the host's decision, and it sends the answer as an
  // Alt key, so a Meta key that reaches here is one the host left alone.
  if (browserKeycode == 16 || browserKeycode == 17 ||
      browserKeycode == 91 || browserKeycode == 93) {
    return -1;
  }

  // Translate browser keycode to base Apple II code
  int appleKey = translateKeycode(browserKeycode);
  if (appleKey < 0) {
    return -1; // Not a mapped key
  }

  // From here the key is one the Apple keyboard has, so it asserts AKD until
  // its key-up arrives.
  setKeyHeld(browserKeycode, true);

  // Handle letters (a-z)
  if (appleKey >= 0x61 && appleKey <= 0x7A) {
    // Apply caps lock and shift
    if (uppercaseOnly_) {
      // The keyboard cannot type lower case, whatever is held.
      appleKey -= 32;
    } else if (capsLock && !shift) {
      // Caps lock on, no shift -> uppercase
      appleKey -= 32;
    } else if (!capsLock && shift) {
      // Caps lock off, shift pressed -> uppercase
      appleKey -= 32;
    }
    // Otherwise stays lowercase
  } else if (shift || (ctrl && (browserKeycode == 50 || browserKeycode == 54 ||
                                browserKeycode == 189))) {
    // Apply shift to non-letter keys. With Control held the encoder reads
    // the 2, 6 and - keys as @, ^ and _ whether or not Shift is down, which
    // is how Ctrl-@ (NUL), Ctrl-^ ($1E) and Ctrl-_ ($1F) are typed on a //e.
    appleKey = applyShift(browserKeycode, appleKey);
  }

  // Apply control modifier (produces control characters)
  if (ctrl) {
    appleKey = applyControl(appleKey);
  }

  // Send to emulator via callback
  if (keyCallback_) {
    keyCallback_(appleKey);
  }

  return appleKey;
}

void Keyboard::handleKeyUp(int browserKeycode, bool shift, bool ctrl,
                           bool alt, bool meta, int keyLocation) {
  (void)shift;
  (void)ctrl;
  (void)meta;

  // Track modifier keys
  if (browserKeycode == 18) { // Alt
    // Hosts do not reliably say which Alt went up. Two things go wrong, both
    // observed on macOS browsers, which synthesise Option key events from the
    // modifier flag rather than the physical key:
    //
    //  - the location on a key-up can name the wrong side, so it cannot be
    //    trusted to decide which button to clear;
    //  - releasing one of two held Option keys produces no event at all,
    //    because the flag does not change while the other is still down.
    //
    // The second is not recoverable: the release never reaches us, so that
    // button stays down until the last Alt comes up. Do not try to work around
    // it here — the information does not exist.
    //
    // What is dependable is `alt`, which says whether any Alt remains held.
    // When it is clear, both sides are released regardless of what was tracked,
    // so state always converges once the keys are actually up.
    if (!alt) {
      altLeftDown_ = false;
      altRightDown_ = false;
    } else if (keyLocation == LOCATION_RIGHT) {
      altRightDown_ = false;
    } else {
      altLeftDown_ = false;
    }
    syncAppleButtons();
    return;
  }
  setKeyHeld(browserKeycode, false);
}

int Keyboard::translateKeycode(int browserKeycode) const {
  // Letters A-Z (browser codes 65-90) -> lowercase a-z (0x61-0x7A)
  if (browserKeycode >= 65 && browserKeycode <= 90) {
    return browserKeycode + 32; // Convert to lowercase
  }

  // Numbers 0-9 (browser codes 48-57) -> ASCII 0x30-0x39
  if (browserKeycode >= 48 && browserKeycode <= 57) {
    return browserKeycode;
  }

  // Numeric keypad digits (browser codes 96-105). A Platinum //e, a //c and a
  // IIgs have a keypad, and it types the same characters as the number row.
  if (browserKeycode >= 96 && browserKeycode <= 105) {
    return browserKeycode - 96 + 0x30;
  }

  // Special keys
  switch (browserKeycode) {
  case 13:
    return 0x0D; // Enter -> CR
  case 8:
    return 0x08; // Backspace -> Left arrow (delete)
  case 27:
    return 0x1B; // Escape
  case 32:
    return 0x20; // Space
  case 9:
    return 0x09; // Tab
  case 46:
    return 0x7F; // Delete (forward delete) -> the Apple's DELETE key

  // Numeric keypad operators
  case 106:
    return 0x2A; // *
  case 107:
    return 0x2B; // +
  case 109:
    return 0x2D; // -
  case 110:
    return 0x2E; // .
  case 111:
    return 0x2F; // /

  // Arrow keys
  case 37:
    return 0x08; // Left arrow
  case 38:
    return 0x0B; // Up arrow
  case 39:
    return 0x15; // Right arrow
  case 40:
    return 0x0A; // Down arrow

  // Punctuation (US keyboard layout)
  case 188:
    return 0x2C; // Comma
  case 190:
    return 0x2E; // Period
  case 191:
    return 0x2F; // Slash
  case 186:
    return 0x3B; // Semicolon
  case 222:
    return 0x27; // Quote
  case 219:
    return 0x5B; // Left bracket
  case 221:
    return 0x5D; // Right bracket
  case 220:
    return 0x5C; // Backslash
  case 189:
    return 0x2D; // Minus
  case 187:
    return 0x3D; // Equals
  case 192:
    return 0x60; // Backtick

  default:
    return -1; // Not mapped
  }
}

int Keyboard::applyShift(int browserKeycode, int baseKey) const {
  // Number row shifted symbols
  switch (browserKeycode) {
  case 48:
    return 0x29; // 0 -> )
  case 49:
    return 0x21; // 1 -> !
  case 50:
    return 0x40; // 2 -> @
  case 51:
    return 0x23; // 3 -> #
  case 52:
    return 0x24; // 4 -> $
  case 53:
    return 0x25; // 5 -> %
  case 54:
    return 0x5E; // 6 -> ^
  case 55:
    return 0x26; // 7 -> &
  case 56:
    return 0x2A; // 8 -> *
  case 57:
    return 0x28; // 9 -> (

  // Punctuation shifted
  case 188:
    return 0x3C; // , -> <
  case 190:
    return 0x3E; // . -> >
  case 191:
    return 0x3F; // / -> ?
  case 186:
    return 0x3A; // ; -> :
  case 222:
    return 0x22; // ' -> "
  case 219:
    return 0x7B; // [ -> {
  case 221:
    return 0x7D; // ] -> }
  case 220:
    return 0x7C; // \ -> |
  case 189:
    return 0x5F; // - -> _
  case 187:
    return 0x2B; // = -> +
  case 192:
    return 0x7E; // ` -> ~

  default:
    return baseKey;
  }
}

int Keyboard::applyControl(int key) const {
  // Convert a-z to Ctrl+A-Z (0x01-0x1A)
  if (key >= 0x61 && key <= 0x7A) {
    return key - 0x60;
  }
  // Convert @ A-Z [ \ ] ^ _ to 0x00-0x1F, as the keyboard encoder does:
  // Ctrl-@ is NUL, Ctrl-[ is Escape, Ctrl-^ is 0x1E and Ctrl-_ is 0x1F.
  // Shift is applied first, so Ctrl-Shift-2 arrives here as '@'.
  if (key >= 0x40 && key <= 0x5F) {
    return key - 0x40;
  }
  return key;
}

int charToAppleKey(int charCode) {
  // Newline/CR -> CR
  if (charCode == 0x0A || charCode == 0x0D) {
    return 0x0D;
  }
  // Tab
  if (charCode == 0x09) {
    return 0x09;
  }
  // Printable ASCII (space through tilde)
  if (charCode >= 0x20 && charCode <= 0x7E) {
    return charCode;
  }
  // Not mappable
  return -1;
}

} // namespace a2e
