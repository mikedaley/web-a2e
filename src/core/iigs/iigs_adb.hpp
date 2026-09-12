/*
 * iigs_adb.hpp - The ADB microcontroller, as the 65816 sees it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <array>
#include <cstdint>
#include <deque>

namespace a2e::iigs {

/**
 * IIgsADB - the keyboard and mouse GLU at $C024-$C027
 *
 * A //e's keyboard is wired to the machine. A IIgs's is on a bus with a
 * microcontroller in the way: the 65816 writes a command byte to $C026, any
 * arguments after it, and reads whatever the controller sends back through the
 * same address, with $C027 saying whose turn it is. The //e's own keyboard
 * registers still work — $C000 and $C010 are still there — because the GLU
 * fills them in on the Mega II's behalf.
 *
 * **What this is for, today, is getting the machine past its own self-test.**
 * The firmware syncs the controller, asks its version, reads a few bytes of
 * its memory and sets its modes before it will draw anything, and a IIgs whose
 * ADB never answers stops with a fatal error before the splash screen. So the
 * commands are decoded, their arguments consumed, and plausible answers
 * queued. Real keys and a real mouse arrive later, through `queueKeyboard` and
 * `queueMouse`, and the queues are here already so that the shape does not
 * have to change when they do.
 *
 * The status register is the interesting half. The firmware polls it before
 * every exchange, and what it waits for is bit 5: the controller has a byte
 * for you. Bit 4 is the other direction — a command it has not finished with —
 * and since this one finishes instantly, that bit is never set for long.
 */
class IIgsADB {
public:
  IIgsADB() { reset(); }

  void reset();

  // ===== The four registers =====

  /**
   * $C024: the mouse's movement, a byte at a time — X then Y, seven bits of
   * signed movement each, with button 1 in the X byte's top bit and button 0
   * in the Y byte's. One button per byte: the same button in both was two
   * presses to the firmware.
   */
  uint8_t readMouseData();

  /** $C025: which modifier keys are down. */
  uint8_t readModifiers() const { return modifiers_; }

  /** $C026: the controller's answers on the way out, commands on the way in. */
  uint8_t readData();
  void writeCommand(uint8_t value);

  /**
   * $C027: whose turn it is, and which of them may interrupt.
   *
   * Bit 7 says the mouse register is full and bit 6 lets that interrupt;
   * bit 2 says the keyboard register is full and bit 3 lets that interrupt.
   * The full bits are the controller's and the enables the processor's, and
   * the interrupt line is the AND of each pair, ORed together. Bit 5 says the
   * data register has an answer, bit 4 that a command is still being taken,
   * and bit 1 whether the next mouse byte is X or Y.
   *
   * GS/OS runs its mouse on this: it sets bit 6 and waits for the interrupt,
   * and a machine that never raised one had a Finder whose pointer never
   * moved however much the mouse did.
   */
  uint8_t readStatus() const;
  void writeStatus(uint8_t value);

  /** Whether the controller is holding the interrupt line down. */
  bool interruptPending() const;

  // ===== Input, for when there is somebody typing =====

  /**
   * A key, as the machine will read it.
   *
   * On a //e the keyboard is wired to the machine and $C000 is the key. On a
   * IIgs the controller is in the way, and one of the things it does is fill
   * that same register in on the Mega II's behalf — which is why //e software
   * reads the keyboard on a IIgs without knowing there is a microcontroller
   * anywhere. So a key goes two places: into the queue a IIgs-aware program
   * reads through $C026, and into the latch $C000 reports.
   */
  void queueKeyboard(uint8_t keycode) {
    keyboard_.push_back(keycode);
    latch_ = static_cast<uint8_t>(keycode | 0x80); // with the strobe set
  }

  /** $C000: the key and its strobe, as a //e would read them. */
  uint8_t keyboardLatch() const { return latch_; }

