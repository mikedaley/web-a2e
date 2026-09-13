/*
 * disassembler.cpp - 65C02 instruction disassembler with flow analysis
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "disassembler.hpp"
#include <algorithm>
#include <queue>
#include <unordered_set>

namespace a2e {

// Internal addressing mode enum for opcode table
enum AddrModeInt {
  IMP = 0, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL, ZPI, AIX, ZPR
};

// Internal category enum for opcode table
enum CategoryInt {
  CAT_BRANCH = 0, CAT_LOAD, CAT_MATH, CAT_STACK, CAT_FLAG, CAT_UNKNOWN
};

// OpcodeInfo is defined in disassembler.hpp

// Mnemonic table - index corresponds to mnemonicIndex in DisasmInstruction
static const char* MNEMONICS[] = {
  "???",  // 0 - unknown
  "ADC",  // 1
  "AND",  // 2
  "ASL",  // 3
  "BBR0", // 4
  "BBR1", // 5
  "BBR2", // 6
  "BBR3", // 7
  "BBR4", // 8
  "BBR5", // 9
  "BBR6", // 10
  "BBR7", // 11
  "BBS0", // 12
  "BBS1", // 13
  "BBS2", // 14
  "BBS3", // 15
  "BBS4", // 16
  "BBS5", // 17
  "BBS6", // 18
  "BBS7", // 19
  "BCC",  // 20
  "BCS",  // 21
  "BEQ",  // 22
  "BIT",  // 23
  "BMI",  // 24
  "BNE",  // 25
  "BPL",  // 26
  "BRA",  // 27
  "BRK",  // 28
  "BVC",  // 29
  "BVS",  // 30
  "CLC",  // 31
  "CLD",  // 32
  "CLI",  // 33
  "CLV",  // 34
  "CMP",  // 35
  "CPX",  // 36
  "CPY",  // 37
  "DEC",  // 38
  "DEX",  // 39
  "DEY",  // 40
  "EOR",  // 41
  "INC",  // 42
  "INX",  // 43
  "INY",  // 44
  "JMP",  // 45
  "JSR",  // 46
  "LDA",  // 47
  "LDX",  // 48
  "LDY",  // 49
  "LSR",  // 50
  "NOP",  // 51
  "ORA",  // 52
  "PHA",  // 53
  "PHP",  // 54
  "PHX",  // 55
  "PHY",  // 56
  "PLA",  // 57
  "PLP",  // 58
  "PLX",  // 59
  "PLY",  // 60
  "RMB0", // 61
  "RMB1", // 62
  "RMB2", // 63
  "RMB3", // 64
  "RMB4", // 65
  "RMB5", // 66
  "RMB6", // 67
  "RMB7", // 68
  "ROL",  // 69
  "ROR",  // 70
  "RTI",  // 71
  "RTS",  // 72
  "SBC",  // 73
  "SEC",  // 74
  "SED",  // 75
  "SEI",  // 76
  "SMB0", // 77
  "SMB1", // 78
  "SMB2", // 79
  "SMB3", // 80
  "SMB4", // 81
  "SMB5", // 82
  "SMB6", // 83
  "SMB7", // 84
  "STA",  // 85
  "STP",  // 86
  "STX",  // 87
  "STY",  // 88
  "STZ",  // 89
  "TAX",  // 90
  "TAY",  // 91
  "TRB",  // 92
  "TSB",  // 93
  "TSX",  // 94
  "TXA",  // 95
  "TXS",  // 96
  "TYA",  // 97
  "WAI",  // 98
};

// Mnemonic indices
enum MnemIdx {
  M_UNK = 0,
  M_ADC = 1, M_AND, M_ASL,
  M_BBR0, M_BBR1, M_BBR2, M_BBR3, M_BBR4, M_BBR5, M_BBR6, M_BBR7,
  M_BBS0, M_BBS1, M_BBS2, M_BBS3, M_BBS4, M_BBS5, M_BBS6, M_BBS7,
  M_BCC, M_BCS, M_BEQ, M_BIT, M_BMI, M_BNE, M_BPL, M_BRA, M_BRK, M_BVC, M_BVS,
  M_CLC, M_CLD, M_CLI, M_CLV, M_CMP, M_CPX, M_CPY,
  M_DEC, M_DEX, M_DEY,
  M_EOR,
  M_INC, M_INX, M_INY,
  M_JMP, M_JSR,
  M_LDA, M_LDX, M_LDY, M_LSR,
  M_NOP,
  M_ORA,
  M_PHA, M_PHP, M_PHX, M_PHY, M_PLA, M_PLP, M_PLX, M_PLY,
  M_RMB0, M_RMB1, M_RMB2, M_RMB3, M_RMB4, M_RMB5, M_RMB6, M_RMB7,
  M_ROL, M_ROR, M_RTI, M_RTS,
  M_SBC, M_SEC, M_SED, M_SEI,
  M_SMB0, M_SMB1, M_SMB2, M_SMB3, M_SMB4, M_SMB5, M_SMB6, M_SMB7,
  M_STA, M_STP, M_STX, M_STY, M_STZ,
  M_TAX, M_TAY, M_TRB, M_TSB, M_TSX, M_TXA, M_TXS, M_TYA,
  M_WAI
};

// Full 65C02 opcode table: {mnemonicIndex, mode, category}
static const OpcodeInfo opcodes[256] = {
  // 0x00-0x0F
  {M_BRK, IMP, CAT_STACK},  {M_ORA, IZX, CAT_MATH},   {M_UNK, IMP, CAT_UNKNOWN}, {M_UNK, IMP, CAT_UNKNOWN},
  {M_TSB, ZP, CAT_MATH},    {M_ORA, ZP, CAT_MATH},    {M_ASL, ZP, CAT_MATH},     {M_RMB0, ZP, CAT_MATH},
  {M_PHP, IMP, CAT_STACK},  {M_ORA, IMM, CAT_MATH},   {M_ASL, ACC, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_TSB, ABS, CAT_MATH},   {M_ORA, ABS, CAT_MATH},   {M_ASL, ABS, CAT_MATH},    {M_BBR0, ZPR, CAT_BRANCH},
  // 0x10-0x1F
  {M_BPL, REL, CAT_BRANCH}, {M_ORA, IZY, CAT_MATH},   {M_ORA, ZPI, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_TRB, ZP, CAT_MATH},    {M_ORA, ZPX, CAT_MATH},   {M_ASL, ZPX, CAT_MATH},    {M_RMB1, ZP, CAT_MATH},
  {M_CLC, IMP, CAT_FLAG},   {M_ORA, ABY, CAT_MATH},   {M_INC, ACC, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_TRB, ABS, CAT_MATH},   {M_ORA, ABX, CAT_MATH},   {M_ASL, ABX, CAT_MATH},    {M_BBR1, ZPR, CAT_BRANCH},
  // 0x20-0x2F
  {M_JSR, ABS, CAT_BRANCH}, {M_AND, IZX, CAT_MATH},   {M_UNK, IMP, CAT_UNKNOWN}, {M_UNK, IMP, CAT_UNKNOWN},
  {M_BIT, ZP, CAT_MATH},    {M_AND, ZP, CAT_MATH},    {M_ROL, ZP, CAT_MATH},     {M_RMB2, ZP, CAT_MATH},
  {M_PLP, IMP, CAT_STACK},  {M_AND, IMM, CAT_MATH},   {M_ROL, ACC, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_BIT, ABS, CAT_MATH},   {M_AND, ABS, CAT_MATH},   {M_ROL, ABS, CAT_MATH},    {M_BBR2, ZPR, CAT_BRANCH},
  // 0x30-0x3F
  {M_BMI, REL, CAT_BRANCH}, {M_AND, IZY, CAT_MATH},   {M_AND, ZPI, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_BIT, ZPX, CAT_MATH},   {M_AND, ZPX, CAT_MATH},   {M_ROL, ZPX, CAT_MATH},    {M_RMB3, ZP, CAT_MATH},
  {M_SEC, IMP, CAT_FLAG},   {M_AND, ABY, CAT_MATH},   {M_DEC, ACC, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_BIT, ABX, CAT_MATH},   {M_AND, ABX, CAT_MATH},   {M_ROL, ABX, CAT_MATH},    {M_BBR3, ZPR, CAT_BRANCH},
  // 0x40-0x4F
  {M_RTI, IMP, CAT_BRANCH}, {M_EOR, IZX, CAT_MATH},   {M_UNK, IMP, CAT_UNKNOWN}, {M_UNK, IMP, CAT_UNKNOWN},
  {M_UNK, IMP, CAT_UNKNOWN},{M_EOR, ZP, CAT_MATH},    {M_LSR, ZP, CAT_MATH},     {M_RMB4, ZP, CAT_MATH},
  {M_PHA, IMP, CAT_STACK},  {M_EOR, IMM, CAT_MATH},   {M_LSR, ACC, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_JMP, ABS, CAT_BRANCH}, {M_EOR, ABS, CAT_MATH},   {M_LSR, ABS, CAT_MATH},    {M_BBR4, ZPR, CAT_BRANCH},
  // 0x50-0x5F
  {M_BVC, REL, CAT_BRANCH}, {M_EOR, IZY, CAT_MATH},   {M_EOR, ZPI, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_UNK, IMP, CAT_UNKNOWN},{M_EOR, ZPX, CAT_MATH},   {M_LSR, ZPX, CAT_MATH},    {M_RMB5, ZP, CAT_MATH},
  {M_CLI, IMP, CAT_FLAG},   {M_EOR, ABY, CAT_MATH},   {M_PHY, IMP, CAT_STACK},   {M_UNK, IMP, CAT_UNKNOWN},
  {M_UNK, IMP, CAT_UNKNOWN},{M_EOR, ABX, CAT_MATH},   {M_LSR, ABX, CAT_MATH},    {M_BBR5, ZPR, CAT_BRANCH},
  // 0x60-0x6F
  {M_RTS, IMP, CAT_BRANCH}, {M_ADC, IZX, CAT_MATH},   {M_UNK, IMP, CAT_UNKNOWN}, {M_UNK, IMP, CAT_UNKNOWN},
  {M_STZ, ZP, CAT_LOAD},    {M_ADC, ZP, CAT_MATH},    {M_ROR, ZP, CAT_MATH},     {M_RMB6, ZP, CAT_MATH},
  {M_PLA, IMP, CAT_STACK},  {M_ADC, IMM, CAT_MATH},   {M_ROR, ACC, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_JMP, IND, CAT_BRANCH}, {M_ADC, ABS, CAT_MATH},   {M_ROR, ABS, CAT_MATH},    {M_BBR6, ZPR, CAT_BRANCH},
  // 0x70-0x7F
  {M_BVS, REL, CAT_BRANCH}, {M_ADC, IZY, CAT_MATH},   {M_ADC, ZPI, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_STZ, ZPX, CAT_LOAD},   {M_ADC, ZPX, CAT_MATH},   {M_ROR, ZPX, CAT_MATH},    {M_RMB7, ZP, CAT_MATH},
  {M_SEI, IMP, CAT_FLAG},   {M_ADC, ABY, CAT_MATH},   {M_PLY, IMP, CAT_STACK},   {M_UNK, IMP, CAT_UNKNOWN},
  {M_JMP, AIX, CAT_BRANCH}, {M_ADC, ABX, CAT_MATH},   {M_ROR, ABX, CAT_MATH},    {M_BBR7, ZPR, CAT_BRANCH},
  // 0x80-0x8F
  {M_BRA, REL, CAT_BRANCH}, {M_STA, IZX, CAT_LOAD},   {M_UNK, IMP, CAT_UNKNOWN}, {M_UNK, IMP, CAT_UNKNOWN},
  {M_STY, ZP, CAT_LOAD},    {M_STA, ZP, CAT_LOAD},    {M_STX, ZP, CAT_LOAD},     {M_SMB0, ZP, CAT_MATH},
  {M_DEY, IMP, CAT_STACK},  {M_BIT, IMM, CAT_MATH},   {M_TXA, IMP, CAT_STACK},   {M_UNK, IMP, CAT_UNKNOWN},
  {M_STY, ABS, CAT_LOAD},   {M_STA, ABS, CAT_LOAD},   {M_STX, ABS, CAT_LOAD},    {M_BBS0, ZPR, CAT_BRANCH},
  // 0x90-0x9F
  {M_BCC, REL, CAT_BRANCH}, {M_STA, IZY, CAT_LOAD},   {M_STA, ZPI, CAT_LOAD},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_STY, ZPX, CAT_LOAD},   {M_STA, ZPX, CAT_LOAD},   {M_STX, ZPY, CAT_LOAD},    {M_SMB1, ZP, CAT_MATH},
  {M_TYA, IMP, CAT_STACK},  {M_STA, ABY, CAT_LOAD},   {M_TXS, IMP, CAT_STACK},   {M_UNK, IMP, CAT_UNKNOWN},
  {M_STZ, ABS, CAT_LOAD},   {M_STA, ABX, CAT_LOAD},   {M_STZ, ABX, CAT_LOAD},    {M_BBS1, ZPR, CAT_BRANCH},
  // 0xA0-0xAF
  {M_LDY, IMM, CAT_LOAD},   {M_LDA, IZX, CAT_LOAD},   {M_LDX, IMM, CAT_LOAD},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_LDY, ZP, CAT_LOAD},    {M_LDA, ZP, CAT_LOAD},    {M_LDX, ZP, CAT_LOAD},     {M_SMB2, ZP, CAT_MATH},
  {M_TAY, IMP, CAT_STACK},  {M_LDA, IMM, CAT_LOAD},   {M_TAX, IMP, CAT_STACK},   {M_UNK, IMP, CAT_UNKNOWN},
  {M_LDY, ABS, CAT_LOAD},   {M_LDA, ABS, CAT_LOAD},   {M_LDX, ABS, CAT_LOAD},    {M_BBS2, ZPR, CAT_BRANCH},
  // 0xB0-0xBF
  {M_BCS, REL, CAT_BRANCH}, {M_LDA, IZY, CAT_LOAD},   {M_LDA, ZPI, CAT_LOAD},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_LDY, ZPX, CAT_LOAD},   {M_LDA, ZPX, CAT_LOAD},   {M_LDX, ZPY, CAT_LOAD},    {M_SMB3, ZP, CAT_MATH},
  {M_CLV, IMP, CAT_FLAG},   {M_LDA, ABY, CAT_LOAD},   {M_TSX, IMP, CAT_STACK},   {M_UNK, IMP, CAT_UNKNOWN},
  {M_LDY, ABX, CAT_LOAD},   {M_LDA, ABX, CAT_LOAD},   {M_LDX, ABY, CAT_LOAD},    {M_BBS3, ZPR, CAT_BRANCH},
  // 0xC0-0xCF
  {M_CPY, IMM, CAT_MATH},   {M_CMP, IZX, CAT_MATH},   {M_UNK, IMP, CAT_UNKNOWN}, {M_UNK, IMP, CAT_UNKNOWN},
  {M_CPY, ZP, CAT_MATH},    {M_CMP, ZP, CAT_MATH},    {M_DEC, ZP, CAT_MATH},     {M_SMB4, ZP, CAT_MATH},
  {M_INY, IMP, CAT_STACK},  {M_CMP, IMM, CAT_MATH},   {M_DEX, IMP, CAT_STACK},   {M_WAI, IMP, CAT_STACK},
  {M_CPY, ABS, CAT_MATH},   {M_CMP, ABS, CAT_MATH},   {M_DEC, ABS, CAT_MATH},    {M_BBS4, ZPR, CAT_BRANCH},
  // 0xD0-0xDF
  {M_BNE, REL, CAT_BRANCH}, {M_CMP, IZY, CAT_MATH},   {M_CMP, ZPI, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_UNK, IMP, CAT_UNKNOWN},{M_CMP, ZPX, CAT_MATH},   {M_DEC, ZPX, CAT_MATH},    {M_SMB5, ZP, CAT_MATH},
  {M_CLD, IMP, CAT_FLAG},   {M_CMP, ABY, CAT_MATH},   {M_PHX, IMP, CAT_STACK},   {M_STP, IMP, CAT_STACK},
  {M_UNK, IMP, CAT_UNKNOWN},{M_CMP, ABX, CAT_MATH},   {M_DEC, ABX, CAT_MATH},    {M_BBS5, ZPR, CAT_BRANCH},
  // 0xE0-0xEF
  {M_CPX, IMM, CAT_MATH},   {M_SBC, IZX, CAT_MATH},   {M_UNK, IMP, CAT_UNKNOWN}, {M_UNK, IMP, CAT_UNKNOWN},
  {M_CPX, ZP, CAT_MATH},    {M_SBC, ZP, CAT_MATH},    {M_INC, ZP, CAT_MATH},     {M_SMB6, ZP, CAT_MATH},
  {M_INX, IMP, CAT_STACK},  {M_SBC, IMM, CAT_MATH},   {M_NOP, IMP, CAT_FLAG},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_CPX, ABS, CAT_MATH},   {M_SBC, ABS, CAT_MATH},   {M_INC, ABS, CAT_MATH},    {M_BBS6, ZPR, CAT_BRANCH},
  // 0xF0-0xFF
  {M_BEQ, REL, CAT_BRANCH}, {M_SBC, IZY, CAT_MATH},   {M_SBC, ZPI, CAT_MATH},    {M_UNK, IMP, CAT_UNKNOWN},
  {M_UNK, IMP, CAT_UNKNOWN},{M_SBC, ZPX, CAT_MATH},   {M_INC, ZPX, CAT_MATH},    {M_SMB7, ZP, CAT_MATH},
  {M_SED, IMP, CAT_FLAG},   {M_SBC, ABY, CAT_MATH},   {M_PLX, IMP, CAT_STACK},   {M_UNK, IMP, CAT_UNKNOWN},
  {M_UNK, IMP, CAT_UNKNOWN},{M_SBC, ABX, CAT_MATH},   {M_INC, ABX, CAT_MATH},    {M_BBS7, ZPR, CAT_BRANCH}
};

int getInstructionLength(uint8_t opcode) {
  switch (opcodes[opcode].mode) {
    case IMP:
    case ACC:
      return 1;
    case IMM:
    case ZP:
    case ZPX:
    case ZPY:
    case IZX:
    case IZY:
    case ZPI:
    case REL:
      return 2;
    case ABS:
    case ABX:
    case ABY:
    case IND:
    case AIX:
    case ZPR:
      return 3;
  }
  return 1;
}

const char* getMnemonic(uint8_t opcode) {
  return MNEMONICS[opcodes[opcode].mnemonicIndex];
}

const char* getMnemonicByIndex(uint8_t index) {
  if (index >= sizeof(MNEMONICS) / sizeof(MNEMONICS[0])) {
    return "???";
  }
  return MNEMONICS[index];
}

AddrMode getAddressingMode(uint8_t opcode) {
  return static_cast<AddrMode>(opcodes[opcode].mode);
}

InstrCategory getInstructionCategory(uint8_t opcode) {
  return static_cast<InstrCategory>(opcodes[opcode].category);
}

const OpcodeInfo* getOpcodeTable() {
  return opcodes;
}

int getMnemonicCount() {
  return static_cast<int>(sizeof(MNEMONICS) / sizeof(MNEMONICS[0]));
}

namespace {
std::string hexDigits(uint32_t value, int digits) {
  static const char *DIGITS = "0123456789ABCDEF";
  std::string out(static_cast<size_t>(digits), '0');
  for (int i = digits - 1; i >= 0; i--) {
    out[static_cast<size_t>(i)] = DIGITS[value & 0x0F];
    value >>= 4;
  }
  return out;
}
} // namespace

std::string formatOperand(const DisasmInstruction &in) {
  const uint8_t low = in.operand1;
  const uint16_t word =
      static_cast<uint16_t>(in.operand1 | (in.operand2 << 8));
  switch (static_cast<AddrMode>(in.mode)) {
  case AddrMode::IMP: return "";
  case AddrMode::ACC: return "A";
  case AddrMode::IMM: return "#$" + hexDigits(low, 2);
  case AddrMode::ZP: return "$" + hexDigits(low, 2);
  case AddrMode::ZPX: return "$" + hexDigits(low, 2) + ",X";
  case AddrMode::ZPY: return "$" + hexDigits(low, 2) + ",Y";
  case AddrMode::ABS: return "$" + hexDigits(word, 4);
  case AddrMode::ABX: return "$" + hexDigits(word, 4) + ",X";
  case AddrMode::ABY: return "$" + hexDigits(word, 4) + ",Y";
  case AddrMode::IND: return "($" + hexDigits(word, 4) + ")";
  case AddrMode::IZX: return "($" + hexDigits(low, 2) + ",X)";
  case AddrMode::IZY: return "($" + hexDigits(low, 2) + "),Y";
  case AddrMode::ZPI: return "($" + hexDigits(low, 2) + ")";
  case AddrMode::AIX: return "($" + hexDigits(word, 4) + ",X)";
  case AddrMode::REL: return "$" + hexDigits(in.target, 4);
  case AddrMode::ZPR:
    // BBR and BBS: a zero page address to test, and where to go if it holds.
    return "$" + hexDigits(low, 2) + ",$" + hexDigits(in.target, 4);
  }
  return "";
}

DisasmInstruction disassembleInstruction(const uint8_t *data, size_t size,
                                          uint16_t address) {
  DisasmInstruction instr = {};
  instr.address = address;

  if (size == 0 || data == nullptr) {
    instr.length = 0;
    instr.mnemonic[0] = '?';
    instr.mnemonic[1] = '?';
    instr.mnemonic[2] = '?';
    instr.mnemonic[3] = '\0';
    return instr;
  }

  uint8_t opcode = data[0];
  const OpcodeInfo &info = opcodes[opcode];
  int instrLen = getInstructionLength(opcode);

  instr.opcode = opcode;
  instr.length = static_cast<uint8_t>(instrLen);
  instr.mode = info.mode;
  instr.category = info.category;

  // Read operand bytes (if available)
  instr.operand1 = (size > 1 && instrLen >= 2) ? data[1] : 0;
  instr.operand2 = (size > 2 && instrLen >= 3) ? data[2] : 0;

  // Calculate target address for branches and jumps
  instr.target = 0;
  switch (info.mode) {
    case REL: {
      int8_t offset = static_cast<int8_t>(instr.operand1);
      instr.target = static_cast<uint16_t>(address + 2 + offset);
      break;
    }
    case ZPR: {
      int8_t offset = static_cast<int8_t>(instr.operand2);
      instr.target = static_cast<uint16_t>(address + 3 + offset);
      break;
    }
    case ABS:
    case ABX:
    case ABY:
    case IND:
    case AIX:
      instr.target = static_cast<uint16_t>(instr.operand1 | (instr.operand2 << 8));
      break;
    default:
      break;
  }

  // Copy mnemonic string (max 4 chars, null-padded)
  const char* mnem = MNEMONICS[info.mnemonicIndex];
  for (int i = 0; i < 4; i++) {
    instr.mnemonic[i] = mnem[i];
    if (mnem[i] == '\0') {
      // Null-pad remainder
      for (int j = i + 1; j < 4; j++) {
        instr.mnemonic[j] = '\0';
      }
      break;
    }
  }

  return instr;
}

DisasmResult disassembleBlock(const uint8_t *data, size_t size,
                              uint16_t baseAddress) {
  DisasmResult result;

  if (size == 0 || data == nullptr) {
    return result;
  }

  // Reserve approximate space
  result.instructions.reserve(size / 2);

  size_t offset = 0;
  while (offset < size) {
    DisasmInstruction instr = disassembleInstruction(
      data + offset, size - offset,
      static_cast<uint16_t>(baseAddress + offset));

    if (instr.length == 0) {
      break;
    }

    result.instructions.push_back(instr);
    offset += instr.length;
  }

  return result;
}

FlowType getFlowType(uint8_t opcode) {
  const OpcodeInfo &info = opcodes[opcode];
  uint8_t mnem = info.mnemonicIndex;

  // Return instructions - stop tracing this path
  if (mnem == M_RTS || mnem == M_RTI) {
    return FlowType::RETURN;
  }

  // Halt instructions - stop tracing
  if (mnem == M_BRK || mnem == M_STP || mnem == M_WAI) {
    return FlowType::HALT;
  }

  // JSR - subroutine call, will return
  if (mnem == M_JSR) {
    return FlowType::CALL;
  }

  // JMP - depends on addressing mode
  if (mnem == M_JMP) {
    // Indirect jumps can't be traced statically
    if (info.mode == IND || info.mode == AIX) {
      return FlowType::INDIRECT;
    }
    // Absolute JMP is unconditional
    return FlowType::UNCONDITIONAL;
  }

  // BRA (65C02) - unconditional branch
  if (mnem == M_BRA) {
    return FlowType::UNCONDITIONAL;
  }

  // Conditional branches - Bxx instructions
  if (mnem == M_BPL || mnem == M_BMI || mnem == M_BVC || mnem == M_BVS ||
      mnem == M_BCC || mnem == M_BCS || mnem == M_BEQ || mnem == M_BNE) {
    return FlowType::CONDITIONAL;
  }

  // BBR/BBS (65C02) - conditional branches
  if ((mnem >= M_BBR0 && mnem <= M_BBR7) ||
      (mnem >= M_BBS0 && mnem <= M_BBS7)) {
    return FlowType::CONDITIONAL;
  }

  // Everything else is sequential
  return FlowType::SEQUENTIAL;
}

DisasmResult disassembleWithFlowAnalysis(const uint8_t *data, size_t size,
                                          uint16_t baseAddress,
                                          const std::vector<uint16_t> &entryPoints) {
  DisasmResult result;

  if (size == 0 || data == nullptr || entryPoints.empty()) {
    return result;
  }

  // Calculate valid address range
  uint16_t endAddress = static_cast<uint16_t>(baseAddress + size);

  // Track visited addresses to avoid re-processing
  std::unordered_set<uint16_t> visited;

  // Work queue of addresses to visit
  std::queue<uint16_t> toVisit;

  // Initialize with entry points
  for (uint16_t entry : entryPoints) {
    if (entry >= baseAddress && entry < endAddress) {
      toVisit.push(entry);
    }
  }

  // Reserve approximate space
  result.instructions.reserve(size / 2);

  // Process addresses until queue is empty
  while (!toVisit.empty()) {
    uint16_t addr = toVisit.front();
    toVisit.pop();

    // Skip if already visited or out of range
    if (visited.count(addr) || addr < baseAddress || addr >= endAddress) {
      continue;
    }

    // Mark as visited
    visited.insert(addr);

    // Calculate offset into data buffer
    size_t offset = addr - baseAddress;

    // Disassemble instruction at this address
    DisasmInstruction instr = disassembleInstruction(
      data + offset, size - offset, addr);

    if (instr.length == 0) {
      continue;
    }

    result.instructions.push_back(instr);

    // Determine what addresses to visit next based on flow type
    FlowType flow = getFlowType(instr.opcode);
    uint16_t nextAddr = static_cast<uint16_t>(addr + instr.length);

    switch (flow) {
      case FlowType::SEQUENTIAL:
        // Continue to next instruction
        if (nextAddr >= baseAddress && nextAddr < endAddress) {
          toVisit.push(nextAddr);
        }
        break;

      case FlowType::CONDITIONAL:
        // Add both target and fall-through
        if (nextAddr >= baseAddress && nextAddr < endAddress) {
          toVisit.push(nextAddr);
        }
        if (instr.target >= baseAddress && instr.target < endAddress) {
          toVisit.push(instr.target);
        }
        break;

      case FlowType::CALL:
        // JSR: add target AND return address (next instruction)
        if (nextAddr >= baseAddress && nextAddr < endAddress) {
          toVisit.push(nextAddr);
        }
        if (instr.target >= baseAddress && instr.target < endAddress) {
          toVisit.push(instr.target);
        }
        break;

      case FlowType::UNCONDITIONAL:
        // JMP/BRA: only add target, don't fall through
        if (instr.target >= baseAddress && instr.target < endAddress) {
          toVisit.push(instr.target);
        }
        break;

      case FlowType::RETURN:
      case FlowType::INDIRECT:
      case FlowType::HALT:
        // Stop tracing this path
        break;
    }
  }

  // Sort instructions by address for consistent output
  std::sort(result.instructions.begin(), result.instructions.end(),
            [](const DisasmInstruction &a, const DisasmInstruction &b) {
              return a.address < b.address;
            });

  return result;
}

DisasmResult disassembleWithFlowAnalysis(const uint8_t *data, size_t size,
                                          uint16_t baseAddress) {
  std::vector<uint16_t> entryPoints = {baseAddress};
  return disassembleWithFlowAnalysis(data, size, baseAddress, entryPoints);
}

} // namespace a2e
