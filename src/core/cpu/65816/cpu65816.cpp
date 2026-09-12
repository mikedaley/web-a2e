/*
 * cpu65816.cpp - 65C816 core: bus, stack, addressing and operations
 *
 * The opcode dispatch lives in cpu65816_dispatch.cpp. This file is everything
 * an opcode is made of; that one is the 256 ways of arranging them.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "cpu65816.hpp"

namespace a2e {

namespace {
// A bank is 64KB, and the difference between the two kinds of 16-bit access is
// what happens at the top of one.
constexpr uint32_t BANK_MASK = 0xFF0000;
constexpr uint32_t ADDR_MASK = 0x00FFFF;

constexpr uint32_t bankOf(uint32_t address) { return address & BANK_MASK; }
constexpr uint32_t offsetOf(uint32_t address) { return address & ADDR_MASK; }
} // namespace

CPU65816::CPU65816(ReadCallback read, WriteCallback write)
    : read_(std::move(read)), write_(std::move(write)) {
  reset();
}

// ============================================================================
// Reset
// ============================================================================

void CPU65816::reset() {
  // A 65816 comes up as a 6502 and has to be asked to be anything else. Every
  // Apple IIgs boots in emulation mode and stays there until firmware runs XCE.
  e_ = true;
  p_ = FLAG816_M | FLAG816_X | FLAG816_I;
  setFlag(FLAG816_D, false);

  d_ = 0;
  dbr_ = 0;
  pbr_ = 0;
  sp_ = 0x01FF;
  applyWidthConstraints();

  stopped_ = false;
  waiting_ = false;
  irqPending_ = false;
  nmiPending_ = false;

  pc_ = readVector(VEC_E_RESET);

  cycleCount_ = 7;
  totalCycles_ += 7;
}

void CPU65816::setP(uint8_t value) {
  p_ = value;
  applyWidthConstraints();
}

void CPU65816::setEmulation(bool e) {
  e_ = e;
  applyWidthConstraints();
}

void CPU65816::applyWidthConstraints() {
  if (e_) {
    // Emulation mode is not a suggestion: the width flags read back as set
    // whatever was written to them, the index registers are eight bits, and
    // the stack lives in page one. A program that clears M in emulation mode
    // and expects a 16-bit accumulator is describing a machine that does not
    // exist.
    p_ |= FLAG816_M | FLAG816_X;
    x_ &= 0x00FF;
    y_ &= 0x00FF;
    sp_ = 0x0100 | (sp_ & 0x00FF);
    return;
  }
  if (p_ & FLAG816_X) {
    // Narrowing the index registers discards their high halves rather than
    // hiding them. This is the documented behaviour and programs rely on it:
    // SEP #$10 is how you know X is under 256.
    x_ &= 0x00FF;
    y_ &= 0x00FF;
  }
}

// ============================================================================
// Bus
// ============================================================================

uint8_t CPU65816::read8(uint32_t address) { return read_(address & 0xFFFFFF); }

void CPU65816::write8(uint32_t address, uint8_t value) {
  write_(address & 0xFFFFFF, value);
}

uint16_t CPU65816::read16(uint32_t address) {
  // Runs into the next bank at the top of one, which is what a data-bank
  // access does.
  const uint8_t lo = read8(address);
  const uint8_t hi = read8((address + 1) & 0xFFFFFF);
  return static_cast<uint16_t>(lo | (hi << 8));
}

uint16_t CPU65816::read16Wrapped(uint32_t address) {
  // Stays inside its bank, which is what direct page and stack accesses do:
  // bank zero is where they live and bank zero is where they stay.
  const uint8_t lo = read8(address);
  const uint8_t hi = read8(bankOf(address) | ((offsetOf(address) + 1) & 0xFFFF));
  return static_cast<uint16_t>(lo | (hi << 8));
}

uint16_t CPU65816::read8or16(uint32_t address, bool eightBit) {
  // See operandWrapsInBank_: the addressing mode already decided whether the
  // second byte can leave this bank.
  const bool wrapInBank = operandWrapsInBank_;
  operandWrapsInBank_ = false;

  if (eightBit) return read8(address);
  cycleCount_++; // A second byte is a second cycle
  return wrapInBank ? read16Wrapped(address) : read16(address);
}

void CPU65816::write8or16(uint32_t address, uint16_t value, bool eightBit) {
  const bool wrapInBank = operandWrapsInBank_;
  operandWrapsInBank_ = false;

  write8(address, static_cast<uint8_t>(value));
  if (eightBit) return;
  cycleCount_++;
  const uint32_t high =
      wrapInBank ? ((address & 0xFF0000) | ((address + 1) & 0xFFFF))
                 : ((address + 1) & 0xFFFFFF);
  write8(high, static_cast<uint8_t>(value >> 8));
}

uint8_t CPU65816::fetch8() {
  const uint8_t value = read8((static_cast<uint32_t>(pbr_) << 16) | pc_);
  pc_ = static_cast<uint16_t>(pc_ + 1); // Wraps inside the program bank
  return value;
}

uint16_t CPU65816::fetch16() {
  const uint8_t lo = fetch8();
  const uint8_t hi = fetch8();
  return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t CPU65816::fetch24() {
  const uint16_t low = fetch16();
  const uint8_t bank = fetch8();
  return (static_cast<uint32_t>(bank) << 16) | low;
}

// ============================================================================
// Stack
//
// Bank zero always. In emulation mode the pointer is the low eight bits and
// wraps inside page one — push past $0100 and you land back on $01FF, which is
// how a 6502 behaves and what any //e code running on this machine expects.
// ============================================================================

void CPU65816::push8(uint8_t value) {
  write8(sp_, value);
  if (e_) {
    sp_ = 0x0100 | ((sp_ - 1) & 0x00FF);
  } else {
    sp_ = static_cast<uint16_t>(sp_ - 1);
  }
}

uint8_t CPU65816::pop8() {
  if (e_) {
    sp_ = 0x0100 | ((sp_ + 1) & 0x00FF);
  } else {
    sp_ = static_cast<uint16_t>(sp_ + 1);
  }
  return read8(sp_);
}

void CPU65816::push16(uint16_t value) {
  push8(static_cast<uint8_t>(value >> 8));
  push8(static_cast<uint8_t>(value));
}

uint16_t CPU65816::pop16() {
  const uint8_t lo = pop8();
  const uint8_t hi = pop8();
  return static_cast<uint16_t>(lo | (hi << 8));
}

// The 65816's own instructions — PHD, PLD, PEA, PEI, PER, PLB, JSL, RTL —
// ignore the page-one wrap while they are running, because they were designed
// for a stack that can be anywhere and emulation mode is something they are
// merely tolerating. The pointer goes wherever sixteen bits take it and is put
// back into page one at the end, which is why PLD with the pointer at $01FE
// reads its high byte from $0200 and then leaves the pointer at $0100.
void CPU65816::push8Wide(uint8_t value) {
  write8(sp_, value);
  sp_ = static_cast<uint16_t>(sp_ - 1);
}

uint8_t CPU65816::pop8Wide() {
  sp_ = static_cast<uint16_t>(sp_ + 1);
  return read8(sp_);
}

void CPU65816::push16Wide(uint16_t value) {
  push8Wide(static_cast<uint8_t>(value >> 8));
  push8Wide(static_cast<uint8_t>(value));
}

uint16_t CPU65816::pop16Wide() {
  const uint8_t lo = pop8Wide();
  const uint8_t hi = pop8Wide();
  return static_cast<uint16_t>(lo | (hi << 8));
}

void CPU65816::normaliseStack() {
  if (e_) sp_ = 0x0100 | (sp_ & 0x00FF);
}

// ============================================================================
// Addressing
//
// Each returns a 24-bit effective address and charges its own extra cycles: a
// direct page that is not page-aligned costs one, and an index that crosses a
// page costs one while the index registers are eight bits wide.
// ============================================================================

uint32_t CPU65816::addrImmediate(bool eightBit) {
  operandWrapsInBank_ = true; // The program bank never increments
  const uint32_t address = (static_cast<uint32_t>(pbr_) << 16) | pc_;
  pc_ = static_cast<uint16_t>(pc_ + (eightBit ? 1 : 2));
  return address;
}

uint32_t CPU65816::addrDirect() {
  const uint8_t offset = fetch8();
  if (d_ & 0x00FF) cycleCount_++;
  operandWrapsInBank_ = true;
  return (d_ + offset) & 0xFFFF; // Bank zero
}

uint32_t CPU65816::addrDirectX() {
  const uint8_t offset = fetch8();
  if (d_ & 0x00FF) cycleCount_++;
  if (e_ && (d_ & 0x00FF) == 0) {
    // The 6502's zero page,X: the index wraps inside the page rather than
    // carrying out of it.
    operandWrapsInBank_ = true;
    return (d_ & 0xFF00) | ((offset + (x_ & 0xFF)) & 0xFF);
  }
  operandWrapsInBank_ = true;
  return (d_ + offset + x_) & 0xFFFF;
}

uint32_t CPU65816::addrDirectY() {
  const uint8_t offset = fetch8();
  if (d_ & 0x00FF) cycleCount_++;
  if (e_ && (d_ & 0x00FF) == 0) {
    operandWrapsInBank_ = true;
    return (d_ & 0xFF00) | ((offset + (y_ & 0xFF)) & 0xFF);
  }
  operandWrapsInBank_ = true;
  return (d_ + offset + y_) & 0xFFFF;
}

uint32_t CPU65816::addrDirectIndirect() {
  const uint32_t pointer = addrDirect();
  const uint16_t target = read16Wrapped(pointer);
  operandWrapsInBank_ = false;
  return (static_cast<uint32_t>(dbr_) << 16) | target;
}

uint32_t CPU65816::addrDirectIndirectLong() {
  const uint32_t pointer = addrDirect();
  const uint16_t low = read16Wrapped(pointer);
  const uint8_t bank = read8((pointer + 2) & 0xFFFF);
  operandWrapsInBank_ = false;
  return (static_cast<uint32_t>(bank) << 16) | low;
}

uint32_t CPU65816::addrDirectIndexedIndirect() {
  const uint32_t pointer = addrDirectX();
  const uint16_t target = read16Wrapped(pointer);
  operandWrapsInBank_ = false;
  return (static_cast<uint32_t>(dbr_) << 16) | target;
}

uint32_t CPU65816::addrDirectIndirectIndexed(bool pageCrossPenalty) {
  const uint32_t pointer = addrDirect();
  const uint16_t base = read16Wrapped(pointer);
  const uint32_t address =
      ((static_cast<uint32_t>(dbr_) << 16) | base) + (y_ & 0xFFFF);
  // Crossing a page costs a cycle, but only while the index is eight bits: a
  // 16-bit index pays for the carry every time and the base count includes it.
  // The cycle is charged when the index crosses a page *or* when the index
  // registers are sixteen bits wide — a wide index carries every time, so the
  // part stops trying to guess and always takes the extra cycle.
  if (pageCrossPenalty &&
      (!index8() || ((base & 0xFF00) != ((base + (y_ & 0xFF)) & 0xFF00))))
    cycleCount_++;
  operandWrapsInBank_ = false;
  return address & 0xFFFFFF;
}

uint32_t CPU65816::addrDirectIndirectLongIndexed() {
  const uint32_t base = addrDirectIndirectLong();
  operandWrapsInBank_ = false;
  return (base + (y_ & 0xFFFF)) & 0xFFFFFF;
}

uint32_t CPU65816::addrAbsolute() {
  const uint16_t offset = fetch16();
  operandWrapsInBank_ = false;
  return (static_cast<uint32_t>(dbr_) << 16) | offset;
}

uint32_t CPU65816::addrAbsoluteX(bool pageCrossPenalty) {
  const uint16_t base = fetch16();
  const uint32_t address =
      ((static_cast<uint32_t>(dbr_) << 16) | base) + (x_ & 0xFFFF);
  // The cycle is charged when the index crosses a page *or* when the index
  // registers are sixteen bits wide — a wide index carries every time, so the
  // part stops trying to guess and always takes the extra cycle.
  if (pageCrossPenalty &&
      (!index8() || ((base & 0xFF00) != ((base + (x_ & 0xFF)) & 0xFF00))))
    cycleCount_++;
  operandWrapsInBank_ = false;
  return address & 0xFFFFFF;
}

uint32_t CPU65816::addrAbsoluteY(bool pageCrossPenalty) {
  const uint16_t base = fetch16();
  const uint32_t address =
      ((static_cast<uint32_t>(dbr_) << 16) | base) + (y_ & 0xFFFF);
  // The cycle is charged when the index crosses a page *or* when the index
  // registers are sixteen bits wide — a wide index carries every time, so the
  // part stops trying to guess and always takes the extra cycle.
  if (pageCrossPenalty &&
      (!index8() || ((base & 0xFF00) != ((base + (y_ & 0xFF)) & 0xFF00))))
    cycleCount_++;
  operandWrapsInBank_ = false;
  return address & 0xFFFFFF;
}

uint32_t CPU65816::addrAbsoluteLong() { return fetch24(); }

uint32_t CPU65816::addrAbsoluteLongX() {
  operandWrapsInBank_ = false;
  operandWrapsInBank_ = false;
  return (fetch24() + (x_ & 0xFFFF)) & 0xFFFFFF;
}

uint32_t CPU65816::addrStackRelative() {
  const uint8_t offset = fetch8();
  operandWrapsInBank_ = true;
  return (sp_ + offset) & 0xFFFF; // Bank zero, like the stack itself
}

uint32_t CPU65816::addrStackRelativeIndirectIndexed() {
  const uint32_t pointer = addrStackRelative();
  const uint16_t base = read16Wrapped(pointer);
  operandWrapsInBank_ = false;
  return (((static_cast<uint32_t>(dbr_) << 16) | base) + (y_ & 0xFFFF)) &
         0xFFFFFF;
}

// ============================================================================
// Flags
// ============================================================================

void CPU65816::updateNZ8(uint8_t value) {
  setFlag(FLAG816_Z, value == 0);
  setFlag(FLAG816_N, (value & 0x80) != 0);
}

void CPU65816::updateNZ16(uint16_t value) {
  setFlag(FLAG816_Z, value == 0);
  setFlag(FLAG816_N, (value & 0x8000) != 0);
}

void CPU65816::updateNZ(uint16_t value, bool eightBit) {
  if (eightBit) {
    updateNZ8(static_cast<uint8_t>(value));
  } else {
    updateNZ16(value);
  }
}

// ============================================================================
// Operations
//
// Each reads its own operand at the width the flags currently say, which is why
// they take an effective address rather than a value: the width is the
// processor's business and not the addressing mode's.
// ============================================================================

void CPU65816::opADC(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t accumulator = eight ? (a_ & 0x00FF) : a_;
  const uint32_t carryIn = getFlag(FLAG816_C) ? 1 : 0;
  const uint16_t signMask = eight ? 0x0080 : 0x8000;
  uint32_t result = 0;

  if (getFlag(FLAG816_D)) {
    // Decimal mode, one nibble at a time, and the order matters: each digit is
    // corrected before the next is added, and overflow is taken from the value
    // *before* the top digit's correction. Reading V off the corrected result
    // instead gives an answer that is right most of the time, which is the
    // worst kind of wrong.
    const int nibbles = eight ? 2 : 4;
    result = (accumulator & 0x0F) + (operand & 0x0F) + carryIn;
    if (result > 0x09) result += 0x06;
    for (int nibble = 1; nibble < nibbles; nibble++) {
      const int shift = nibble * 4;
      const uint32_t digitMask = 0xFu << shift;
      const uint32_t carryBit = (result > ((1u << shift) - 1)) ? (1u << shift) : 0;
      result = (accumulator & digitMask) + (operand & digitMask) + carryBit +
               (result & ((1u << shift) - 1));
      // Every digit but the last is corrected here; the last is corrected
      // after V has been read.
      if (nibble < nibbles - 1) {
        const uint32_t limit = (0xAu << shift) - 1;
        if (result > limit) result += 0x6u << shift;
      }
    }
    setFlag(FLAG816_V,
            (~(accumulator ^ operand) & (accumulator ^ result) & signMask) != 0);
    const uint32_t topLimit = eight ? 0x9F : 0x9FFF;
    const uint32_t topAdjust = eight ? 0x60 : 0x6000;
    if (result > topLimit) result += topAdjust;
    setFlag(FLAG816_C, result > (eight ? 0xFFu : 0xFFFFu));
  } else {
    result = accumulator + operand + carryIn;
    setFlag(FLAG816_C, (result & (eight ? 0x100u : 0x10000u)) != 0);
    setFlag(FLAG816_V,
            (~(accumulator ^ operand) & (accumulator ^ result) & signMask) != 0);
  }

  const uint16_t value =
      static_cast<uint16_t>(eight ? (result & 0x00FF) : (result & 0xFFFF));
  if (eight) {
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | value);
  } else {
    a_ = value;
  }
  updateNZ(value, eight);
}

void CPU65816::opSBC(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t accumulator = eight ? (a_ & 0x00FF) : a_;
  const uint32_t carryIn = getFlag(FLAG816_C) ? 1 : 0;
  const uint16_t signMask = eight ? 0x0080 : 0x8000;
  const uint32_t mask = eight ? 0xFFu : 0xFFFFu;

  // Both flags come from the plain binary difference whichever mode this is,
  // and in decimal mode only the *value* is corrected. That asymmetry is the
  // part that catches people: a decimal subtraction sets carry from a binary
  // borrow that the answer it hands back does not show.
  const int32_t difference = static_cast<int32_t>(accumulator) -
                             static_cast<int32_t>(operand) -
                             static_cast<int32_t>(carryIn ? 0 : 1);
  setFlag(FLAG816_C, difference >= 0);
  setFlag(FLAG816_V, ((accumulator ^ operand) &
                      (accumulator ^ static_cast<uint32_t>(difference)) &
                      signMask) != 0);

  uint32_t result = static_cast<uint32_t>(difference) & mask;
  if (getFlag(FLAG816_D)) {
    const int nibbles = eight ? 2 : 4;
    int borrow = carryIn ? 0 : 1;
    result = 0;
    for (int nibble = 0; nibble < nibbles; nibble++) {
      const int shift = nibble * 4;
      int32_t digit = static_cast<int32_t>((accumulator >> shift) & 0xF) -
                      static_cast<int32_t>((operand >> shift) & 0xF) - borrow;
      borrow = 0;
      if (digit < 0) {
        digit -= 6;
        borrow = 1;
      }
      result |= static_cast<uint32_t>(digit & 0xF) << shift;
    }
  }

  const uint16_t value = static_cast<uint16_t>(result & mask);
  if (eight) {
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | value);
  } else {
    a_ = value;
  }
  updateNZ(value, eight);
}

void CPU65816::opAND(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  if (eight) {
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | ((a_ & operand) & 0x00FF));
    updateNZ8(static_cast<uint8_t>(a_));
  } else {
    a_ &= operand;
    updateNZ16(a_);
  }
}

void CPU65816::opORA(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  if (eight) {
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | ((a_ | operand) & 0x00FF));
    updateNZ8(static_cast<uint8_t>(a_));
  } else {
    a_ |= operand;
    updateNZ16(a_);
  }
}

void CPU65816::opEOR(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  if (eight) {
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | ((a_ ^ operand) & 0x00FF));
    updateNZ8(static_cast<uint8_t>(a_));
  } else {
    a_ ^= operand;
    updateNZ16(a_);
  }
}

void CPU65816::opBIT(uint32_t ea, bool immediate) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t accumulator = eight ? (a_ & 0x00FF) : a_;
  setFlag(FLAG816_Z, (accumulator & operand) == 0);
  // Immediate BIT sets only Z. Every other form copies the operand's top two
  // bits into N and V without the accumulator being involved at all, which is
  // what makes it the way to test a hardware flag.
  if (immediate) return;
  if (eight) {
    setFlag(FLAG816_N, (operand & 0x80) != 0);
    setFlag(FLAG816_V, (operand & 0x40) != 0);
  } else {
    setFlag(FLAG816_N, (operand & 0x8000) != 0);
    setFlag(FLAG816_V, (operand & 0x4000) != 0);
  }
}

void CPU65816::opCMP(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t accumulator = eight ? (a_ & 0x00FF) : a_;
  const uint32_t difference = static_cast<uint32_t>(accumulator) - operand;
  setFlag(FLAG816_C, accumulator >= operand);
  updateNZ(static_cast<uint16_t>(difference), eight);
}

void CPU65816::opCPX(uint32_t ea) {
  const bool eight = index8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t index = eight ? (x_ & 0x00FF) : x_;
  setFlag(FLAG816_C, index >= operand);
  updateNZ(static_cast<uint16_t>(index - operand), eight);
}

void CPU65816::opCPY(uint32_t ea) {
  const bool eight = index8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t index = eight ? (y_ & 0x00FF) : y_;
  setFlag(FLAG816_C, index >= operand);
  updateNZ(static_cast<uint16_t>(index - operand), eight);
}

void CPU65816::opLDA(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t value = read8or16(ea, eight);
  if (eight) {
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | (value & 0x00FF));
  } else {
    a_ = value;
  }
  updateNZ(value, eight);
}

void CPU65816::opLDX(uint32_t ea) {
  const bool eight = index8();
  const uint16_t value = read8or16(ea, eight);
  x_ = eight ? (value & 0x00FF) : value;
  updateNZ(x_, eight);
}

void CPU65816::opLDY(uint32_t ea) {
  const bool eight = index8();
  const uint16_t value = read8or16(ea, eight);
  y_ = eight ? (value & 0x00FF) : value;
  updateNZ(y_, eight);
}

void CPU65816::opSTA(uint32_t ea) { write8or16(ea, a_, accumulator8()); }
void CPU65816::opSTX(uint32_t ea) { write8or16(ea, x_, index8()); }
void CPU65816::opSTY(uint32_t ea) { write8or16(ea, y_, index8()); }
void CPU65816::opSTZ(uint32_t ea) { write8or16(ea, 0, accumulator8()); }

void CPU65816::opINC(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t value = static_cast<uint16_t>(read8or16(ea, eight) + 1);
  write8or16(ea, value, eight);
  updateNZ(value, eight);
}

void CPU65816::opDEC(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t value = static_cast<uint16_t>(read8or16(ea, eight) - 1);
  write8or16(ea, value, eight);
  updateNZ(value, eight);
}

void CPU65816::opASL(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  setFlag(FLAG816_C, (operand & (eight ? 0x80 : 0x8000)) != 0);
  const uint16_t value = static_cast<uint16_t>(operand << 1);
  write8or16(ea, value, eight);
  updateNZ(value, eight);
}

void CPU65816::opLSR(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  setFlag(FLAG816_C, (operand & 1) != 0);
  const uint16_t value = static_cast<uint16_t>(operand >> 1);
  write8or16(ea, value, eight);
  updateNZ(value, eight);
}

void CPU65816::opROL(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t carryIn = getFlag(FLAG816_C) ? 1 : 0;
  setFlag(FLAG816_C, (operand & (eight ? 0x80 : 0x8000)) != 0);
  const uint16_t value = static_cast<uint16_t>((operand << 1) | carryIn);
  write8or16(ea, value, eight);
  updateNZ(value, eight);
}

void CPU65816::opROR(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t carryIn = getFlag(FLAG816_C) ? (eight ? 0x80 : 0x8000) : 0;
  setFlag(FLAG816_C, (operand & 1) != 0);
  const uint16_t value = static_cast<uint16_t>((operand >> 1) | carryIn);
  write8or16(ea, value, eight);
  updateNZ(value, eight);
}

void CPU65816::opTSB(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t accumulator = eight ? (a_ & 0x00FF) : a_;
  setFlag(FLAG816_Z, (operand & accumulator) == 0);
  write8or16(ea, static_cast<uint16_t>(operand | accumulator), eight);
}

void CPU65816::opTRB(uint32_t ea) {
  const bool eight = accumulator8();
  const uint16_t operand = read8or16(ea, eight);
  const uint16_t accumulator = eight ? (a_ & 0x00FF) : a_;
  setFlag(FLAG816_Z, (operand & accumulator) == 0);
  write8or16(ea, static_cast<uint16_t>(operand & ~accumulator), eight);
}

void CPU65816::opASLAcc() {
  const bool eight = accumulator8();
  if (eight) {
    const uint8_t value = static_cast<uint8_t>(a_);
    setFlag(FLAG816_C, (value & 0x80) != 0);
    const uint8_t shifted = static_cast<uint8_t>(value << 1);
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | shifted);
    updateNZ8(shifted);
  } else {
    setFlag(FLAG816_C, (a_ & 0x8000) != 0);
    a_ = static_cast<uint16_t>(a_ << 1);
    updateNZ16(a_);
  }
}

void CPU65816::opLSRAcc() {
  const bool eight = accumulator8();
  if (eight) {
    const uint8_t value = static_cast<uint8_t>(a_);
    setFlag(FLAG816_C, (value & 1) != 0);
    const uint8_t shifted = static_cast<uint8_t>(value >> 1);
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | shifted);
    updateNZ8(shifted);
  } else {
    setFlag(FLAG816_C, (a_ & 1) != 0);
    a_ = static_cast<uint16_t>(a_ >> 1);
    updateNZ16(a_);
  }
}

void CPU65816::opROLAcc() {
  const bool eight = accumulator8();
  const uint16_t carryIn = getFlag(FLAG816_C) ? 1 : 0;
  if (eight) {
    const uint8_t value = static_cast<uint8_t>(a_);
    setFlag(FLAG816_C, (value & 0x80) != 0);
    const uint8_t rotated = static_cast<uint8_t>((value << 1) | carryIn);
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | rotated);
    updateNZ8(rotated);
  } else {
    setFlag(FLAG816_C, (a_ & 0x8000) != 0);
    a_ = static_cast<uint16_t>((a_ << 1) | carryIn);
    updateNZ16(a_);
  }
}

void CPU65816::opRORAcc() {
  const bool eight = accumulator8();
  if (eight) {
    const uint8_t value = static_cast<uint8_t>(a_);
    const uint8_t carryIn = getFlag(FLAG816_C) ? 0x80 : 0;
    setFlag(FLAG816_C, (value & 1) != 0);
    const uint8_t rotated = static_cast<uint8_t>((value >> 1) | carryIn);
    a_ = static_cast<uint16_t>((a_ & 0xFF00) | rotated);
    updateNZ8(rotated);
  } else {
    const uint16_t carryIn = getFlag(FLAG816_C) ? 0x8000 : 0;
    setFlag(FLAG816_C, (a_ & 1) != 0);
    a_ = static_cast<uint16_t>((a_ >> 1) | carryIn);
    updateNZ16(a_);
  }
}

void CPU65816::opMVN(bool negative) {
  // Block move. The operand bytes are the destination and source banks in that
  // order, and they become the data bank — a move leaves DBR pointing at where
  // it was writing. One byte is moved per execution and PC is rewound until the
  // count runs out, so an interrupt can be taken in the middle of a 64KB move
  // rather than after it.
  const uint8_t destinationBank = fetch8();
  const uint8_t sourceBank = fetch8();
  dbr_ = destinationBank;

  const uint32_t from = (static_cast<uint32_t>(sourceBank) << 16) | x_;
  const uint32_t to = (static_cast<uint32_t>(destinationBank) << 16) | y_;
  write8(to, read8(from));

  if (negative) {
    x_ = static_cast<uint16_t>(x_ + 1);
    y_ = static_cast<uint16_t>(y_ + 1);
  } else {
    x_ = static_cast<uint16_t>(x_ - 1);
    y_ = static_cast<uint16_t>(y_ - 1);
  }
  if (index8()) {
    x_ &= 0x00FF;
    y_ &= 0x00FF;
  }

  a_ = static_cast<uint16_t>(a_ - 1);
  if (a_ != 0xFFFF) {
    pc_ = static_cast<uint16_t>(pc_ - 3); // Run the same instruction again
  }
}

void CPU65816::branch(bool condition) {
  const int8_t offset = static_cast<int8_t>(fetch8());
  if (!condition) return;
  cycleCount_++;
  const uint16_t target = static_cast<uint16_t>(pc_ + offset);
  // A branch that crosses a page costs a cycle in emulation mode only; in
  // native mode the prefetch covers it.
  if (e_ && ((target & 0xFF00) != (pc_ & 0xFF00))) cycleCount_++;
  pc_ = target;
}

// ============================================================================
// Interrupts
// ============================================================================

void CPU65816::irq() { irqPending_ = true; }
void CPU65816::nmi() { nmiPending_ = true; }

void CPU65816::interrupt(uint16_t nativeVector, uint16_t emulationVector,
                         bool software) {
  // Native mode pushes the program bank as well, because a return has to know
  // which bank to go back to. Emulation mode does not, and the bank is zero.
  if (!e_) {
    push8(pbr_);
    push16(pc_);
    push8(p_);
    cycleCount_++;
  } else {
    push16(pc_);
    // The B flag is what tells a handler whether it was a BRK or a real
    // interrupt, and it only exists in emulation mode.
    push8(software ? (p_ | FLAG816_X) : (p_ & ~FLAG816_X));
  }

  setFlag(FLAG816_I, true);
  setFlag(FLAG816_D, false); // Never decimal inside a handler
  pbr_ = 0;
  pc_ = readVector(e_ ? emulationVector : nativeVector);
  waiting_ = false;
}

} // namespace a2e
