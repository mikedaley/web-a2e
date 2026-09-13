/*
 * disassembler65816.cpp - 65C816 instruction disassembler
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "disassembler65816.hpp"

#include <cstdio>

namespace a2e {

namespace {

// Short names, because the table is 256 entries and has to be readable as a
// table. They are local to this file and expand to the enumerators above.
constexpr AddrMode816 IMP = AddrMode816::Implied;
constexpr AddrMode816 ACC = AddrMode816::Accumulator;
constexpr AddrMode816 IM8 = AddrMode816::ImmediateByte;
constexpr AddrMode816 IMA = AddrMode816::ImmediateA;
constexpr AddrMode816 IMI = AddrMode816::ImmediateIndex;
constexpr AddrMode816 DP = AddrMode816::Direct;
constexpr AddrMode816 DPX = AddrMode816::DirectX;
constexpr AddrMode816 DPY = AddrMode816::DirectY;
constexpr AddrMode816 DPI = AddrMode816::DirectIndirect;
constexpr AddrMode816 DPIX = AddrMode816::DirectIndexedIndirect;
constexpr AddrMode816 DPIY = AddrMode816::DirectIndirectIndexed;
constexpr AddrMode816 DPL = AddrMode816::DirectIndirectLong;
constexpr AddrMode816 DPLY = AddrMode816::DirectIndirectLongIndexed;
constexpr AddrMode816 ABS = AddrMode816::Absolute;
constexpr AddrMode816 ABX = AddrMode816::AbsoluteX;
constexpr AddrMode816 ABY = AddrMode816::AbsoluteY;
constexpr AddrMode816 AL = AddrMode816::AbsoluteLong;
constexpr AddrMode816 ALX = AddrMode816::AbsoluteLongX;
constexpr AddrMode816 AIND = AddrMode816::AbsoluteIndirect;
constexpr AddrMode816 AIX = AddrMode816::AbsoluteIndexedIndirect;
constexpr AddrMode816 AINL = AddrMode816::AbsoluteIndirectLong;
constexpr AddrMode816 SR = AddrMode816::StackRelative;
constexpr AddrMode816 SRIY = AddrMode816::StackRelativeIndirectIndexed;
constexpr AddrMode816 REL = AddrMode816::Relative;
constexpr AddrMode816 RELL = AddrMode816::RelativeLong;
constexpr AddrMode816 MOVE = AddrMode816::BlockMove;

constexpr InstrCategory BR = InstrCategory::BRANCH;
constexpr InstrCategory LD = InstrCategory::LOAD;
constexpr InstrCategory MA = InstrCategory::MATH;
constexpr InstrCategory ST = InstrCategory::STACK;
constexpr InstrCategory FL = InstrCategory::FLAG;
constexpr InstrCategory UN = InstrCategory::UNKNOWN;

struct Entry816 {
  const char *mnemonic;
  AddrMode816 mode;
  InstrCategory category;
};

// Every one of the 256 opcodes, read off cpu65816_dispatch.cpp rather than
// typed from a datasheet: that table is verified against 5.1 million recorded
// states from real hardware, so it is the better authority, and
// test_disassembler65816.cpp checks the lengths here against what the CPU's
// program counter actually does so the two cannot drift.
constexpr Entry816 TABLE[256] = {
    {"BRK", IM8, BR}, {"ORA", DPIX, MA}, {"COP", IM8, BR}, {"ORA", SR, MA}, // 00-03
    {"TSB", DP, MA}, {"ORA", DP, MA}, {"ASL", DP, MA}, {"ORA", DPL, MA}, // 04-07
    {"PHP", IMP, ST}, {"ORA", IMA, MA}, {"ASL", ACC, MA}, {"PHD", IMP, ST}, // 08-0B
    {"TSB", ABS, MA}, {"ORA", ABS, MA}, {"ASL", ABS, MA}, {"ORA", AL, MA}, // 0C-0F
    {"BPL", REL, BR}, {"ORA", DPIY, MA}, {"ORA", DPI, MA}, {"ORA", SRIY, MA}, // 10-13
    {"TRB", DP, MA}, {"ORA", DPX, MA}, {"ASL", DPX, MA}, {"ORA", DPLY, MA}, // 14-17
    {"CLC", IMP, FL}, {"ORA", ABY, MA}, {"INC", ACC, MA}, {"TCS", IMP, ST}, // 18-1B
    {"TRB", ABS, MA}, {"ORA", ABX, MA}, {"ASL", ABX, MA}, {"ORA", ALX, MA}, // 1C-1F
    {"JSR", ABS, BR}, {"AND", DPIX, MA}, {"JSL", AL, BR}, {"AND", SR, MA}, // 20-23
    {"BIT", DP, MA}, {"AND", DP, MA}, {"ROL", DP, MA}, {"AND", DPL, MA}, // 24-27
    {"PLP", IMP, ST}, {"AND", IMA, MA}, {"ROL", ACC, MA}, {"PLD", IMP, ST}, // 28-2B
    {"BIT", ABS, MA}, {"AND", ABS, MA}, {"ROL", ABS, MA}, {"AND", AL, MA}, // 2C-2F
    {"BMI", REL, BR}, {"AND", DPIY, MA}, {"AND", DPI, MA}, {"AND", SRIY, MA}, // 30-33
    {"BIT", DPX, MA}, {"AND", DPX, MA}, {"ROL", DPX, MA}, {"AND", DPLY, MA}, // 34-37
    {"SEC", IMP, FL}, {"AND", ABY, MA}, {"DEC", ACC, MA}, {"TSC", IMP, ST}, // 38-3B
    {"BIT", ABX, MA}, {"AND", ABX, MA}, {"ROL", ABX, MA}, {"AND", ALX, MA}, // 3C-3F
    {"RTI", IMP, BR}, {"EOR", DPIX, MA}, {"WDM", IM8, BR}, {"EOR", SR, MA}, // 40-43
    {"MVP", MOVE, LD}, {"EOR", DP, MA}, {"LSR", DP, MA}, {"EOR", DPL, MA}, // 44-47
    {"PHA", IMP, ST}, {"EOR", IMA, MA}, {"LSR", ACC, MA}, {"PHK", IMP, ST}, // 48-4B
    {"JMP", ABS, BR}, {"EOR", ABS, MA}, {"LSR", ABS, MA}, {"EOR", AL, MA}, // 4C-4F
    {"BVC", REL, BR}, {"EOR", DPIY, MA}, {"EOR", DPI, MA}, {"EOR", SRIY, MA}, // 50-53
    {"MVN", MOVE, LD}, {"EOR", DPX, MA}, {"LSR", DPX, MA}, {"EOR", DPLY, MA}, // 54-57
    {"CLI", IMP, FL}, {"EOR", ABY, MA}, {"PHY", IMP, ST}, {"TCD", IMP, ST}, // 58-5B
    {"JML", AL, BR}, {"EOR", ABX, MA}, {"LSR", ABX, MA}, {"EOR", ALX, MA}, // 5C-5F
    {"RTS", IMP, BR}, {"ADC", DPIX, MA}, {"PER", RELL, ST}, {"ADC", SR, MA}, // 60-63
    {"STZ", DP, LD}, {"ADC", DP, MA}, {"ROR", DP, MA}, {"ADC", DPL, MA}, // 64-67
    {"PLA", IMP, ST}, {"ADC", IMA, MA}, {"ROR", ACC, MA}, {"RTL", IMP, BR}, // 68-6B
    {"JMP", AIND, BR}, {"ADC", ABS, MA}, {"ROR", ABS, MA}, {"ADC", AL, MA}, // 6C-6F
    {"BVS", REL, BR}, {"ADC", DPIY, MA}, {"ADC", DPI, MA}, {"ADC", SRIY, MA}, // 70-73
    {"STZ", DPX, LD}, {"ADC", DPX, MA}, {"ROR", DPX, MA}, {"ADC", DPLY, MA}, // 74-77
    {"SEI", IMP, FL}, {"ADC", ABY, MA}, {"PLY", IMP, ST}, {"TDC", IMP, ST}, // 78-7B
    {"JMP", AIX, BR}, {"ADC", ABX, MA}, {"ROR", ABX, MA}, {"ADC", ALX, MA}, // 7C-7F
    {"BRA", REL, BR}, {"STA", DPIX, LD}, {"BRL", RELL, BR}, {"STA", SR, LD}, // 80-83
    {"STY", DP, LD}, {"STA", DP, LD}, {"STX", DP, LD}, {"STA", DPL, LD}, // 84-87
    {"DEY", IMP, UN}, {"BIT", IMA, MA}, {"TXA", IMP, ST}, {"PHB", IMP, ST}, // 88-8B
    {"STY", ABS, LD}, {"STA", ABS, LD}, {"STX", ABS, LD}, {"STA", AL, LD}, // 8C-8F
    {"BCC", REL, BR}, {"STA", DPIY, LD}, {"STA", DPI, LD}, {"STA", SRIY, LD}, // 90-93
    {"STY", DPX, LD}, {"STA", DPX, LD}, {"STX", DPY, LD}, {"STA", DPLY, LD}, // 94-97
    {"TYA", IMP, ST}, {"STA", ABY, LD}, {"TXS", IMP, ST}, {"TXY", IMP, ST}, // 98-9B
    {"STZ", ABS, LD}, {"STA", ABX, LD}, {"STZ", ABX, LD}, {"STA", ALX, LD}, // 9C-9F
    {"LDY", IMI, LD}, {"LDA", DPIX, LD}, {"LDX", IMI, LD}, {"LDA", SR, LD}, // A0-A3
    {"LDY", DP, LD}, {"LDA", DP, LD}, {"LDX", DP, LD}, {"LDA", DPL, LD}, // A4-A7
    {"TAY", IMP, ST}, {"LDA", IMA, LD}, {"TAX", IMP, ST}, {"PLB", IMP, ST}, // A8-AB
    {"LDY", ABS, LD}, {"LDA", ABS, LD}, {"LDX", ABS, LD}, {"LDA", AL, LD}, // AC-AF
    {"BCS", REL, BR}, {"LDA", DPIY, LD}, {"LDA", DPI, LD}, {"LDA", SRIY, LD}, // B0-B3
    {"LDY", DPX, LD}, {"LDA", DPX, LD}, {"LDX", DPY, LD}, {"LDA", DPLY, LD}, // B4-B7
    {"CLV", IMP, FL}, {"LDA", ABY, LD}, {"TSX", IMP, ST}, {"TYX", IMP, ST}, // B8-BB
    {"LDY", ABX, LD}, {"LDA", ABX, LD}, {"LDX", ABY, LD}, {"LDA", ALX, LD}, // BC-BF
    {"CPY", IMI, MA}, {"CMP", DPIX, MA}, {"REP", IM8, FL}, {"CMP", SR, MA}, // C0-C3
    {"CPY", DP, MA}, {"CMP", DP, MA}, {"DEC", DP, MA}, {"CMP", DPL, MA}, // C4-C7
    {"INY", IMP, UN}, {"CMP", IMA, MA}, {"DEX", IMP, UN}, {"WAI", IMP, BR}, // C8-CB
    {"CPY", ABS, MA}, {"CMP", ABS, MA}, {"DEC", ABS, MA}, {"CMP", AL, MA}, // CC-CF
    {"BNE", REL, BR}, {"CMP", DPIY, MA}, {"CMP", DPI, MA}, {"CMP", SRIY, MA}, // D0-D3
    {"PEI", DPI, ST}, {"CMP", DPX, MA}, {"DEC", DPX, MA}, {"CMP", DPLY, MA}, // D4-D7
    {"CLD", IMP, FL}, {"CMP", ABY, MA}, {"PHX", IMP, ST}, {"STP", IMP, BR}, // D8-DB
    {"JML", AINL, BR}, {"CMP", ABX, MA}, {"DEC", ABX, MA}, {"CMP", ALX, MA}, // DC-DF
    {"CPX", IMI, MA}, {"SBC", DPIX, MA}, {"SEP", IM8, FL}, {"SBC", SR, MA}, // E0-E3
    {"CPX", DP, MA}, {"SBC", DP, MA}, {"INC", DP, MA}, {"SBC", DPL, MA}, // E4-E7
    {"INX", IMP, UN}, {"SBC", IMA, MA}, {"NOP", IMP, ST}, {"XBA", IMP, LD}, // E8-EB
    {"CPX", ABS, MA}, {"SBC", ABS, MA}, {"INC", ABS, MA}, {"SBC", AL, MA}, // EC-EF
    {"BEQ", REL, BR}, {"SBC", DPIY, MA}, {"SBC", DPI, MA}, {"SBC", SRIY, MA}, // F0-F3
    {"PEA", ABS, ST}, {"SBC", DPX, MA}, {"INC", DPX, MA}, {"SBC", DPLY, MA}, // F4-F7
    {"SED", IMP, FL}, {"SBC", ABY, MA}, {"PLX", IMP, ST}, {"XCE", IMP, FL}, // F8-FB
    {"JSR", AIX, BR}, {"SBC", ABX, MA}, {"INC", ABX, MA}, {"SBC", ALX, MA}, // FC-FF
};

} // namespace

const char *mnemonic816(uint8_t opcode) { return TABLE[opcode].mnemonic; }
AddrMode816 addressingMode816(uint8_t opcode) { return TABLE[opcode].mode; }
InstrCategory category816(uint8_t opcode) { return TABLE[opcode].category; }

int operandBytes816(AddrMode816 mode, bool accumulator8, bool index8) {
  switch (mode) {
  case AddrMode816::Implied:
  case AddrMode816::Accumulator:
    return 0;
  case AddrMode816::ImmediateA:
    return accumulator8 ? 1 : 2;
  case AddrMode816::ImmediateIndex:
    return index8 ? 1 : 2;
  case AddrMode816::ImmediateByte:
  case AddrMode816::Direct:
  case AddrMode816::DirectX:
  case AddrMode816::DirectY:
  case AddrMode816::DirectIndirect:
  case AddrMode816::DirectIndexedIndirect:
  case AddrMode816::DirectIndirectIndexed:
  case AddrMode816::DirectIndirectLong:
  case AddrMode816::DirectIndirectLongIndexed:
  case AddrMode816::StackRelative:
  case AddrMode816::StackRelativeIndirectIndexed:
  case AddrMode816::Relative:
    return 1;
  case AddrMode816::Absolute:
  case AddrMode816::AbsoluteX:
  case AddrMode816::AbsoluteY:
  case AddrMode816::AbsoluteIndirect:
  case AddrMode816::AbsoluteIndexedIndirect:
  case AddrMode816::AbsoluteIndirectLong:
  case AddrMode816::RelativeLong:
  case AddrMode816::BlockMove:
    return 2;
  case AddrMode816::AbsoluteLong:
  case AddrMode816::AbsoluteLongX:
    return 3;
  }
  return 0;
}

int instructionLength816(uint8_t opcode, bool accumulator8, bool index8) {
  return 1 + operandBytes816(TABLE[opcode].mode, accumulator8, index8);
}

FlowType flowType816(uint8_t opcode) {
  switch (opcode) {
  case 0x10: case 0x30: case 0x50: case 0x70:
  case 0x90: case 0xB0: case 0xD0: case 0xF0:
    return FlowType::CONDITIONAL;
  case 0x80: // BRA
  case 0x82: // BRL
  case 0x4C: // JMP abs
  case 0x5C: // JML long
    return FlowType::UNCONDITIONAL;
  case 0x20: // JSR abs
  case 0x22: // JSL long
  case 0xFC: // JSR (abs,X)
    return FlowType::CALL;
  case 0x60: // RTS
  case 0x6B: // RTL
  case 0x40: // RTI
    return FlowType::RETURN;
  case 0x6C: // JMP (abs)
  case 0x7C: // JMP (abs,X)
  case 0xDC: // JML [abs]
    return FlowType::INDIRECT;
  case 0x00: // BRK
  case 0x02: // COP
  case 0xCB: // WAI
  case 0xDB: // STP
    return FlowType::HALT;
  default:
    return FlowType::SEQUENTIAL;
  }
}

Disasm816Instruction disassemble816(const uint8_t *bytes, size_t size,
                                    uint32_t address, bool accumulator8,
                                    bool index8) {
  Disasm816Instruction out;
  out.address = address & 0xFFFFFF;
  out.target = out.address;
  if (!bytes || size == 0) return out;

  out.opcode = bytes[0];
  const Entry816 &entry = TABLE[out.opcode];
  out.mode = entry.mode;
  out.category = entry.category;
  for (int i = 0; i < 4 && entry.mnemonic[i]; i++) out.mnemonic[i] = entry.mnemonic[i];

  const int operands = operandBytes816(entry.mode, accumulator8, index8);
  out.length = static_cast<uint8_t>(1 + operands);
  for (int i = 0; i < operands; i++) {
    // A truncated instruction keeps its real length, so a caller walking
    // forward stays in step with the processor even at the end of a buffer.
    out.operands[i] = (static_cast<size_t>(i) + 1 < size) ? bytes[i + 1] : 0;
  }

  // Where it goes. A branch and a jump both stay in the program bank unless
  // the instruction names another one, which is the whole of the difference
  // between JMP and JML.
  const uint32_t bank = out.address & 0xFF0000;
  const uint16_t offset = static_cast<uint16_t>(out.address & 0xFFFF);
  const uint16_t word =
      static_cast<uint16_t>(out.operands[0] | (out.operands[1] << 8));
  switch (entry.mode) {
  case AddrMode816::Relative: {
    const int8_t delta = static_cast<int8_t>(out.operands[0]);
    out.target = bank | static_cast<uint16_t>(offset + 2 + delta);
    break;
  }
  case AddrMode816::RelativeLong: {
    const int16_t delta = static_cast<int16_t>(word);
    out.target = bank | static_cast<uint16_t>(offset + 3 + delta);
    break;
  }
  case AddrMode816::Absolute:
  case AddrMode816::AbsoluteIndirect:
  case AddrMode816::AbsoluteIndexedIndirect:
  case AddrMode816::AbsoluteIndirectLong:
    out.target = bank | word;
    break;
  case AddrMode816::AbsoluteLong:
  case AddrMode816::AbsoluteLongX:
    out.target = static_cast<uint32_t>(out.operands[2] << 16) | word;
    break;
  default:
    break;
  }
  return out;
}

namespace {
std::string hex(uint32_t value, int digits) {
  char buffer[12];
  snprintf(buffer, sizeof buffer, "%0*X", digits, value);
  return buffer;
}
} // namespace

std::string formatOperand816(const Disasm816Instruction &in) {
  const uint8_t low = in.operands[0];
  const uint16_t word = static_cast<uint16_t>(in.operands[0] | (in.operands[1] << 8));
  const uint32_t full =
      static_cast<uint32_t>(in.operands[2] << 16) | word;
  const bool wide = in.length >= 3;

  switch (in.mode) {
  case AddrMode816::Implied: return "";
  case AddrMode816::Accumulator: return "A";
  case AddrMode816::ImmediateByte: return "#$" + hex(low, 2);
  case AddrMode816::ImmediateA:
  case AddrMode816::ImmediateIndex:
    // The width the processor was in is why this instruction is as long as it
    // is, so the operand is printed at the width that was actually read.
    return wide ? "#$" + hex(word, 4) : "#$" + hex(low, 2);
  case AddrMode816::Direct: return "$" + hex(low, 2);
  case AddrMode816::DirectX: return "$" + hex(low, 2) + ",X";
  case AddrMode816::DirectY: return "$" + hex(low, 2) + ",Y";
  case AddrMode816::DirectIndirect: return "($" + hex(low, 2) + ")";
  case AddrMode816::DirectIndexedIndirect: return "($" + hex(low, 2) + ",X)";
  case AddrMode816::DirectIndirectIndexed: return "($" + hex(low, 2) + "),Y";
  case AddrMode816::DirectIndirectLong: return "[$" + hex(low, 2) + "]";
  case AddrMode816::DirectIndirectLongIndexed: return "[$" + hex(low, 2) + "],Y";
  case AddrMode816::Absolute: return "$" + hex(word, 4);
  case AddrMode816::AbsoluteX: return "$" + hex(word, 4) + ",X";
  case AddrMode816::AbsoluteY: return "$" + hex(word, 4) + ",Y";
  case AddrMode816::AbsoluteLong: return "$" + hex(full, 6);
  case AddrMode816::AbsoluteLongX: return "$" + hex(full, 6) + ",X";
  case AddrMode816::AbsoluteIndirect: return "($" + hex(word, 4) + ")";
  case AddrMode816::AbsoluteIndexedIndirect: return "($" + hex(word, 4) + ",X)";
  case AddrMode816::AbsoluteIndirectLong: return "[$" + hex(word, 4) + "]";
  case AddrMode816::StackRelative: return "$" + hex(low, 2) + ",S";
  case AddrMode816::StackRelativeIndirectIndexed:
    return "($" + hex(low, 2) + ",S),Y";
  case AddrMode816::Relative:
  case AddrMode816::RelativeLong:
    return "$" + hex(in.target & 0xFFFF, 4);
  case AddrMode816::BlockMove:
    // The operand bytes are destination then source, and an assembler's source
    // line is the other way round — MVN source,destination. Printing them in
    // the order they sit in memory would produce a line that reassembles into
    // a move in the opposite direction.
    return "$" + hex(in.operands[1], 2) + ",$" + hex(in.operands[0], 2);
  }
  return "";
}

std::string formatDisasm816(const Disasm816Instruction &in) {
  std::string line = hex((in.address >> 16) & 0xFF, 2) + "/" +
                     hex(in.address & 0xFFFF, 4) + ": ";
  for (int i = 0; i < 4; i++) {
    if (i < in.length) {
      line += hex(i == 0 ? in.opcode : in.operands[i - 1], 2);
      line += " ";
    } else {
      line += "   ";
    }
  }
  line += " ";
  line += in.mnemonic;
  const std::string operand = formatOperand816(in);
  if (!operand.empty()) {
    line += " ";
    line += operand;
  }
  return line;
}

} // namespace a2e
