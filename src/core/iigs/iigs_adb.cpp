/*
 * iigs_adb.cpp - The ADB microcontroller, as the 65816 sees it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include <algorithm>
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

// Two commands the firmware sends that Apple never documented. They each take
// two bytes and say nothing back; MESS calls them "mystery" and does the same.
constexpr uint8_t CMD_MYSTERY_12 = 0x12;
constexpr uint8_t CMD_MYSTERY_13 = 0x13;

// Above the controller's own commands, a byte addresses the ADB bus: the high
// nibble is the command the controller is to put on the wire and the low
// nibble is the device it is aimed at. $70-$73 is the odd one out and is the
// controller's own "stop polling this device".
constexpr uint8_t CMD_DISABLE_SRQ_FIRST = 0x70;
constexpr uint8_t CMD_DISABLE_SRQ_LAST = 0x73;
constexpr uint8_t BUS_LISTEN_FIRST = 0x80; // $8n-$Bn, registers 0 to 3
constexpr uint8_t BUS_LISTEN_LAST = 0xBF;
constexpr uint8_t BUS_TALK_FIRST = 0xC0; // $Cn-$Fn, registers 0 to 3
constexpr uint8_t BUS_TALK_LAST = 0xFF;

// Where the two devices this machine has sit on the bus. These are the
// addresses the firmware assigns and then talks to, and they are the same in
// MESS.
constexpr uint8_t ADDRESS_KEYBOARD = 0x02;
constexpr uint8_t ADDRESS_MOUSE = 0x03;

// A device's register 3: the address it answers to, with the service-request
// bit above it, and the handler that says what kind of device it is. $01 is
// the plain keyboard and the plain mouse.
constexpr uint8_t SRQ_ENABLE = 0x20;
constexpr uint8_t HANDLER_ID = 0x01;

// The version the firmware is told it is talking to. Apple's own controller
// answers with its ROM revision; what matters is that it answers.
constexpr uint8_t CONTROLLER_VERSION = 0x06;
} // namespace

void IIgsADB::reset() {
  response_.clear();
  keyboard_.clear();
  pendingX_ = 0;
  pendingY_ = 0;
  buttonChanged_ = false;
  reportInProgress_ = false;
  reportY_ = 0;
  controllerMemory_.fill(0);
  arguments_.fill(0);
  lastCommand_ = 0;
  argumentsExpected_ = 0;
  argumentsSeen_ = 0;
  modifiers_ = 0;
  latch_ = 0;
  mouseButton_ = false;
  anyKeyDown_ = false;
  modes_ = 0;
  interruptEnables_ = 0;
  configuration_.fill(0);
}

uint8_t IIgsADB::readStatus() const {
  uint8_t status = 0;

  // Bit 5 means the controller has put a byte in the data register. It has
  // to be true only when that is so: the interrupt manager reads this
  // register first, and a bit 5 that was always set looked to it like an ADB
  // interrupt to service every time — so the vertical-blanking interrupt
  // underneath it was never acknowledged, and the machine took three million
  // interrupts without getting anywhere.
  if (!response_.empty()) status |= STATUS_DATA_AVAILABLE;

  if (!keyboard_.empty()) status |= STATUS_KEYBOARD_DATA;
  if (hasMouseData()) status |= STATUS_MOUSE_DATA;

  // A report is two bytes, X then Y, and the flag says which is next.
  if (reportInProgress_) status |= STATUS_MOUSE_Y_NEXT;

  // Bit 4 says a command is still being taken. This controller finishes
  // instantly, so it is only set between the command byte and its arguments.
  if (argumentsSeen_ < argumentsExpected_) status |= STATUS_COMMAND_FULL;

  return status | interruptEnables_;
}

void IIgsADB::writeStatus(uint8_t value) {
  // Only the enables are the processor's to write; the rest is what the
  // controller has, and a write cannot change that.
  interruptEnables_ = static_cast<uint8_t>(value & STATUS_INTERRUPT_ENABLES);
}

bool IIgsADB::interruptPending() const {
  // Bit 5 reads as set whether or not a byte is really waiting, because the
  // firmware polls it before every read and a real controller is always
  // ready to be read. An *interrupt* is another matter: that is raised only
  // for a byte that is actually there, or a handler would be re-entered for
  // ever.
  if ((interruptEnables_ & STATUS_MOUSE_INTERRUPT) && hasMouseData()) return true;
  if ((interruptEnables_ & STATUS_KEYBOARD_INTERRUPT) && !keyboard_.empty()) return true;
  return false;
}

uint8_t IIgsADB::readData() {
  if (response_.empty()) return 0x00;
  const uint8_t value = response_.front();
  response_.pop_front();
  return value;
}

bool IIgsADB::hasMouseData() const {
  return reportInProgress_ || pendingX_ != 0 || pendingY_ != 0 || buttonChanged_;
}

uint8_t IIgsADB::readMouseData() {
  if (reportInProgress_) {
    reportInProgress_ = false;
    return reportY_;
  }
  if (!hasMouseData()) return 0x00;

  // Seven bits of signed movement with a button in the top bit, X first.
  // The two top bits are two *different* buttons: the X byte's is button 1,
  // the second button a two-button mouse has, and the Y byte's is button 0,
  // the one everybody presses — which is how GSSquared lays the report out.
  // Putting the same button in both, so the firmware would take whichever
  // it read, made every press two presses: the Finder opened a folder on a
  // single click. Button 1 is never pressed here, so its bit is always up.
  // What the seven bits cannot carry stays pending for the next report, so a
  // fast shove arrives in a few reports rather than being cut short.
  auto take = [this](int &pending, bool withButton) {
    const int clamped = pending < -63 ? -63 : (pending > 63 ? 63 : pending);
    pending -= clamped;
    uint8_t byte = static_cast<uint8_t>(clamped & 0x7F);
    // A pressed button reads as zero in the top bit, as it does everywhere
    // else on this machine.
    if (!(withButton && mouseButton_)) byte |= 0x80;
    return byte;
  };
  const uint8_t x = take(pendingX_, false);
  reportY_ = take(pendingY_, true);
  buttonChanged_ = false;
  reportInProgress_ = true;
  return x;
}

void IIgsADB::queueMouse(int deltaX, int deltaY) {
  pendingX_ += deltaX;
  pendingY_ += deltaY;
}

void IIgsADB::setMouseButton(bool pressed) {
  if (pressed == mouseButton_) return;
  mouseButton_ = pressed;
  buttonChanged_ = true;
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
    argumentsExpected_ = 1;
    break;
  case CMD_WRITE_MEMORY:
    argumentsExpected_ = 2;
    break;
  case CMD_READ_MEMORY:
    // Two bytes, not one: the address is sixteen bits, low byte first. Taking
    // one left the firmware's second byte to be read as a command of its own,
    // and the reply it was waiting for belonged to whatever that turned out
    // to be.
    argumentsExpected_ = 2;
    break;
  case CMD_MYSTERY_12:
  case CMD_MYSTERY_13:
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
    // A Listen carries the two bytes to be written into the device's
    // register; a Talk carries none, because the bytes come back.
    if (code >= BUS_LISTEN_FIRST && code <= BUS_LISTEN_LAST) {
      argumentsExpected_ = 2;
    }
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
  case CMD_READ_MEMORY: {
    // The address is sixteen bits but the memory is 256 bytes, so only the low
    // byte selects; the firmware sends zero for the high one.
    const uint16_t address =
        static_cast<uint16_t>(arguments_[0] | (arguments_[1] << 8));
    response_.push_back(controllerMemory_[address & 0xFF]);
    break;
  }

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
    // One of each is available, and it is the first. Answering zero said the
    // machine had no character set and no keyboard layout at all.
    response_.push_back(0x01);
    response_.push_back(0x00);
    break;

  case CMD_MYSTERY_12:
  case CMD_MYSTERY_13:
    // Undocumented, two bytes in, nothing back. Taking the arguments is the
    // whole of the job: what matters is that they are not read as commands.
    break;

  case CMD_ABORT:
  case CMD_RESET:
  case CMD_RESET_KEYBOARD:
  case CMD_FLUSH_KEYBOARD:
    keyboard_.clear();
    response_.clear();
    pendingX_ = 0;
    pendingY_ = 0;
    buttonChanged_ = false;
    reportInProgress_ = false;
    break;

  default:
    if (lastCommand_ >= CMD_DISABLE_SRQ_FIRST &&
        lastCommand_ <= CMD_DISABLE_SRQ_LAST) {
      // Stop polling a device for service requests. Nothing here polls, so
      // there is nothing to stop; the firmware only needs it acknowledged.
      break;
    }
    if (lastCommand_ >= BUS_TALK_FIRST && lastCommand_ <= BUS_TALK_LAST) {
      answerTalk(static_cast<uint8_t>(lastCommand_ & 0x0F),
                 static_cast<uint8_t>((lastCommand_ >> 4) & 0x03));
      break;
    }
    // A Listen, or something this controller does not know. Its arguments have
    // been taken and there is nothing to say back.
    break;
  }

  argumentsExpected_ = 0;
  argumentsSeen_ = 0;
}


void IIgsADB::answerTalk(uint8_t address, uint8_t reg) {
  // A bus transaction is answered in a frame, not as bare bytes: the firmware
  // reads until it sees a byte with bit 7 set, and that byte's bottom three
  // bits say how many follow. A device that says nothing still has to send the
  // header — without it the firmware sits in its read loop until the loop's
  // own counter runs out, reports the transaction as incomplete, and unwinds
  // through an error path that lands the boot in the monitor.
  const bool present = address == ADDRESS_KEYBOARD || address == ADDRESS_MOUSE;

  // Only register 3 has an answer here. It is how the firmware enumerates the
  // bus: the address the device settled on, with the service-request bit, and
  // the handler that says what it is.
  if (!present || reg != 3) {
    response_.push_back(RESPONSE_HEADER);
    return;
  }

  const uint8_t reply[] = {static_cast<uint8_t>(SRQ_ENABLE | address),
                           HANDLER_ID};
  postBusResponse(reply, sizeof(reply));
}

void IIgsADB::postBusResponse(const uint8_t *bytes, size_t count) {
  // The count in the header is one less than the number of bytes, because the
  // firmware's read loop counts from it *down to one* after an INY — so a
  // header of 2 has it read three bytes, not two. Answering an ADB register 3
  // with a header of 2 left it one byte short and waiting for ever.
  response_.push_back(
      static_cast<uint8_t>(RESPONSE_HEADER | (count ? count - 1 : 0)));
  for (size_t i = 0; i < count; i++) response_.push_back(bytes[i]);
}

namespace {
template <typename Q> void writeQueue(StateWriter &w, const Q &q) {
  w.u32(static_cast<uint32_t>(q.size()));
  for (uint8_t b : q) w.u8(b);
}
template <typename Q> void readQueue(StateReader &r, Q &q) {
  q.clear();
  const uint32_t n = r.u32();
  for (uint32_t i = 0; i < n && r.ok(); i++) q.push_back(r.u8());
}
} // namespace

void IIgsADB::serialize(StateWriter &w) const {
  writeQueue(w, response_);
  writeQueue(w, keyboard_);
  w.i32(pendingX_);
  w.i32(pendingY_);
  w.boolean(buttonChanged_);
  w.boolean(reportInProgress_);
  w.u8(reportY_);
  w.bytes(controllerMemory_.data(), controllerMemory_.size());
  w.bytes(arguments_.data(), arguments_.size());
  w.u8(lastCommand_);
  w.u8(argumentsExpected_);
  w.u8(argumentsSeen_);
  w.u8(modifiers_);
  w.u8(latch_);
  w.boolean(mouseButton_);
  w.boolean(anyKeyDown_);
  w.u8(modes_);
  w.u8(interruptEnables_);
  w.bytes(configuration_.data(), configuration_.size());
}

void IIgsADB::deserialize(StateReader &r) {
  readQueue(r, response_);
  readQueue(r, keyboard_);
  pendingX_ = r.i32();
  pendingY_ = r.i32();
  buttonChanged_ = r.boolean();
  reportInProgress_ = r.boolean();
  reportY_ = r.u8();
  if (const uint8_t *p = r.bytes(controllerMemory_.size()))
    std::copy(p, p + controllerMemory_.size(), controllerMemory_.begin());
  if (const uint8_t *p = r.bytes(arguments_.size()))
    std::copy(p, p + arguments_.size(), arguments_.begin());
  lastCommand_ = r.u8();
  argumentsExpected_ = r.u8();
  argumentsSeen_ = r.u8();
  modifiers_ = r.u8();
  latch_ = r.u8();
  mouseButton_ = r.boolean();
  anyKeyDown_ = r.boolean();
  modes_ = r.u8();
  interruptEnables_ = r.u8();
  if (const uint8_t *p = r.bytes(configuration_.size()))
    std::copy(p, p + configuration_.size(), configuration_.begin());
}

} // namespace a2e::iigs
