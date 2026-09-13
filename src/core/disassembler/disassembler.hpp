/*
 * disassembler.hpp - 65C02 instruction disassembler interface
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace a2e {

/**
 * Standalone 6502/65C02 disassembler
 *
 * Returns raw structured data for frontend rendering.
 */

// Addressing modes
enum class AddrMode : uint8_t {
  IMP = 0,   // Implied
  ACC = 1,   // Accumulator
  IMM = 2,   // Immediate #$nn
  ZP = 3,    // Zero Page $nn
  ZPX = 4,   // Zero Page,X $nn,X
  ZPY = 5,   // Zero Page,Y $nn,Y
  ABS = 6,   // Absolute $nnnn
  ABX = 7,   // Absolute,X $nnnn,X
  ABY = 8,   // Absolute,Y $nnnn,Y
  IND = 9,   // Indirect ($nnnn)
  IZX = 10,  // Indexed Indirect ($nn,X)
  IZY = 11,  // Indirect Indexed ($nn),Y
  REL = 12,  // Relative (branches)
  ZPI = 13,  // Zero Page Indirect ($nn) - 65C02
  AIX = 14,  // Absolute Indexed Indirect ($nnnn,X) - 65C02
  ZPR = 15   // Zero Page Relative (BBR/BBS) - 65C02
};

// Instruction categories for syntax highlighting
enum class InstrCategory : uint8_t {
  BRANCH = 0,   // Jumps and branches
  LOAD = 1,     // Load/store
  MATH = 2,     // Math and logic
  STACK = 3,    // Stack and register operations
  FLAG = 4,     // Flag operations
  UNKNOWN = 5   // Unknown/illegal opcodes
};

// Opcode info structure (maps opcode byte to mnemonic, mode, category)
struct OpcodeInfo {
  uint8_t mnemonicIndex;
  uint8_t mode;      // AddrMode enum value
  uint8_t category;  // InstrCategory enum value
};

/**
 * Get pointer to the 256-entry opcode table
 */
const OpcodeInfo* getOpcodeTable();

/**
 * Get the number of mnemonics in the mnemonic table
 */
int getMnemonicCount();

// Disassembled instruction data - 16 bytes per instruction
// Layout: [address:2][target:2][length:1][opcode:1][op1:1][op2:1][mode:1][cat:1][mnem:4][pad:2]
struct DisasmInstruction {
  uint16_t address;       // offset 0-1: Memory address
  uint16_t target;        // offset 2-3: Branch/jump target address
  uint8_t length;         // offset 4: Instruction length (1-3)
  uint8_t opcode;         // offset 5: Opcode byte
  uint8_t operand1;       // offset 6: First operand byte
  uint8_t operand2;       // offset 7: Second operand byte
  uint8_t mode;           // offset 8: AddrMode enum value
  uint8_t category;       // offset 9: InstrCategory enum value
  char mnemonic[4];       // offset 10-13: Mnemonic (null-padded)
  uint8_t padding[2];     // offset 14-15: Alignment padding
};

// Result structure for disassembly
struct DisasmResult {
  std::vector<DisasmInstruction> instructions;
};

/**
 * Get the length of an instruction given its opcode
 */
int getInstructionLength(uint8_t opcode);

/**
 * Get mnemonic string for an opcode
 */
const char* getMnemonic(uint8_t opcode);

/**
 * Get mnemonic string by index (for use with DisasmInstruction.mnemonicIndex)
 */
const char* getMnemonicByIndex(uint8_t index);

/**
 * Get addressing mode for an opcode
 */
AddrMode getAddressingMode(uint8_t opcode);

/**
 * Get instruction category for an opcode
 */
InstrCategory getInstructionCategory(uint8_t opcode);

/**
 * Disassemble a single instruction from raw data
 */
DisasmInstruction disassembleInstruction(const uint8_t *data, size_t size,
                                          uint16_t address);

/**
 * Disassemble a block of raw binary data (linear)
 */
DisasmResult disassembleBlock(const uint8_t *data, size_t size,
                              uint16_t baseAddress);

/**
 * Instruction flow classification
 */
enum class FlowType : uint8_t {
  SEQUENTIAL = 0,     // Continue to next instruction
  CONDITIONAL = 1,    // Branch may or may not be taken (Bxx, BBRx, BBSx)
  UNCONDITIONAL = 2,  // Always transfers control (JMP abs, BRA)
  CALL = 3,           // Subroutine call (JSR) - returns to next instruction
  RETURN = 4,         // Returns via stack (RTS, RTI)
  INDIRECT = 5,       // Indirect jump - target unknown (JMP indirect)
  HALT = 6            // Stops execution (BRK, STP, WAI)
};

/**
 * Get the flow type of an instruction
 */
FlowType getFlowType(uint8_t opcode);

/**
 * The operand as a programmer writes it: "#$12", "($10),Y", "$1234,X".
 *
 * Formatting an operand from a decoded instruction rather than from a live
 * address is what a trace needs — the bytes are already in hand and the
 * machine has moved on since. It exists here so there is one operand
 * formatter per processor rather than one per place that wants one.
 */
std::string formatOperand(const DisasmInstruction &instruction);

/**
 * Disassemble using recursive descent / control flow analysis
 *
 * This approach traces execution paths from entry points to better
 * distinguish code from data:
 * 1. Decode instruction at entry point
 * 2. If sequential, continue to next instruction
 * 3. If branch/jump, add target to visit list
 * 4. If JSR, add target AND return address
 * 5. If RTS/RTI/indirect jump, stop this path
 * 6. Repeat until no more addresses to visit
 *
 * @param data Raw binary data
 * @param size Size of data in bytes
 * @param baseAddress Memory address of first byte
 * @param entryPoints List of known entry points to start tracing from
 * @return Disassembly result with instructions sorted by address
 */
DisasmResult disassembleWithFlowAnalysis(const uint8_t *data, size_t size,
                                          uint16_t baseAddress,
                                          const std::vector<uint16_t> &entryPoints);

/**
 * Convenience overload with single entry point (defaults to baseAddress)
 */
DisasmResult disassembleWithFlowAnalysis(const uint8_t *data, size_t size,
                                          uint16_t baseAddress);

} // namespace a2e
