/*
 * joyport.cpp - Sirius Software Joyport (two Atari-style digital joysticks)
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "joyport.hpp"

namespace a2e {

uint8_t Joyport::readPushButton(int pb, bool an0, bool an1) const {
  const uint8_t stick = stickState(an0 ? 1 : 0);

  uint8_t bit = 0;
  switch (pb) {
  case 0:
    bit = FIRE;
    break;
  case 1:
    bit = an1 ? UP : LEFT;
    break;
  case 2:
    bit = an1 ? DOWN : RIGHT;
    break;
  default:
    return 0x80; // Nothing else on the connector, so the line floats high
  }

  return (stick & bit) ? 0x00 : 0x80;
}

} // namespace a2e
