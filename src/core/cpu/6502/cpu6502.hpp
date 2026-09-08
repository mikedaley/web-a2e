/*
 * cpu6502.hpp - Cycle-accurate 65C02 CPU emulation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../../types.hpp"
#include <array>
#include <cstdint>
#include <functional>

namespace a2e {

// CPUVariant is defined in types.hpp so a machine profile can name a CPU
// without including this header.

// Status flag bits
enum StatusFlag : uint8_t {
  FLAG_C = 0x01, // Carry
  FLAG_Z = 0x02, // Zero
  FLAG_I = 0x04, // Interrupt disable
  FLAG_D = 0x08, // Decimal mode
  FLAG_B = 0x10, // Break
  FLAG_U = 0x20, // Unused (always 1)
  FLAG_V = 0x40, // Overflow
  FLAG_N = 0x80  // Negative
};

class CPU6502 {
public:
  using ReadCallback = std::function<uint8_t(uint16_t)>;
  using WriteCallback = std::function<void(uint16_t, uint8_t)>;
  using IRQStatusCallback = std::function<bool()>;

  CPU6502(ReadCallback read, WriteCallback write,
          CPUVariant variant = CPUVariant::CMOS_65C02);

  // Execution
  void reset();
  void executeInstruction();
  void step(); // Single cycle step
  bool isInstructionComplete() const { return cycleCount_ == 0; }

  // Interrupts
  void irq();
  void nmi();

  // Set callback for polling IRQ status (level-triggered IRQs like VIA)
  void setIRQStatusCallback(IRQStatusCallback cb) { irqStatusCallback_ = std::move(cb); }

  // Register access
  uint8_t getA() const { return a_; }
  uint8_t getX() const { return x_; }
  uint8_t getY() const { return y_; }
  uint8_t getSP() const { return sp_; }
  uint16_t getPC() const { return pc_; }
  uint8_t getP() const { return p_; }

  void setA(uint8_t v) { a_ = v; }
  void setX(uint8_t v) { x_ = v; }
  void setY(uint8_t v) { y_ = v; }
  void setSP(uint8_t v) { sp_ = v; }
  void setPC(uint16_t v) { pc_ = v; }
  void setP(uint8_t v) { p_ = v; }

  // Flag access
  bool getFlag(StatusFlag flag) const { return (p_ & flag) != 0; }
  void setFlag(StatusFlag flag, bool value) {
    if (value)
      p_ |= flag;
    else
      p_ &= ~flag;
  }

  // Cycle counting
  // During instruction execution (cycleCount_ > 0), return the cycle of
  // the last bus access — this matches when the 6502 performs the effective
  // memory read/write that triggers soft switch callbacks.
  // Between instructions (cycleCount_ == 0), return the plain total.
  uint64_t getTotalCycles() const {
    return cycleCount_ > 0 ? totalCycles_ + cycleCount_ - 1 : totalCycles_;
  }
  void resetCycleCount() { totalCycles_ = 0; }
  void setTotalCycles(uint64_t cycles) { totalCycles_ = cycles; }

  // Interrupt state access
  bool isIRQPending() const { return irqPending_; }
  bool isNMIPending() const { return nmiPending_; }
  bool isNMIEdge() const { return nmiEdge_; }

  // Debugging
  std::string disassembleAt(uint16_t address) const;
  uint8_t peekMemory(uint16_t address) const { return read_(address); }

private:
  // Memory access
  uint8_t read(uint16_t address);
  void write(uint16_t address, uint8_t value);
  uint8_t fetch();
  uint16_t fetchWord();

  // Stack operations
  void push(uint8_t value);
  uint8_t pop();
  void pushWord(uint16_t value);
  uint16_t popWord();

  // Flag helpers
  void updateNZ(uint8_t value);
  void compare(uint8_t reg, uint8_t value);

  // Addressing modes - return effective address
  uint16_t addrImmediate();
  uint16_t addrZeroPage();
  uint16_t addrZeroPageX();
  uint16_t addrZeroPageY();
  uint16_t addrAbsolute();
  uint16_t addrAbsoluteX(bool checkPage = true);
  uint16_t addrAbsoluteY(bool checkPage = true);
  uint16_t addrIndirect();
  uint16_t addrIndexedIndirect();                      // (zp,X)
  uint16_t addrIndirectIndexed(bool checkPage = true); // (zp),Y
  uint16_t addrIndirectZP();                           // 65C02: (zp)

  // Instruction implementations
  void opADC(uint8_t value);
  void opSBC(uint8_t value);
  void opAND(uint8_t value);
  void opORA(uint8_t value);
  void opEOR(uint8_t value);
  void opCMP(uint8_t value);
  void opCPX(uint8_t value);
  void opCPY(uint8_t value);
  void opBIT(uint8_t value);
  void opASL_A();
  uint8_t opASL(uint8_t value);
  void opLSR_A();
  uint8_t opLSR(uint8_t value);
  void opROL_A();
  uint8_t opROL(uint8_t value);
  void opROR_A();
  uint8_t opROR(uint8_t value);
  uint8_t opINC(uint8_t value);
  uint8_t opDEC(uint8_t value);

  // Branch helper
  void branch(bool condition);

  // Execute the current opcode
  void executeOpcode(uint8_t opcode);

  // Registers
  uint8_t a_ = 0;     // Accumulator
  uint8_t x_ = 0;     // X index
  uint8_t y_ = 0;     // Y index
  uint8_t sp_ = 0xFF; // Stack pointer
  uint16_t pc_ = 0;   // Program counter
  uint8_t p_ = 0x24;  // Status register (U always set, I set on reset)

  // Cycle tracking
  int cycleCount_ = 0;
  uint64_t totalCycles_ = 0;
  bool pageCrossed_ = false;

  // Interrupt state
  bool irqPending_ = false;
  bool nmiPending_ = false;
  bool nmiEdge_ = false;

  // Memory callbacks
  ReadCallback read_;
  WriteCallback write_;

  // IRQ status callback for level-triggered IRQs (VIA)
  IRQStatusCallback irqStatusCallback_;

  // CPU variant
  CPUVariant variant_;

  // Opcode table for cycle counts
  static constexpr std::array<uint8_t, 256> CYCLE_TABLE = {{
      7, 6, 2, 2, 5, 3, 5, 5, 3, 2, 2, 2, 6, 4, 6, 5, // 00-0F
      2, 5, 5, 2, 5, 4, 6, 5, 2, 4, 2, 2, 6, 4, 6, 5, // 10-1F
      6, 6, 2, 2, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 5, // 20-2F
      2, 5, 5, 2, 4, 4, 6, 5, 2, 4, 2, 2, 4, 4, 6, 5, // 30-3F
      6, 6, 2, 2, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 5, // 40-4F
      2, 5, 5, 2, 4, 4, 6, 5, 2, 4, 3, 2, 8, 4, 6, 5, // 50-5F
      6, 6, 2, 2, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 5, // 60-6F
      2, 5, 5, 2, 4, 4, 6, 5, 2, 4, 4, 2, 6, 4, 6, 5, // 70-7F
      3, 6, 2, 2, 3, 3, 3, 5, 2, 2, 2, 2, 4, 4, 4, 5, // 80-8F
      2, 6, 5, 2, 4, 4, 4, 5, 2, 5, 2, 2, 4, 5, 5, 5, // 90-9F
      2, 6, 2, 2, 3, 3, 3, 5, 2, 2, 2, 2, 4, 4, 4, 5, // A0-AF
      2, 5, 5, 2, 4, 4, 4, 5, 2, 4, 2, 2, 4, 4, 4, 5, // B0-BF
      2, 6, 2, 2, 3, 3, 5, 5, 2, 2, 2, 3, 4, 4, 6, 5, // C0-CF
      2, 5, 5, 2, 4, 4, 6, 5, 2, 4, 3, 3, 4, 4, 7, 5, // D0-DF
      2, 6, 2, 2, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 5, // E0-EF
      2, 5, 5, 2, 4, 4, 6, 5, 2, 4, 4, 2, 4, 4, 7, 5  // F0-FF
  }};
};

} // namespace a2e
