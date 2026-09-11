/*
 * iigs_adb.cpp - The ADB microcontroller, as the 65816 sees it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_adb.hpp"

namespace a2e::iigs {

namespace {
// The controller's commands, with how many bytes follow and how many come
// back. Only the ones the firmware uses on the way up are named; anything else
// is taken, acknowledged and ignored, which is closer to a real controller
// than refusing would be.
constexpr uint8_t CMD_ABORT = 0x01;
constexpr uint8_t CMD_RESET_KEYBOARD = 0x02;
constexpr uint8_t CMD_FLUSH_KEYBOARD = 0x03;
constexpr uint8_t CMD_SET_MODES = 0x04;
constexpr uint8_t CMD_CLEAR_MODES = 0x05;
constexpr uint8_t CMD_SET_CONFIG = 0x06;
constexpr uint8_t CMD_SYNC = 0x07;
constexpr uint8_t CMD_WRITE_MEMORY = 0x08;
constexpr uint8_t CMD_READ_MEMORY = 0x09;
constexpr uint8_t CMD_READ_MODES = 0x0A;
constexpr uint8_t CMD_READ_CONFIG = 0x0B;
constexpr uint8_t CMD_READ_ERROR = 0x0C;
constexpr uint8_t CMD_VERSION = 0x0D;
constexpr uint8_t CMD_READ_CHARSETS = 0x0E;
constexpr uint8_t CMD_READ_LAYOUTS = 0x0F;
constexpr uint8_t CMD_RESET = 0x10;

// The version the firmware is told it is talking to. Apple's own controller
// answers with its ROM revision; what matters is that it answers.
constexpr uint8_t CONTROLLER_VERSION = 0x06;
} // namespace

void IIgsADB::reset() {
  response_.clear();
  keyboard_.clear();
  mouse_.clear();
  controllerMemory_.fill(0);
  arguments_.fill(0);
  lastCommand_ = 0;
  argumentsExpected_ = 0;
  argumentsSeen_ = 0;
  modifiers_ = 0;
  modes_ = 0;
  configuration_.fill(0);
}

uint8_t IIgsADB::readStatus() const {
  uint8_t status = 0;

  // Bit 5 is what the firmware polls for before every read, and it means the
  // controller has a byte waiting. It is also set when there is nothing in
  // particular to say: a controller that never raised it would hang the
  // machine on the first command that expects no answer, and a real one is
  // ready to be read at any time — what comes back is then whatever it last
  // had, which is what an empty queue returns here.
  status |= STATUS_DATA_AVAILABLE;

  if (!keyboard_.empty()) status |= STATUS_KEYBOARD_DATA;
  if (!mouse_.empty()) status |= STATUS_MOUSE_DATA;

  // Bit 4 says a command is still being taken. This controller finishes
  // instantly, so it is only set between the command byte and its arguments.
  if (argumentsSeen_ < argumentsExpected_) status |= STATUS_COMMAND_FULL;

  return status;
}

uint8_t IIgsADB::readData() {
  if (response_.empty()) return 0x00;
  const uint8_t value = response_.front();
  response_.pop_front();
  return value;
}

uint8_t IIgsADB::readMouseData() {
  if (mouse_.empty()) return 0x00;
  const uint8_t value = mouse_.front();
  mouse_.pop_front();
  return value;
}

void IIgsADB::queueMouse(uint8_t x, uint8_t y) {
  // Two bytes, X then Y, which is the order the firmware reads them in.
  mouse_.push_back(x);
  mouse_.push_back(y);
}

void IIgsADB::writeCommand(uint8_t value) {
  if (argumentsSeen_ < argumentsExpected_) {
    arguments_[argumentsSeen_++] = value;
    if (argumentsSeen_ == argumentsExpected_) completeCommand();
    return;
  }
  beginCommand(value);
}

void IIgsADB::beginCommand(uint8_t code) {
  lastCommand_ = code;
  argumentsSeen_ = 0;
  argumentsExpected_ = 0;

  switch (code) {
  case CMD_SET_MODES:
  case CMD_CLEAR_MODES:
  case CMD_READ_MEMORY:
    argumentsExpected_ = 1;
    break;
  case CMD_WRITE_MEMORY:
    argumentsExpected_ = 2;
    break;
  case CMD_SET_CONFIG:
    argumentsExpected_ = 3;
    break;
  case CMD_SYNC:
    // Four bytes of modes and configuration on a ROM 01. A ROM 3's controller
    // takes eight, which is one of the places the two machines differ.
    argumentsExpected_ = 4;
    break;
  default:
    break;
  }

  if (argumentsExpected_ == 0) completeCommand();
}

void IIgsADB::completeCommand() {
  switch (lastCommand_) {
  case CMD_SYNC:
    modes_ = arguments_[0];
    configuration_[0] = arguments_[1];
    configuration_[1] = arguments_[2];
    configuration_[2] = arguments_[3];
    break;

  case CMD_SET_MODES:
    modes_ |= arguments_[0];
    break;
  case CMD_CLEAR_MODES:
    modes_ &= static_cast<uint8_t>(~arguments_[0]);
    break;
  case CMD_SET_CONFIG:
    configuration_[0] = arguments_[0];
    configuration_[1] = arguments_[1];
    configuration_[2] = arguments_[2];
    break;

  case CMD_WRITE_MEMORY:
    controllerMemory_[arguments_[0]] = arguments_[1];
    break;
  case CMD_READ_MEMORY:
    response_.push_back(controllerMemory_[arguments_[0]]);
    break;

  case CMD_READ_MODES:
    response_.push_back(modes_);
    break;
  case CMD_READ_CONFIG:
    response_.push_back(configuration_[0]);
    response_.push_back(configuration_[1]);
    response_.push_back(configuration_[2]);
    break;
  case CMD_READ_ERROR:
    response_.push_back(0x00); // Nothing has gone wrong
    break;
  case CMD_VERSION:
    response_.push_back(CONTROLLER_VERSION);
    break;
  case CMD_READ_CHARSETS:
  case CMD_READ_LAYOUTS:
    response_.push_back(0x00);
    response_.push_back(0x00);
    break;

  case CMD_ABORT:
  case CMD_RESET:
  case CMD_RESET_KEYBOARD:
  case CMD_FLUSH_KEYBOARD:
    keyboard_.clear();
    mouse_.clear();
    response_.clear();
    break;

  default:
    // Talking to a device on the bus rather than to the controller itself.
    // Nothing is attached yet, so there is nothing to say back.
    break;
  }

  argumentsExpected_ = 0;
  argumentsSeen_ = 0;
}

} // namespace a2e::iigs
