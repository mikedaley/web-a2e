/*
 * cpu65816.hpp - 65C816 CPU emulation, the processor in an Apple IIgs
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <functional>

namespace a2e {

// ============================================================================
// Why this is not CPU6502 with more opcodes
//
// A 65816 is a different part wearing a compatible face. It has a 24-bit
// address bus, 16-bit accumulator and index registers whose widths change at
// runtime, a direct page that can sit anywhere in bank zero, a stack that can
// be anywhere in bank zero, separate banks for code and data, and a second
// operating mode. Emulation mode has to behave exactly like a 65C02 — that is
// the mode a IIgs boots in and the mode every //e program runs in — while
// native mode is a machine the 8-bit cores here have no vocabulary for.
//
// Folding that into CPU6502 would put a width test on every load, store and
// arithmetic op in the hottest loop of a //e, to serve a machine a //e is not.
// So this is its own class, and a machine is built from one or the other.
//
// Cycle counts are the documented base counts plus the documented adjustments —
// a 16-bit register costs a cycle, a direct page not aligned to a page costs a
// cycle, an index crossing a page costs a cycle where the index is 8 bits. What
// is not modelled is the internal cycle *pattern*: which cycle of an
// instruction touches which address. A //e's video reads the bus during those
// cycles, which is why CPU6502 models them; a IIgs's video does not read the
// 65816's bus at all — it reads the Mega II's, on the other side of the
// machine — so the count is what matters here and the pattern is not.
// ============================================================================

// Status flags. N, V, Z, C, I and D sit where a 6502's do; the two that differ
// are the ones that only mean something in native mode.
enum Status816 : uint8_t {
  FLAG816_C = 0x01, // Carry
  FLAG816_Z = 0x02, // Zero
  FLAG816_I = 0x04, // Interrupt disable
  FLAG816_D = 0x08, // Decimal
  FLAG816_X = 0x10, // Index registers are 8 bits (native); Break (emulation)
  FLAG816_M = 0x20, // Accumulator is 8 bits (native); unused, always 1 (emulation)
  FLAG816_V = 0x40, // Overflow
  FLAG816_N = 0x80  // Negative
};

// Vectors. A 65816 has two sets: the emulation ones a 6502 would recognise, and
// a native set eight bytes below them.
inline constexpr uint16_t VEC_E_NMI = 0xFFFA;
inline constexpr uint16_t VEC_E_RESET = 0xFFFC;
inline constexpr uint16_t VEC_E_IRQ = 0xFFFE; // and BRK, as on a 6502
inline constexpr uint16_t VEC_E_ABORT = 0xFFF8;
inline constexpr uint16_t VEC_E_COP = 0xFFF4;
inline constexpr uint16_t VEC_N_NMI = 0xFFEA;
inline constexpr uint16_t VEC_N_IRQ = 0xFFEE;
inline constexpr uint16_t VEC_N_BRK = 0xFFE6;
inline constexpr uint16_t VEC_N_ABORT = 0xFFE8;
inline constexpr uint16_t VEC_N_COP = 0xFFE4;

class CPU65816 {
public:
  // The bus is 24 bits wide: bank in the top eight, address in the low sixteen.
  using ReadCallback = std::function<uint8_t(uint32_t)>;
  using WriteCallback = std::function<void(uint32_t, uint8_t)>;
  using IRQStatusCallback = std::function<bool()>;

  CPU65816(ReadCallback read, WriteCallback write);

  // ===== Execution =====

  void reset();
  void executeInstruction();
  bool isStopped() const { return stopped_; }  // STP, until the next reset
  bool isWaiting() const { return waiting_; }  // WAI, until the next interrupt

  // ===== Interrupts =====

  void irq();
  void nmi();
  void setIRQStatusCallback(IRQStatusCallback cb) {
    irqStatusCallback_ = std::move(cb);
  }
  bool isIRQPending() const { return irqPending_; }
  bool isNMIPending() const { return nmiPending_; }

  // ===== Registers =====
  //
  // The accumulator is always held as sixteen bits. In eight-bit mode the low
  // half is A and the high half is B, and B survives everything done to A —
  // which is the whole point of XBA, and a core that threw it away would break
  // any program that used the hidden register as scratch.

  uint16_t getA() const { return a_; }
  uint16_t getX() const { return x_; }
  uint16_t getY() const { return y_; }
  uint16_t getSP() const { return sp_; }
  uint16_t getD() const { return d_; }
  uint16_t getPC() const { return pc_; }
  uint8_t getPBR() const { return pbr_; }
  uint8_t getDBR() const { return dbr_; }
  uint8_t getP() const { return p_; }
  bool getEmulation() const { return e_; }

  void setA(uint16_t v) { a_ = v; }
  void setX(uint16_t v) { x_ = v; }
  void setY(uint16_t v) { y_ = v; }
  void setSP(uint16_t v) { sp_ = v; }
  void setD(uint16_t v) { d_ = v; }
  void setPC(uint16_t v) { pc_ = v; }
  void setPBR(uint8_t v) { pbr_ = v; }
  void setDBR(uint8_t v) { dbr_ = v; }
  void setP(uint8_t v);
  void setEmulation(bool e);

  /** The full 24-bit address of the next instruction. */
  uint32_t getPCFull() const {
    return (static_cast<uint32_t>(pbr_) << 16) | pc_;
  }

  // ===== Widths =====
  //
  // Emulation mode forces both registers to eight bits whatever the flags say,
  // and this is the only place that rule is written down.

  bool accumulator8() const { return e_ || (p_ & FLAG816_M); }
  bool index8() const { return e_ || (p_ & FLAG816_X); }

  // ===== Cycles =====

  int getCycleCount() const { return cycleCount_; }
  uint64_t getTotalCycles() const { return totalCycles_; }
  void setTotalCycles(uint64_t c) { totalCycles_ = c; }
  void resetCycleCount() { totalCycles_ = 0; }

