/*
 * keyboard.hpp - Apple IIe keyboard input handling
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace a2e {

/**
 * Keyboard - Handles keyboard input translation for the Apple II family
 *
 * Translates raw browser keycodes to Apple II ASCII codes, handling shift,
 * control, and caps lock modifiers, and tracks the two Apple keys.
 *
 * The core only ever hears about the Apple keys as the two Alt keys: left is
 * Open Apple and right is Closed Apple. Which *host* key is which is the
 * host's decision (on a IIgs the host sends ⌘ as the left Alt); the Meta keys
 * themselves are ignored here, so a Windows key or a ⌘ that the host chose to
 * leave to the browser can never press an Apple key by accident.
 */
class Keyboard {
public:
  // Callback type for sending translated keys to emulator
  using KeyCallback = std::function<void(int)>;

  /**
   * Which physical key produced an event, mirroring DOM_KEY_LOCATION_*.
   * Both Alt keys share one keycode, so this is the only thing separating
   * Open Apple from Closed Apple.
   */
  enum KeyLocation { LOCATION_STANDARD = 0, LOCATION_LEFT = 1, LOCATION_RIGHT = 2 };

  Keyboard();

  /**
   * Set callback for when a key is translated
   * @param callback Function to call with translated Apple II keycode
   */
  void setKeyCallback(KeyCallback callback) { keyCallback_ = std::move(callback); }

  /**
   * Handle a raw key down event from the browser
   * @param browserKeycode The browser's keycode (e.g., 65 for 'A')
   * @param shift Shift key is pressed
   * @param ctrl Control key is pressed
   * @param alt Any Alt/Option key is still held (the Apple keys)
   * @param meta Any Meta/Command key is held; ignored, see the class comment
   * @param capsLock Caps Lock is active
   * @param keyLocation Which side of the keyboard the key is on
   * @return The translated Apple II keycode, or -1 if not mapped
   */
  int handleKeyDown(int browserKeycode, bool shift, bool ctrl, bool alt,
                    bool meta, bool capsLock, int keyLocation = LOCATION_STANDARD);

  /**
   * Handle a raw key up event from the browser
   * @param browserKeycode The browser's keycode
   * @param shift Shift key is pressed
   * @param ctrl Control key is pressed
   * @param alt Any Alt/Option key is still held
   * @param meta Any Meta/Command key is held; ignored
   * @param keyLocation Which side of the keyboard the key is on
   */
  void handleKeyUp(int browserKeycode, bool shift, bool ctrl, bool alt,
                   bool meta, int keyLocation = LOCATION_STANDARD);

  /**
   * Is any Apple II key physically held down?
   *
   * This is AKD — the any-key-down line the //e reports in bit 7 of $C010.
   * It is deliberately not affected by Shift, Control, Caps Lock or the Apple
   * buttons: those are separate lines on real hardware, not keys in the
   * matrix. Only keys that translate to an Apple II code count.
   */
  bool isAnyKeyDown() const { return keysHeldCount_ > 0; }

  /**
   * Get the current Open Apple (left Alt) button state
   */
  bool isOpenApplePressed() const { return openApplePressed_; }

  /**
   * Get the current Closed Apple (right Alt) button state
   */
  bool isClosedApplePressed() const { return closedApplePressed_; }

  /**
   * Whether the machine's keyboard can type lower case at all.
   *
   * An unmodified II+ keyboard has no lower case: its encoder produces
   * upper case whatever Shift and the (absent) Caps Lock say, and Applesoft on
   * it rejects a lower-case keyword. With this set, letters are always sent
   * as upper case; nothing else about the translation changes.
   */
  void setUppercaseOnly(bool uppercaseOnly) { uppercaseOnly_ = uppercaseOnly; }
  bool isUppercaseOnly() const { return uppercaseOnly_; }

  /**
   * Release every modifier the host may be holding.
   *
   * A key held while the host loses focus never delivers its key-up, which
   * would otherwise leave an Apple button latched until it was pressed again.
   */
  void releaseModifiers() {
    altLeftDown_ = false;
    altRightDown_ = false;
    syncAppleButtons();
    // Ordinary keys go the same way, and for the same reason: a key held while
    // the host loses focus never delivers its key-up, which would otherwise
    // leave AKD asserted for the rest of the session.
    keyHeld_.fill(false);
    keysHeldCount_ = 0;
  }

  /**
   * Reset keyboard state (clears modifier key states)
   */
  void reset() { releaseModifiers(); }

private:
  /**
   * Translate a browser keycode to base Apple II ASCII
   * @param browserKeycode The browser's keycode
   * @return Base ASCII code, or -1 if not mapped
   */
  int translateKeycode(int browserKeycode) const;

  /**
   * Apply shift modifier to a keycode
   * @param browserKeycode Original browser keycode
   * @param baseKey Base ASCII code
   * @return Shifted ASCII code
   */
  int applyShift(int browserKeycode, int baseKey) const;

  /**
   * Apply control modifier to a keycode
   * @param key ASCII code: a letter, or one of @ [ \ ] ^ _
   * @return Control character (0x00-0x1F); anything else is unchanged
   */
  int applyControl(int key) const;

  /** Recompute the Apple buttons from the modifier keys currently held. */
  void syncAppleButtons() {
    openApplePressed_ = altLeftDown_;
    closedApplePressed_ = altRightDown_;
  }

  /** Record a key as held / released, keeping the count in step. */
  void setKeyHeld(int browserKeycode, bool held) {
    if (browserKeycode < 0 || browserKeycode > 255) return;
    if (keyHeld_[browserKeycode] == held) return;  // auto-repeat, or a stray up
    keyHeld_[browserKeycode] = held;
    keysHeldCount_ += held ? 1 : -1;
  }

  KeyCallback keyCallback_;

  // Which ordinary keys are physically down, indexed by browser keycode, with
  // a running count so AKD is a comparison rather than a scan. Tracking is by
  // keycode so a key-up always clears the key it names, whatever the modifier
  // state was when it was pressed.
  std::array<bool, 256> keyHeld_{};
  int keysHeldCount_ = 0;

  // Which modifiers are physically down. The Apple button states are derived
  // from these rather than being toggled directly, so a key-up that names the
  // wrong side cannot leave a button stuck on.
  bool altLeftDown_ = false;
  bool altRightDown_ = false;

  bool openApplePressed_ = false;
  bool closedApplePressed_ = false;

  bool uppercaseOnly_ = false;
};

/**
 * Convert a Unicode character code to an Apple II key code (for paste input).
 * @param charCode Unicode code point
 * @return Apple II key code, or -1 if not mappable
 */
int charToAppleKey(int charCode);

} // namespace a2e
