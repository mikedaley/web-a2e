/*
 * cpu65816_dispatch.cpp - 65C816 instruction dispatch
 *
 * The 256 ways of arranging what cpu65816.cpp provides. Kept in its own file
 * for the same reason the Z80's opcodes are: a table this size buried at the
 * end of the core would make the core hard to read and this table hard to find.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "cpu65816.hpp"

#include <array>

namespace a2e {

namespace {

// Base cycle counts: eight-bit registers, a page-aligned direct page, and no
// index crossing a page. Everything else is charged by the addressing mode or
// the operation that causes it, so that each cost is written down next to the
// thing that incurs it rather than in a second table nobody remembers to edit.
constexpr std::array<uint8_t, 256> CYCLES = {{
    7, 6, 7, 4, 5, 3, 5, 6, 3, 2, 2, 4, 6, 4, 6, 5, // 00-0F
    2, 5, 5, 7, 5, 4, 6, 6, 2, 4, 2, 2, 6, 4, 7, 5, // 10-1F
    6, 6, 8, 4, 3, 3, 5, 6, 4, 2, 2, 5, 4, 4, 6, 5, // 20-2F
    2, 5, 5, 7, 4, 4, 6, 6, 2, 4, 2, 2, 4, 4, 7, 5, // 30-3F
    6, 6, 2, 4, 7, 3, 5, 6, 3, 2, 2, 3, 3, 4, 6, 5, // 40-4F
    2, 5, 5, 7, 7, 4, 6, 6, 2, 4, 3, 2, 4, 4, 7, 5, // 50-5F
    6, 6, 6, 4, 3, 3, 5, 6, 4, 2, 2, 6, 5, 4, 6, 5, // 60-6F
    2, 5, 5, 7, 4, 4, 6, 6, 2, 4, 4, 2, 6, 4, 7, 5, // 70-7F
    2, 6, 4, 4, 3, 3, 3, 6, 2, 2, 2, 3, 4, 4, 4, 5, // 80-8F
    2, 6, 5, 7, 4, 4, 4, 6, 2, 5, 2, 2, 4, 5, 5, 5, // 90-9F
    2, 6, 2, 4, 3, 3, 3, 6, 2, 2, 2, 4, 4, 4, 4, 5, // A0-AF
    2, 5, 5, 7, 4, 4, 4, 6, 2, 4, 2, 2, 4, 4, 4, 5, // B0-BF
    2, 6, 3, 4, 3, 3, 5, 6, 2, 2, 2, 4, 4, 4, 6, 5, // C0-CF
    2, 5, 5, 7, 6, 4, 6, 6, 2, 4, 3, 4, 6, 4, 7, 5, // D0-DF
    2, 6, 3, 4, 3, 3, 5, 6, 2, 2, 2, 3, 4, 4, 6, 5, // E0-EF
    2, 5, 5, 7, 5, 4, 6, 6, 2, 4, 4, 2, 8, 4, 7, 5, // F0-FF
}};

} // namespace

void CPU65816::executeInstruction() {
  if (stopped_) {
    // STP stops the clock. Only a reset starts it again, and until then the
    // machine is burning cycles doing nothing, which is what this says.
    cycleCount_ = 1;
    totalCycles_++;
    return;
  }

  // The IRQ input is a level, sampled while interrupts are enabled, with irq()
  // as the latch on top for a device that only pulses. Same model as CPU6502:
  // see the note there, and in CLAUDE.md.
  if (!getFlag(FLAG816_I) && irqStatusCallback_ && irqStatusCallback_()) {
    irqPending_ = true;
  }

  if (waiting_) {
    // WAI waits for an interrupt to arrive, not for one to be taken: with
    // interrupts masked the CPU wakes and carries on with the next instruction
    // instead, which is how it is used to synchronise with the video.
    if (nmiPending_ || irqPending_) {
      waiting_ = false;
    } else {
      cycleCount_ = 1;
      totalCycles_++;
      return;
    }
  }

  if (nmiPending_) {
    nmiPending_ = false;
    cycleCount_ = 7;
    interrupt(VEC_N_NMI, VEC_E_NMI, false);
    totalCycles_ += cycleCount_;
    return;
  }

  if (irqPending_ && !getFlag(FLAG816_I)) {
    irqPending_ = false;
    cycleCount_ = 7;
    interrupt(VEC_N_IRQ, VEC_E_IRQ, false);
    totalCycles_ += cycleCount_;
    return;
  }

  const uint8_t opcode = fetch8();
  cycleCount_ = CYCLES[opcode];
  executeOpcode(opcode);
  totalCycles_ += cycleCount_;
}

void CPU65816::executeOpcode(uint8_t opcode) {
  switch (opcode) {
  // ===== ORA =====
  case 0x09: opORA(addrImmediate(accumulator8())); break;
  case 0x05: opORA(addrDirect()); break;
  case 0x15: opORA(addrDirectX()); break;
  case 0x12: opORA(addrDirectIndirect()); break;
  case 0x01: opORA(addrDirectIndexedIndirect()); break;
  case 0x11: opORA(addrDirectIndirectIndexed()); break;
  case 0x07: opORA(addrDirectIndirectLong()); break;
  case 0x17: opORA(addrDirectIndirectLongIndexed()); break;
  case 0x0D: opORA(addrAbsolute()); break;
  case 0x1D: opORA(addrAbsoluteX()); break;
  case 0x19: opORA(addrAbsoluteY()); break;
  case 0x0F: opORA(addrAbsoluteLong()); break;
  case 0x1F: opORA(addrAbsoluteLongX()); break;
  case 0x03: opORA(addrStackRelative()); break;
  case 0x13: opORA(addrStackRelativeIndirectIndexed()); break;

  // ===== AND =====
  case 0x29: opAND(addrImmediate(accumulator8())); break;
  case 0x25: opAND(addrDirect()); break;
  case 0x35: opAND(addrDirectX()); break;
  case 0x32: opAND(addrDirectIndirect()); break;
  case 0x21: opAND(addrDirectIndexedIndirect()); break;
  case 0x31: opAND(addrDirectIndirectIndexed()); break;
  case 0x27: opAND(addrDirectIndirectLong()); break;
  case 0x37: opAND(addrDirectIndirectLongIndexed()); break;
  case 0x2D: opAND(addrAbsolute()); break;
  case 0x3D: opAND(addrAbsoluteX()); break;
  case 0x39: opAND(addrAbsoluteY()); break;
  case 0x2F: opAND(addrAbsoluteLong()); break;
  case 0x3F: opAND(addrAbsoluteLongX()); break;
  case 0x23: opAND(addrStackRelative()); break;
  case 0x33: opAND(addrStackRelativeIndirectIndexed()); break;

  // ===== EOR =====
  case 0x49: opEOR(addrImmediate(accumulator8())); break;
  case 0x45: opEOR(addrDirect()); break;
  case 0x55: opEOR(addrDirectX()); break;
  case 0x52: opEOR(addrDirectIndirect()); break;
  case 0x41: opEOR(addrDirectIndexedIndirect()); break;
  case 0x51: opEOR(addrDirectIndirectIndexed()); break;
  case 0x47: opEOR(addrDirectIndirectLong()); break;
  case 0x57: opEOR(addrDirectIndirectLongIndexed()); break;
  case 0x4D: opEOR(addrAbsolute()); break;
  case 0x5D: opEOR(addrAbsoluteX()); break;
  case 0x59: opEOR(addrAbsoluteY()); break;
  case 0x4F: opEOR(addrAbsoluteLong()); break;
  case 0x5F: opEOR(addrAbsoluteLongX()); break;
  case 0x43: opEOR(addrStackRelative()); break;
  case 0x53: opEOR(addrStackRelativeIndirectIndexed()); break;

  // ===== ADC =====
  case 0x69: opADC(addrImmediate(accumulator8())); break;
  case 0x65: opADC(addrDirect()); break;
  case 0x75: opADC(addrDirectX()); break;
  case 0x72: opADC(addrDirectIndirect()); break;
  case 0x61: opADC(addrDirectIndexedIndirect()); break;
  case 0x71: opADC(addrDirectIndirectIndexed()); break;
  case 0x67: opADC(addrDirectIndirectLong()); break;
  case 0x77: opADC(addrDirectIndirectLongIndexed()); break;
  case 0x6D: opADC(addrAbsolute()); break;
  case 0x7D: opADC(addrAbsoluteX()); break;
  case 0x79: opADC(addrAbsoluteY()); break;
  case 0x6F: opADC(addrAbsoluteLong()); break;
  case 0x7F: opADC(addrAbsoluteLongX()); break;
  case 0x63: opADC(addrStackRelative()); break;
  case 0x73: opADC(addrStackRelativeIndirectIndexed()); break;

  // ===== SBC =====
  case 0xE9: opSBC(addrImmediate(accumulator8())); break;
  case 0xE5: opSBC(addrDirect()); break;
  case 0xF5: opSBC(addrDirectX()); break;
  case 0xF2: opSBC(addrDirectIndirect()); break;
  case 0xE1: opSBC(addrDirectIndexedIndirect()); break;
  case 0xF1: opSBC(addrDirectIndirectIndexed()); break;
  case 0xE7: opSBC(addrDirectIndirectLong()); break;
  case 0xF7: opSBC(addrDirectIndirectLongIndexed()); break;
  case 0xED: opSBC(addrAbsolute()); break;
  case 0xFD: opSBC(addrAbsoluteX()); break;
  case 0xF9: opSBC(addrAbsoluteY()); break;
  case 0xEF: opSBC(addrAbsoluteLong()); break;
  case 0xFF: opSBC(addrAbsoluteLongX()); break;
  case 0xE3: opSBC(addrStackRelative()); break;
  case 0xF3: opSBC(addrStackRelativeIndirectIndexed()); break;

  // ===== CMP =====
  case 0xC9: opCMP(addrImmediate(accumulator8())); break;
  case 0xC5: opCMP(addrDirect()); break;
  case 0xD5: opCMP(addrDirectX()); break;
  case 0xD2: opCMP(addrDirectIndirect()); break;
  case 0xC1: opCMP(addrDirectIndexedIndirect()); break;
  case 0xD1: opCMP(addrDirectIndirectIndexed()); break;
  case 0xC7: opCMP(addrDirectIndirectLong()); break;
  case 0xD7: opCMP(addrDirectIndirectLongIndexed()); break;
  case 0xCD: opCMP(addrAbsolute()); break;
  case 0xDD: opCMP(addrAbsoluteX()); break;
  case 0xD9: opCMP(addrAbsoluteY()); break;
  case 0xCF: opCMP(addrAbsoluteLong()); break;
  case 0xDF: opCMP(addrAbsoluteLongX()); break;
  case 0xC3: opCMP(addrStackRelative()); break;
  case 0xD3: opCMP(addrStackRelativeIndirectIndexed()); break;

  // ===== CPX / CPY =====
  case 0xE0: opCPX(addrImmediate(index8())); break;
  case 0xE4: opCPX(addrDirect()); break;
  case 0xEC: opCPX(addrAbsolute()); break;
  case 0xC0: opCPY(addrImmediate(index8())); break;
  case 0xC4: opCPY(addrDirect()); break;
  case 0xCC: opCPY(addrAbsolute()); break;

  // ===== BIT =====
  case 0x89: opBIT(addrImmediate(accumulator8()), true); break;
  case 0x24: opBIT(addrDirect(), false); break;
  case 0x34: opBIT(addrDirectX(), false); break;
  case 0x2C: opBIT(addrAbsolute(), false); break;
  case 0x3C: opBIT(addrAbsoluteX(), false); break;

  // ===== LDA =====
  case 0xA9: opLDA(addrImmediate(accumulator8())); break;
  case 0xA5: opLDA(addrDirect()); break;
  case 0xB5: opLDA(addrDirectX()); break;
  case 0xB2: opLDA(addrDirectIndirect()); break;
  case 0xA1: opLDA(addrDirectIndexedIndirect()); break;
  case 0xB1: opLDA(addrDirectIndirectIndexed()); break;
  case 0xA7: opLDA(addrDirectIndirectLong()); break;
  case 0xB7: opLDA(addrDirectIndirectLongIndexed()); break;
  case 0xAD: opLDA(addrAbsolute()); break;
  case 0xBD: opLDA(addrAbsoluteX()); break;
  case 0xB9: opLDA(addrAbsoluteY()); break;
  case 0xAF: opLDA(addrAbsoluteLong()); break;
  case 0xBF: opLDA(addrAbsoluteLongX()); break;
  case 0xA3: opLDA(addrStackRelative()); break;
  case 0xB3: opLDA(addrStackRelativeIndirectIndexed()); break;

  // ===== LDX / LDY =====
  case 0xA2: opLDX(addrImmediate(index8())); break;
  case 0xA6: opLDX(addrDirect()); break;
  case 0xB6: opLDX(addrDirectY()); break;
  case 0xAE: opLDX(addrAbsolute()); break;
  case 0xBE: opLDX(addrAbsoluteY()); break;
  case 0xA0: opLDY(addrImmediate(index8())); break;
  case 0xA4: opLDY(addrDirect()); break;
  case 0xB4: opLDY(addrDirectX()); break;
  case 0xAC: opLDY(addrAbsolute()); break;
  case 0xBC: opLDY(addrAbsoluteX()); break;

  // ===== STA / STX / STY / STZ =====
  case 0x85: opSTA(addrDirect()); break;
  case 0x95: opSTA(addrDirectX()); break;
  case 0x92: opSTA(addrDirectIndirect()); break;
  case 0x81: opSTA(addrDirectIndexedIndirect()); break;
  case 0x91: opSTA(addrDirectIndirectIndexed(false)); break;
  case 0x87: opSTA(addrDirectIndirectLong()); break;
  case 0x97: opSTA(addrDirectIndirectLongIndexed()); break;
  case 0x8D: opSTA(addrAbsolute()); break;
  case 0x9D: opSTA(addrAbsoluteX(false)); break;
  case 0x99: opSTA(addrAbsoluteY(false)); break;
  case 0x8F: opSTA(addrAbsoluteLong()); break;
  case 0x9F: opSTA(addrAbsoluteLongX()); break;
  case 0x83: opSTA(addrStackRelative()); break;
  case 0x93: opSTA(addrStackRelativeIndirectIndexed()); break;
  case 0x86: opSTX(addrDirect()); break;
  case 0x96: opSTX(addrDirectY()); break;
  case 0x8E: opSTX(addrAbsolute()); break;
  case 0x84: opSTY(addrDirect()); break;
  case 0x94: opSTY(addrDirectX()); break;
  case 0x8C: opSTY(addrAbsolute()); break;
  case 0x64: opSTZ(addrDirect()); break;
  case 0x74: opSTZ(addrDirectX()); break;
  case 0x9C: opSTZ(addrAbsolute()); break;
  case 0x9E: opSTZ(addrAbsoluteX(false)); break;

  // ===== Read-modify-write =====
  case 0x0A: opASLAcc(); break;
  case 0x06: opASL(addrDirect()); break;
  case 0x16: opASL(addrDirectX()); break;
  case 0x0E: opASL(addrAbsolute()); break;
  case 0x1E: opASL(addrAbsoluteX(false)); break;
  case 0x4A: opLSRAcc(); break;
  case 0x46: opLSR(addrDirect()); break;
  case 0x56: opLSR(addrDirectX()); break;
  case 0x4E: opLSR(addrAbsolute()); break;
  case 0x5E: opLSR(addrAbsoluteX(false)); break;
  case 0x2A: opROLAcc(); break;
  case 0x26: opROL(addrDirect()); break;
  case 0x36: opROL(addrDirectX()); break;
  case 0x2E: opROL(addrAbsolute()); break;
  case 0x3E: opROL(addrAbsoluteX(false)); break;
  case 0x6A: opRORAcc(); break;
  case 0x66: opROR(addrDirect()); break;
  case 0x76: opROR(addrDirectX()); break;
  case 0x6E: opROR(addrAbsolute()); break;
  case 0x7E: opROR(addrAbsoluteX(false)); break;
  case 0xE6: opINC(addrDirect()); break;
  case 0xF6: opINC(addrDirectX()); break;
  case 0xEE: opINC(addrAbsolute()); break;
  case 0xFE: opINC(addrAbsoluteX(false)); break;
  case 0xC6: opDEC(addrDirect()); break;
  case 0xD6: opDEC(addrDirectX()); break;
  case 0xCE: opDEC(addrAbsolute()); break;
  case 0xDE: opDEC(addrAbsoluteX(false)); break;
  case 0x04: opTSB(addrDirect()); break;
  case 0x0C: opTSB(addrAbsolute()); break;
  case 0x14: opTRB(addrDirect()); break;
  case 0x1C: opTRB(addrAbsolute()); break;

  // ===== Accumulator and index arithmetic =====
  case 0x1A: // INC A
    if (accumulator8()) {
      a_ = static_cast<uint16_t>((a_ & 0xFF00) | ((a_ + 1) & 0x00FF));
      updateNZ8(static_cast<uint8_t>(a_));
    } else {
      a_ = static_cast<uint16_t>(a_ + 1);
      updateNZ16(a_);
    }
    break;
  case 0x3A: // DEC A
    if (accumulator8()) {
      a_ = static_cast<uint16_t>((a_ & 0xFF00) | ((a_ - 1) & 0x00FF));
      updateNZ8(static_cast<uint8_t>(a_));
    } else {
      a_ = static_cast<uint16_t>(a_ - 1);
      updateNZ16(a_);
    }
    break;
  case 0xE8: // INX
    x_ = static_cast<uint16_t>(x_ + 1);
    if (index8()) x_ &= 0x00FF;
    updateNZ(x_, index8());
    break;
  case 0xC8: // INY
    y_ = static_cast<uint16_t>(y_ + 1);
    if (index8()) y_ &= 0x00FF;
    updateNZ(y_, index8());
    break;
  case 0xCA: // DEX
    x_ = static_cast<uint16_t>(x_ - 1);
    if (index8()) x_ &= 0x00FF;
    updateNZ(x_, index8());
    break;
  case 0x88: // DEY
    y_ = static_cast<uint16_t>(y_ - 1);
    if (index8()) y_ &= 0x00FF;
    updateNZ(y_, index8());
    break;

  // ===== Transfers =====
  //
  // Each moves as many bits as the *destination* is wide, which is the rule
  // that makes TAX after REP #$10 different from TAX after SEP #$10 — and the
  // two that ignore the flags entirely, TCS/TCD and their partners, because
  // the stack and direct page registers are always sixteen bits.
  case 0xAA: // TAX
    x_ = index8() ? (a_ & 0x00FF) : a_;
    updateNZ(x_, index8());
    break;
  case 0xA8: // TAY
    y_ = index8() ? (a_ & 0x00FF) : a_;
    updateNZ(y_, index8());
    break;
  case 0x8A: // TXA
    if (accumulator8()) {
      a_ = static_cast<uint16_t>((a_ & 0xFF00) | (x_ & 0x00FF));
      updateNZ8(static_cast<uint8_t>(a_));
    } else {
      a_ = x_;
      updateNZ16(a_);
    }
    break;
  case 0x98: // TYA
    if (accumulator8()) {
      a_ = static_cast<uint16_t>((a_ & 0xFF00) | (y_ & 0x00FF));
      updateNZ8(static_cast<uint8_t>(a_));
    } else {
      a_ = y_;
      updateNZ16(a_);
    }
    break;
  case 0x9B: // TXY
    y_ = index8() ? (x_ & 0x00FF) : x_;
    updateNZ(y_, index8());
    break;
  case 0xBB: // TYX
    x_ = index8() ? (y_ & 0x00FF) : y_;
    updateNZ(x_, index8());
    break;
  case 0xBA: // TSX
    x_ = index8() ? (sp_ & 0x00FF) : sp_;
    updateNZ(x_, index8());
    break;
  case 0x9A: // TXS — no flags, and in emulation the stack stays in page one
    sp_ = e_ ? static_cast<uint16_t>(0x0100 | (x_ & 0x00FF)) : x_;
    break;
  case 0x1B: // TCS — the whole accumulator, whatever M says
    sp_ = e_ ? static_cast<uint16_t>(0x0100 | (a_ & 0x00FF)) : a_;
    break;
  case 0x3B: // TSC
    a_ = sp_;
    updateNZ16(a_);
    break;
  case 0x5B: // TCD
    d_ = a_;
    updateNZ16(d_);
    break;
  case 0x7B: // TDC
    a_ = d_;
    updateNZ16(a_);
    break;
  case 0xEB: // XBA — swap the halves, and B is what was hidden all along
    a_ = static_cast<uint16_t>((a_ >> 8) | (a_ << 8));
    updateNZ8(static_cast<uint8_t>(a_));
    break;

  // ===== Stack =====
  case 0x48: // PHA
    if (accumulator8()) {
      push8(static_cast<uint8_t>(a_));
    } else {
      push16(a_);
      cycleCount_++;
    }
    break;
  case 0x68: // PLA
    if (accumulator8()) {
      a_ = static_cast<uint16_t>((a_ & 0xFF00) | pop8());
      updateNZ8(static_cast<uint8_t>(a_));
    } else {
      a_ = pop16();
      cycleCount_++;
      updateNZ16(a_);
    }
    break;
  case 0xDA: // PHX
    if (index8()) {
      push8(static_cast<uint8_t>(x_));
    } else {
      push16(x_);
      cycleCount_++;
    }
    break;
  case 0xFA: // PLX
    if (index8()) {
      x_ = pop8();
      updateNZ8(static_cast<uint8_t>(x_));
    } else {
      x_ = pop16();
      cycleCount_++;
      updateNZ16(x_);
    }
    break;
  case 0x5A: // PHY
    if (index8()) {
      push8(static_cast<uint8_t>(y_));
    } else {
      push16(y_);
      cycleCount_++;
    }
    break;
  case 0x7A: // PLY
    if (index8()) {
      y_ = pop8();
      updateNZ8(static_cast<uint8_t>(y_));
    } else {
      y_ = pop16();
      cycleCount_++;
      updateNZ16(y_);
    }
    break;
  case 0x08: push8(p_); break;                  // PHP
  case 0x28: setP(pop8()); break;               // PLP
  case 0x8B:                                    // PHB
    push8Wide(dbr_);
    normaliseStack();
    break;
  case 0xAB:                                    // PLB
    dbr_ = pop8Wide();
    normaliseStack();
    updateNZ8(dbr_);
    break;
  case 0x4B:                                    // PHK
    push8Wide(pbr_);
    normaliseStack();
    break;
  case 0x0B:                                    // PHD
    push16Wide(d_);
    normaliseStack();
    break;
  case 0x2B:                                    // PLD
    d_ = pop16Wide();
    normaliseStack();
    updateNZ16(d_);
    break;
  case 0xF4:                                    // PEA
    push16Wide(fetch16());
    normaliseStack();
    break;
  case 0xD4:                                    // PEI
    push16Wide(read16Wrapped(addrDirect()));
    normaliseStack();
    break;
  case 0x62: {                                  // PER
    const int16_t offset = static_cast<int16_t>(fetch16());
    push16Wide(static_cast<uint16_t>(pc_ + offset));
    normaliseStack();
    break;
  }

  // ===== Flags and modes =====
  case 0x18: setFlag(FLAG816_C, false); break; // CLC
  case 0x38: setFlag(FLAG816_C, true); break;  // SEC
  case 0x58: setFlag(FLAG816_I, false); break; // CLI
  case 0x78: setFlag(FLAG816_I, true); break;  // SEI
  case 0xD8: setFlag(FLAG816_D, false); break; // CLD
  case 0xF8: setFlag(FLAG816_D, true); break;  // SED
  case 0xB8: setFlag(FLAG816_V, false); break; // CLV
  case 0xC2: setP(static_cast<uint8_t>(p_ & ~fetch8())); break; // REP
  case 0xE2: setP(static_cast<uint8_t>(p_ | fetch8())); break;  // SEP
  case 0xFB: {                                                  // XCE
    // The whole of the mode switch: swap carry and the emulation bit. A IIgs
    // leaves emulation mode with CLC, XCE and comes back with SEC, XCE.
    const bool carry = getFlag(FLAG816_C);
    setFlag(FLAG816_C, e_);
    setEmulation(carry);
    break;
  }

  // ===== Jumps and calls =====
  case 0x4C: pc_ = fetch16(); break; // JMP abs
  case 0x5C: {                       // JML long
    const uint32_t target = fetch24();
    pbr_ = static_cast<uint8_t>(target >> 16);
    pc_ = static_cast<uint16_t>(target);
    break;
  }
  case 0x6C: pc_ = read16Wrapped(fetch16()); break; // JMP (abs) — from bank 0
  case 0x7C: {                                      // JMP (abs,X)
    // Read through the program bank, because the table being indexed is code.
    const uint16_t base = static_cast<uint16_t>(fetch16() + x_);
    pc_ = read16Wrapped((static_cast<uint32_t>(pbr_) << 16) | base);
    break;
  }
  case 0xDC: { // JML [abs] — a long pointer, in bank 0
    const uint16_t pointer = fetch16();
    pc_ = read16Wrapped(pointer);
    pbr_ = read8((pointer + 2) & 0xFFFF);
    break;
  }
  case 0x20: { // JSR abs
    const uint16_t target = fetch16();
    push16(static_cast<uint16_t>(pc_ - 1));
    pc_ = target;
    break;
  }
  case 0xFC: { // JSR (abs,X)
    const uint16_t base = fetch16();
    push16(static_cast<uint16_t>(pc_ - 1));
    pc_ = read16Wrapped((static_cast<uint32_t>(pbr_) << 16) |
                        static_cast<uint16_t>(base + x_));
    break;
  }
  case 0x22: { // JSL long
    const uint32_t target = fetch24();
    push8Wide(pbr_);
    push16Wide(static_cast<uint16_t>(pc_ - 1));
    normaliseStack();
    pbr_ = static_cast<uint8_t>(target >> 16);
    pc_ = static_cast<uint16_t>(target);
    break;
  }
  case 0x60: pc_ = static_cast<uint16_t>(pop16() + 1); break; // RTS
  case 0x6B:                                                  // RTL
    pc_ = static_cast<uint16_t>(pop16Wide() + 1);
    pbr_ = pop8Wide();
    normaliseStack();
    break;
  case 0x40: // RTI
    setP(pop8());
    pc_ = pop16();
    if (!e_) {
      pbr_ = pop8();
      cycleCount_++;
    }
    break;

  // ===== Branches =====
  case 0x10: branch(!getFlag(FLAG816_N)); break; // BPL
  case 0x30: branch(getFlag(FLAG816_N)); break;  // BMI
  case 0x50: branch(!getFlag(FLAG816_V)); break; // BVC
  case 0x70: branch(getFlag(FLAG816_V)); break;  // BVS
  case 0x90: branch(!getFlag(FLAG816_C)); break; // BCC
  case 0xB0: branch(getFlag(FLAG816_C)); break;  // BCS
  case 0xD0: branch(!getFlag(FLAG816_Z)); break; // BNE
  case 0xF0: branch(getFlag(FLAG816_Z)); break;  // BEQ
  case 0x80: branch(true); break;                // BRA
  case 0x82: {                                   // BRL — sixteen bits of reach
    const int16_t offset = static_cast<int16_t>(fetch16());
    pc_ = static_cast<uint16_t>(pc_ + offset);
    break;
  }

  // ===== Block moves =====
  case 0x54: opMVN(true); break;  // MVN — ascending
  case 0x44: opMVN(false); break; // MVP — descending

  // ===== Interrupts and idling =====
  case 0x00: // BRK
    fetch8(); // The signature byte, which the handler may read back
    interrupt(VEC_N_BRK, VEC_E_IRQ, true);
    break;
  case 0x02: // COP
    fetch8();
    interrupt(VEC_N_COP, VEC_E_COP, true);
    break;
  case 0xCB: waiting_ = true; break; // WAI
  case 0xDB: stopped_ = true; break; // STP
  case 0xEA: break;                  // NOP
  case 0x42: fetch8(); break;        // WDM — reserved, and eats its operand

  default:
    // Every one of the 256 is defined on this part: there are no illegal
    // opcodes to emulate, so reaching here means the table above has a hole.
    break;
  }
}

} // namespace a2e