  /** $C010: the strobe is cleared, and the key stays readable without it. */
  void clearKeyboardStrobe() {
    latch_ &= 0x7F;
    if (!keyboard_.empty()) keyboard_.pop_front();
  }

  /** Whether a key is physically held, which is a different line entirely. */
  void setAnyKeyDown(bool down) { anyKeyDown_ = down; }
  bool isAnyKeyDown() const { return anyKeyDown_; }
  /**
   * Mouse movement, as the host sees it.
   *
   * The host sends every twitch it gets, hundreds a second on a fast mouse,
   * and each one is *added* to what is waiting rather than queued behind it.
   * The controller makes a report only when the processor comes to read one,
   * and that report carries as much of the movement as its seven bits will —
   * so a pointer that has fallen behind catches up in one report rather than
   * crawling through a queue of tiny ones, which is what "choppy" was.
   */
  void queueMouse(int deltaX, int deltaY);

  /**
   * The button. A change is a report of its own, with no movement in it: a
   * real mouse says so when it is clicked without being moved, and a Finder
   * that only heard about the button when the pointer happened to be moving
   * could not be clicked on anything.
   */
  void setMouseButton(bool pressed);
  bool isMouseButtonPressed() const { return mouseButton_; }

  /** Whether there is a mouse report waiting or in the middle of being read. */
  bool hasMouseData() const;
  void setModifiers(uint8_t modifiers) { modifiers_ = modifiers; }

  // ===== State, for tests =====

  bool hasResponse() const { return !response_.empty(); }
  size_t responseCount() const { return response_.size(); }
  uint8_t lastCommand() const { return lastCommand_; }

  // The layout the ROM's own diagnostic checks: get bit 4 wrong and the
  // machine stops at "Fatal system error-> 0911" before drawing anything.
  static constexpr uint8_t STATUS_MOUSE_DATA = 0x80;
  static constexpr uint8_t STATUS_MOUSE_INTERRUPT = 0x40;
  static constexpr uint8_t STATUS_DATA_AVAILABLE = 0x20;
  static constexpr uint8_t STATUS_COMMAND_FULL = 0x10;
  static constexpr uint8_t STATUS_KEYBOARD_INTERRUPT = 0x08;
  static constexpr uint8_t STATUS_KEYBOARD_DATA = 0x04;
  static constexpr uint8_t STATUS_MOUSE_Y_NEXT = 0x02;
  static constexpr uint8_t STATUS_INTERRUPT_ENABLES =
      STATUS_MOUSE_INTERRUPT | STATUS_KEYBOARD_INTERRUPT;

private:
  // How many argument bytes a command takes, and what it sends back. The
  // controller is a small computer of its own and this is the part of its
  // vocabulary the firmware uses on the way up.
  struct Command {
    uint8_t code;
    uint8_t arguments;
    uint8_t responseLength;
  };

  void beginCommand(uint8_t code);
  void completeCommand();

  std::deque<uint8_t> response_;
  std::deque<uint8_t> keyboard_;

  // The mouse: movement not yet reported, whether the button has changed
  // since it was, and the report in progress. A report is two bytes, X then
  // Y, made when the X is read and finished when the Y is.
  int pendingX_ = 0;
  int pendingY_ = 0;
  bool buttonChanged_ = false;
  bool reportInProgress_ = false;
  uint8_t reportY_ = 0;

  std::array<uint8_t, 256> controllerMemory_{};
  std::array<uint8_t, 8> arguments_{};

  uint8_t lastCommand_ = 0;
  uint8_t argumentsExpected_ = 0;
  uint8_t argumentsSeen_ = 0;
  uint8_t modifiers_ = 0;
  uint8_t latch_ = 0;
  bool mouseButton_ = false;
  bool anyKeyDown_ = false;
  uint8_t modes_ = 0;
  uint8_t interruptEnables_ = 0;
  std::array<uint8_t, 3> configuration_{};
};

} // namespace a2e::iigs