private:
  // ===== Bus =====

  uint8_t read8(uint32_t address);
  void write8(uint32_t address, uint8_t value);

  // A 16-bit access is two 8-bit ones, and where the second one lands is the
  // part worth being careful about: reads and writes through a data bank run
  // into the next bank at $FFFF, while direct page and stack accesses stay
  // inside bank zero.
  uint16_t read16(uint32_t address);
  uint16_t read16Wrapped(uint32_t address); // Stays inside its bank
  void write8or16(uint32_t address, uint16_t value, bool eightBit);
  uint16_t read8or16(uint32_t address, bool eightBit);

  uint8_t fetch8();
  uint16_t fetch16();
  uint32_t fetch24();

  // ===== Stack =====
  //
  // Bank zero always. In emulation mode the pointer is eight bits inside page
  // one and wraps there; in native mode it is a full sixteen and does not.

  void push8(uint8_t value);
  void push16(uint16_t value);
  uint8_t pop8();
  uint16_t pop16();

  // The instructions a 6502 never had do not honour the page-one wrap while
  // they run: they walk the stack pointer through all sixteen bits and the
  // high byte is put back to $01 when the instruction finishes. So PLD with
  // the pointer at $01FE really does read its second byte from $0200.
  void push8Wide(uint8_t value);
  void push16Wide(uint16_t value);
  uint8_t pop8Wide();
  uint16_t pop16Wide();
  void normaliseStack();

  // ===== Addressing =====
  //
  // Each returns the 24-bit effective address. Immediate returns the address of
  // the operand itself, in the program bank, and advances PC past it.

  uint32_t addrImmediate(bool eightBit);
  uint32_t addrDirect();
  uint32_t addrDirectX();
  uint32_t addrDirectY();
  uint32_t addrDirectIndirect();
  uint32_t addrDirectIndirectLong();
  uint32_t addrDirectIndexedIndirect(); // (dp,X)
  // Indexed reads pay a cycle for crossing a page; writes and read-modify-
  // writes pay it every time, and their base count already includes it. So the
  // penalty is asked for rather than assumed.
  uint32_t addrDirectIndirectIndexed(bool pageCrossPenalty = true); // (dp),Y
  uint32_t addrDirectIndirectLongIndexed();
  uint32_t addrAbsolute();
  uint32_t addrAbsoluteX(bool pageCrossPenalty = true);
  uint32_t addrAbsoluteY(bool pageCrossPenalty = true);
  uint32_t addrAbsoluteLong();
  uint32_t addrAbsoluteLongX();
  uint32_t addrStackRelative();
  uint32_t addrStackRelativeIndirectIndexed();

  // ===== Flags =====

  void setFlag(uint8_t flag, bool value) {
    p_ = value ? (p_ | flag) : (p_ & ~flag);
  }
  bool getFlag(uint8_t flag) const { return (p_ & flag) != 0; }
  void updateNZ8(uint8_t value);
  void updateNZ16(uint16_t value);
  void updateNZ(uint16_t value, bool eightBit);

  // Emulation mode and the width flags constrain the registers; this is what
  // enforces it after anything that could have changed either.
  void applyWidthConstraints();

  // ===== Operations =====

  void opADC(uint32_t ea);
  void opSBC(uint32_t ea);
  void opAND(uint32_t ea);
  void opORA(uint32_t ea);
  void opEOR(uint32_t ea);
  void opBIT(uint32_t ea, bool immediate);
  void opCMP(uint32_t ea);
  void opCPX(uint32_t ea);
  void opCPY(uint32_t ea);
  void opLDA(uint32_t ea);
  void opLDX(uint32_t ea);
  void opLDY(uint32_t ea);
  void opSTA(uint32_t ea);
  void opSTX(uint32_t ea);
  void opSTY(uint32_t ea);
  void opSTZ(uint32_t ea);
  void opINC(uint32_t ea);
  void opDEC(uint32_t ea);
  void opASL(uint32_t ea);
  void opLSR(uint32_t ea);
  void opROL(uint32_t ea);
  void opROR(uint32_t ea);
  void opTSB(uint32_t ea);
  void opTRB(uint32_t ea);
  void opASLAcc();
  void opLSRAcc();
  void opROLAcc();
  void opRORAcc();
  void opMVN(bool negative);

  void branch(bool condition);
  void interrupt(uint16_t nativeVector, uint16_t emulationVector,
                 bool software);

  void executeOpcode(uint8_t opcode);

  // ===== State =====

  uint16_t a_ = 0;      // Accumulator: B in the high half, A in the low
  uint16_t x_ = 0;      // X index
  uint16_t y_ = 0;      // Y index
  uint16_t sp_ = 0x01FF; // Stack pointer, always in bank zero
  uint16_t d_ = 0;      // Direct page register
  uint16_t pc_ = 0;     // Program counter
  uint8_t pbr_ = 0;     // Program bank
  uint8_t dbr_ = 0;     // Data bank
  uint8_t p_ = FLAG816_M | FLAG816_X | FLAG816_I;
  bool e_ = true;       // Emulation mode: how the machine starts

  // Whether the operand the next access reads or writes stays inside its bank.
  //
  // Two kinds of address behave differently at the top of a bank. An immediate
  // operand comes out of the program bank and a direct page or stack operand
  // out of bank zero, and neither of those banks ever increments: LDA #$1234 at
  // $CF:FFFE takes its high byte from $CF:0000, and LDA $07 with the direct
  // page at $FFF8 takes its high byte from $00:0000. An operand reached through
  // the data bank does cross into the next one.
  //
  // Every addressing mode says which it produced, and the next read or write
  // consumes the answer. A rule read off the address instead could not tell
  // them apart — they are the same number.
  bool operandWrapsInBank_ = false;

  bool stopped_ = false;
  bool waiting_ = false;

  int cycleCount_ = 0;
  uint64_t totalCycles_ = 0;

  bool irqPending_ = false;
  bool nmiPending_ = false;

  ReadCallback read_;
  WriteCallback write_;
  IRQStatusCallback irqStatusCallback_;
};

} // namespace a2e
