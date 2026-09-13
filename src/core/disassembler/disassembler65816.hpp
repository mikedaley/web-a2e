/*
 * disassembler65816.hpp - 65C816 instruction disassembler
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "disassembler.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace a2e {

/**
 * A 65816 disassembler, which is a different disassembler and not a wider one.
 *
 * Three things make it so, and each of them is a reason it could not be the
 * 65C02's table with entries added:
 *
 * - **All 256 opcodes are instructions.** A 65C02 disassembler can print "???"
 *   and move on a byte at a time; here there is no such thing as an illegal
 *   opcode, so every byte of a code stream means something.
 * - **An instruction's length depends on the processor's state.** `LDA #` is
 *   two bytes with an eight-bit accumulator and three bytes with a sixteen-bit
 *   one, and the same for the index registers, so the M and X flags are an
 *   input to disassembly. Get them wrong and every instruction after the first
 *   immediate is garbage — which is why the host passes the live flags in
 *   rather than assuming.
 * - **Addresses are 24 bits and there are modes a 6502 never had:** long
 *   absolute, long indirect, stack relative, block move.
 *
 * The table is the one in `cpu65816_dispatch.cpp`, read off that file rather
 * than typed from a datasheet: the CPU's dispatch is verified against 5.1
 * million recorded states from real hardware, so it is the better authority.
 * `test_disassembler65816.cpp` then checks the lengths here against what the
 * CPU's program counter actually does, opcode by opcode and in both widths, so
 * the two cannot drift.
 */

// ===== Addressing modes =====
//
// Deliberately a separate enumeration from AddrMode: the names overlap but the
// meanings do not (a 65816's "direct page" is not a 6502's "zero page" — it
// moves), and a host that had to tell which processor an enumerator came from
// would get it wrong eventually.
enum class AddrMode816 : uint8_t {
  Implied = 0,                     // TAX
  Accumulator,                     // ASL A
  ImmediateByte,                   // #$nn — always one byte (REP, SEP, COP)
  ImmediateA,                      // #$nn or #$nnnn, as the M flag says
  ImmediateIndex,                  // #$nn or #$nnnn, as the X flag says
  Direct,                          // $nn
  DirectX,                         // $nn,X
  DirectY,                         // $nn,Y
  DirectIndirect,                  // ($nn)
  DirectIndexedIndirect,           // ($nn,X)
  DirectIndirectIndexed,           // ($nn),Y
  DirectIndirectLong,              // [$nn]
  DirectIndirectLongIndexed,       // [$nn],Y
  Absolute,                        // $nnnn
  AbsoluteX,                       // $nnnn,X
  AbsoluteY,                       // $nnnn,Y
  AbsoluteLong,                    // $nnnnnn
  AbsoluteLongX,                   // $nnnnnn,X
  AbsoluteIndirect,                // ($nnnn)
  AbsoluteIndexedIndirect,         // ($nnnn,X)
  AbsoluteIndirectLong,            // [$nnnn]
  StackRelative,                   // $nn,S
  StackRelativeIndirectIndexed,    // ($nn,S),Y
  Relative,                        // branch, one signed byte
  RelativeLong,                    // BRL and PER, two signed bytes
  BlockMove,                       // MVN and MVP: #$ss,#$dd
};

/**
 * One disassembled instruction.
 *
 * Addresses are 24-bit and there are up to three operand bytes, which is what
 * separates this from DisasmInstruction rather than any difference of purpose.
 */
struct Disasm816Instruction {
  uint32_t address = 0;  // Where the instruction is, bank included
  uint32_t target = 0;   // Where it goes, for a branch or a jump; else address
  uint8_t length = 1;    // 1 to 4 bytes
  uint8_t opcode = 0;
  uint8_t operands[3] = {0, 0, 0};
  AddrMode816 mode = AddrMode816::Implied;
  InstrCategory category = InstrCategory::UNKNOWN;
  char mnemonic[5] = {0, 0, 0, 0, 0};
};

/** The mnemonic, which every one of the 256 opcodes has. */
const char *mnemonic816(uint8_t opcode);

AddrMode816 addressingMode816(uint8_t opcode);
InstrCategory category816(uint8_t opcode);

/** How the instruction moves the program counter, for a debugger's stepping. */
FlowType flowType816(uint8_t opcode);

/**
 * How many bytes the instruction occupies, with the processor in this state.
 *
 * `accumulator8` and `index8` are the CPU's current widths — emulation mode or
 * the M and X flags — because two opcodes' lengths depend on them and nothing
 * in the byte stream says which.
 */
int instructionLength816(uint8_t opcode, bool accumulator8, bool index8);

/** Operand bytes alone: the length without the opcode. */
int operandBytes816(AddrMode816 mode, bool accumulator8, bool index8);

/**
 * Disassemble one instruction out of `bytes`.
 *
 * `size` is how many bytes are readable; a truncated instruction is returned
 * with whatever operands were available and its real length, so a caller
 * stepping forward stays in step with the processor.
 */
Disasm816Instruction disassemble816(const uint8_t *bytes, size_t size,
                                    uint32_t address, bool accumulator8,
                                    bool index8);

/** The operand as a monitor would write it: "#$1234", "[$10],Y", "$01/2000". */
std::string formatOperand816(const Disasm816Instruction &instruction);

/**
 * The whole line, as "BB/AAAA: xx xx xx xx  MNEM operand".
 *
 * The bank and a slash is how a IIgs's own monitor and its Diagnostic write an
 * address, so it is how this writes one.
 */
std::string formatDisasm816(const Disasm816Instruction &instruction);

} // namespace a2e
