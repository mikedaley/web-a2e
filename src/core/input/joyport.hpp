/*
 * joyport.hpp - Sirius Software Joyport (two Atari-style digital joysticks)
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <array>
#include <cstdint>

namespace a2e {

/** What is plugged into the game I/O connector. */
enum class GamePortDevice : uint8_t {
  AppleJoystick = 0, //!< The resistive paddle/joystick the machine shipped with
  SiriusJoyport = 1, //!< Sirius Joyport: two Atari CX40-style digital sticks
};

/**
 * The Sirius Joyport.
 *
 * A digital joystick has five switches and the game connector has three
 * pushbutton inputs, so the Joyport multiplexes: the annunciators choose which
 * stick and which axis pair the three inputs are currently reporting.
 *
 *   AN0   AN1   PB0 ($C061)   PB1 ($C062)   PB2 ($C063)
 *   off   off   fire 1        left 1        right 1
 *   off   on    fire 1        up 1          down 1
 *   on    off   fire 2        left 2        right 2
 *   on    on    fire 2        up 2          down 2
 *
 * **The switches are active low**, which is the one thing about the Joyport
 * that surprises code written for an Apple joystick: a line reads *high* while
 * nothing is pressed, the opposite of a pushbutton. That inversion is why the
 * Joyport cannot coexist with the Apple keys on PB0/PB1 and why the device is
 * a choice rather than an addition.
 *
 * This is not a slot card. It hangs off the 16-pin game connector, so it is
 * owned by the emulator and consulted from the pushbutton read path rather
 * than being fitted into a slot.
 */
class Joyport {
public:
  static constexpr int STICK_COUNT = 2;

  /** Bit positions within a stick's switch mask. */
  enum SwitchBit : uint8_t {
    UP = 1 << 0,
    DOWN = 1 << 1,
    LEFT = 1 << 2,
    RIGHT = 1 << 3,
    FIRE = 1 << 4,
  };
  static constexpr uint8_t SWITCH_MASK = UP | DOWN | LEFT | RIGHT | FIRE;

  /** Replace one stick's switch state (a mask of SwitchBit). */
  void setStickState(int stick, uint8_t switches) {
    if (stick >= 0 && stick < STICK_COUNT) {
      sticks_[static_cast<size_t>(stick)] = switches & SWITCH_MASK;
    }
  }

  uint8_t stickState(int stick) const {
    return (stick >= 0 && stick < STICK_COUNT)
               ? sticks_[static_cast<size_t>(stick)]
               : 0;
  }

  /** Release every switch on both sticks. */
  void reset() { sticks_.fill(0); }

  /**
   * Read one of the three pushbutton inputs as the Joyport drives it.
   *
   * @param pb   0, 1 or 2 for $C061, $C062, $C063
   * @param an0  Annunciator 0 — selects stick 2 when set
   * @param an1  Annunciator 1 — selects the vertical pair when set
   * @return 0x80 with the switch open, 0x00 with it closed (active low)
   */
  uint8_t readPushButton(int pb, bool an0, bool an1) const;

private:
  std::array<uint8_t, STICK_COUNT> sticks_ = {0, 0};
};

} // namespace a2e
