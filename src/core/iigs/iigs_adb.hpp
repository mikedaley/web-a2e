/*
 * iigs_adb.hpp - The ADB microcontroller, as the 65816 sees it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../emulator/state_stream.hpp"

#include <array>
#include <cstddef>
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
 * **Every command has to take exactly the bytes that follow it.** The
 * firmware writes a command and then its arguments to the same address, so a
 * command whose argument count is wrong does not merely mishandle itself: the
 * bytes it failed to take are read as commands of their own, and from there
 * the two sides disagree about everything. Read-memory takes *two* bytes, not
 * one, because its address is sixteen bits; a Listen takes two; $12 and $13,
 * which Apple never documented, take two. Getting read-memory wrong was enough
 * to put a GS/OS boot in the monitor.
 *
 * **A command above $1F addresses the ADB bus rather than the controller.**
 * The high nibble is the command to put on the wire and the low nibble is the
 * device: $8n-$Bn are Listen registers 0 to 3 and $Cn-$Fn are Talk. $70-$73
 * are the exception and are the controller's own — stop polling that device.
 * A Talk is answered in a frame: a header byte with bit 7 set, whose bottom
 * three bits are one *less* than the number of bytes that follow, since the
 * firmware's read loop counts down to one after an INY. A device that has
 * nothing to say still sends the header; saying nothing at all leaves the
 * firmware in its read loop until it gives up, and the error path it unwinds
 * through lands the boot in the monitor. Register 3 is the one that matters:
 * it is how the firmware enumerates the bus, and it answers with the address
 * the device settled on and a handler that says what it is.
 *
 * Real keys and a real mouse arrive through `queueKeyboard` and `queueMouse`.
 *
 * The status register is the interesting half. The firmware polls it before
 * every exchange, and what it waits for is bit 5: the controller has a byte
 * for you. Bit 4 is the other direction — a command it has not finished with —
 * and since this one finishes instantly, that bit is only set between a
 * command and the last of its arguments.
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
  /**
   * $C025: which modifier keys are down.
   *
   * The Event Manager reads this on every event, so a machine that always
   * answered zero had no shift-click, no command-key menu shortcut and no way
   * to reach the Control Panel: its hotkey is Control-Open-Apple-Escape and
   * two thirds of that is in here.
   */
  uint8_t readModifiers() {
    const uint8_t value = modifiers_;
    modifiers_ &= static_cast<uint8_t>(~MOD_LATCH);
    return value;
  }

  /** The same, without clearing the latch, for a debugger. */
  uint8_t peekModifiers() const { return modifiers_; }

  // The bits, from Figure 6-6 of the Hardware Reference.
  static constexpr uint8_t MOD_SHIFT = 0x01;
  static constexpr uint8_t MOD_CONTROL = 0x02;
  static constexpr uint8_t MOD_CAPS_LOCK = 0x04;
  static constexpr uint8_t MOD_REPEAT = 0x08;
  static constexpr uint8_t MOD_KEYPAD = 0x10;
  static constexpr uint8_t MOD_LATCH = 0x20; // A modifier has changed
  static constexpr uint8_t MOD_OPTION = 0x40;
  static constexpr uint8_t MOD_APPLE = 0x80;

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

  /**
   * The header byte of an answer to an ADB bus transaction: bit 7 says "this
   * is the count", and the bottom three bits are how many bytes follow.
   */
  static constexpr uint8_t RESPONSE_HEADER = 0x80;

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
  /**
   * The modifier keys are held down or they are not, and the host says which.
   *
   * The latch bit is raised whenever the set changes and stays up until the
   * register is read, which is what it is for: a program polling the register
   * can tell "nothing is held" from "something was pressed and let go again
   * between two polls".
   */
  void setModifiers(uint8_t modifiers) {
    if ((modifiers & ~MOD_LATCH) != (modifiers_ & ~MOD_LATCH)) {
      modifiers |= MOD_LATCH;
    } else {
      modifiers |= modifiers_ & MOD_LATCH;
    }
    modifiers_ = modifiers;
  }

  // ===== State, for tests =====

  bool hasResponse() const { return !response_.empty(); }
  size_t responseCount() const { return response_.size(); }
  uint8_t lastCommand() const { return lastCommand_; }

  /** The controller in a save state, queues included. */
  void serialize(StateWriter &w) const;
  void deserialize(StateReader &r);

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
  void answerTalk(uint8_t address, uint8_t reg);
  void postBusResponse(const uint8_t *bytes, size_t count);
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
