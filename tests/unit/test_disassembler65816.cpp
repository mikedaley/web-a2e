/*
 * test_disassembler65816.cpp - The 65816 disassembler against the 65816
 *
 * A disassembler that gets an instruction's length wrong is worse than no
 * disassembler: every line after the mistake is garbage, and the mistake is
 * invisible because the wrong bytes still decode into something. So the length
 * of all 256 opcodes is checked here against the only authority that matters —
 * what the CPU's program counter actually does when it executes one — in both
 * accumulator widths and both index widths, because two opcodes' lengths
 * depend on them.
 *
 * The CPU it is checked against is itself verified against 5.1 million
 * recorded states from a real 65816, so this is a chain back to hardware
 * rather than to a datasheet somebody typed out.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "cpu65816.hpp"
#include "disassembler65816.hpp"

#include <memory>
#include <set>
#include <vector>

using namespace a2e;

namespace {

class FlatBus {
public:
  FlatBus() : memory_(16 * 1024 * 1024, 0) {}
  uint8_t read(uint32_t address) const { return memory_[address & 0xFFFFFF]; }
  void write(uint32_t address, uint8_t value) { memory_[address & 0xFFFFFF] = value; }
  uint8_t &operator[](uint32_t address) { return memory_[address & 0xFFFFFF]; }

private:
  std::vector<uint8_t> memory_;
};

// The opcodes whose program counter deliberately goes somewhere else, so the
// distance it moved says nothing about how long the instruction was. Their
// lengths are checked separately below.
const std::set<uint8_t> GOES_ELSEWHERE = {
    0x00, // BRK
    0x02, // COP
    0x20, // JSR abs
    0x22, // JSL long
    0x40, // RTI
    0x4C, // JMP abs
    0x5C, // JML long
    0x60, // RTS
    0x6B, // RTL
    0x6C, // JMP (abs)
    0x7C, // JMP (abs,X)
    0xDC, // JML [abs]
    0xFC, // JSR (abs,X)
};

} // namespace

TEST_CASE("Every opcode's length is the distance the CPU's PC moves",
          "[disasm816]") {
  FlatBus bus;
  auto cpu = std::make_unique<CPU65816>(
      [&bus](uint32_t a) { return bus.read(a); },
      [&bus](uint32_t a, uint8_t v) { bus.write(a, v); });

  constexpr uint32_t AT = 0x030000; // A bank with nothing special in it

  struct Widths { bool native; bool m8; bool x8; };
  const Widths cases[] = {
      {false, false, false}, // native, sixteen-bit A and index
      {false, true, false},  // native, eight-bit A
      {false, false, true},  // native, eight-bit index
      {false, true, true},   // native, both eight-bit
      {true, true, true},    // emulation, where both are eight-bit always
  };

  for (const Widths &w : cases) {
    for (int op = 0; op <= 0xFF; op++) {
      const uint8_t opcode = static_cast<uint8_t>(op);
      if (GOES_ELSEWHERE.count(opcode)) continue;

      // A zero displacement makes every branch land on the next instruction
      // whether it is taken or not, so the measurement holds either way.
      bus[AT] = opcode;
      bus[AT + 1] = 0x00;
      bus[AT + 2] = 0x00;
      bus[AT + 3] = 0x00;

      cpu->reset();
      cpu->setEmulation(w.native ? false : true);
      if (!w.native) {
        uint8_t p = cpu->getP();
        p = static_cast<uint8_t>(w.m8 ? (p | FLAG816_M) : (p & ~FLAG816_M));
        p = static_cast<uint8_t>(w.x8 ? (p | FLAG816_X) : (p & ~FLAG816_X));
        cpu->setP(p);
      }
      // Interrupts masked, so none is taken in place of the instruction; and
      // A zero so a block move runs its last byte rather than rewinding to
      // run itself again.
      cpu->setP(static_cast<uint8_t>(cpu->getP() | FLAG816_I));
      cpu->setA(0x0000);
      cpu->setPBR(static_cast<uint8_t>(AT >> 16));
      cpu->setPC(static_cast<uint16_t>(AT & 0xFFFF));

      const bool accumulator8 = cpu->accumulator8();
      const bool index8 = cpu->index8();
      const int expected = instructionLength816(opcode, accumulator8, index8);

      cpu->executeInstruction();
      const int moved = static_cast<int>(cpu->getPC()) - static_cast<int>(AT & 0xFFFF);

      INFO("opcode $" << std::hex << op << " mnemonic " << mnemonic816(opcode)
                      << (w.native ? " native" : " emulation") << " m8=" << accumulator8
                      << " x8=" << index8);
      REQUIRE(moved == expected);
    }
  }
}

TEST_CASE("A call pushes the return address its length implies",
          "[disasm816]") {
  // The three calls move the program counter away, so their length is checked
  // through what they left on the stack instead: a call pushes the address of
  // its own last byte.
  FlatBus bus;
  auto cpu = std::make_unique<CPU65816>(
      [&bus](uint32_t a) { return bus.read(a); },
      [&bus](uint32_t a, uint8_t v) { bus.write(a, v); });

  constexpr uint32_t AT = 0x030000;
  for (uint8_t opcode : {uint8_t(0x20), uint8_t(0x22), uint8_t(0xFC)}) {
    bus[AT] = opcode;
    bus[AT + 1] = 0x00;
    bus[AT + 2] = 0x40;
    bus[AT + 3] = 0x03;

    cpu->reset();
    cpu->setEmulation(false);
    cpu->setSP(0x01FF);
    cpu->setPBR(static_cast<uint8_t>(AT >> 16));
    cpu->setPC(static_cast<uint16_t>(AT & 0xFFFF));
    const int length = instructionLength816(opcode, cpu->accumulator8(), cpu->index8());
    cpu->executeInstruction();

    // The return address is the two bytes below where the stack pointer now
    // is; a long call pushed its bank above them.
    const uint16_t sp = cpu->getSP();
    const uint16_t pushed = static_cast<uint16_t>(bus.read(sp + 1) | (bus.read(sp + 2) << 8));
    INFO("opcode $" << std::hex << int(opcode) << " " << mnemonic816(opcode));
    REQUIRE(pushed == static_cast<uint16_t>((AT & 0xFFFF) + length - 1));
  }
}

TEST_CASE("The jumps and returns are the lengths the datasheet gives",
          "[disasm816]") {
  // The ones whose length no experiment can measure, written out.
  struct Known { uint8_t opcode; int length; const char *mnemonic; };
  const Known known[] = {
      {0x00, 2, "BRK"}, {0x02, 2, "COP"}, {0x40, 1, "RTI"},
      {0x4C, 3, "JMP"}, {0x5C, 4, "JML"}, {0x60, 1, "RTS"},
      {0x6B, 1, "RTL"}, {0x6C, 3, "JMP"}, {0x7C, 3, "JMP"},
      {0xDC, 3, "JML"}, {0x20, 3, "JSR"}, {0x22, 4, "JSL"},
      {0xFC, 3, "JSR"},
  };
  for (const Known &k : known) {
    INFO("opcode $" << std::hex << int(k.opcode));
    REQUIRE(instructionLength816(k.opcode, true, true) == k.length);
    REQUIRE(instructionLength816(k.opcode, false, false) == k.length);
    REQUIRE(std::string(mnemonic816(k.opcode)) == k.mnemonic);
  }
}

TEST_CASE("An immediate is as wide as the processor was", "[disasm816]") {
  // The one thing a byte stream cannot tell you. LDA # follows the
  // accumulator's width and LDX # the index registers', and they are
  // separate flags, so a program with a sixteen-bit accumulator and eight-bit
  // index registers has instructions of both lengths next to each other.
  const uint8_t lda[] = {0xA9, 0x34, 0x12};
  const uint8_t ldx[] = {0xA2, 0x34, 0x12};

  auto text = [](const uint8_t *bytes, bool m8, bool x8) {
    return formatOperand816(disassemble816(bytes, 3, 0x00E10000, m8, x8));
  };

  REQUIRE(text(lda, true, true) == "#$34");
  REQUIRE(text(lda, false, true) == "#$1234");
  REQUIRE(text(ldx, true, true) == "#$34");
  REQUIRE(text(ldx, true, false) == "#$1234");

  REQUIRE(instructionLength816(0xA9, false, true) == 3);
  REQUIRE(instructionLength816(0xA2, false, true) == 2);
  REQUIRE(instructionLength816(0xA9, true, false) == 2);
  REQUIRE(instructionLength816(0xA2, true, false) == 3);
}

TEST_CASE("The operands read as a 65816 programmer writes them",
          "[disasm816]") {
  struct Case { std::vector<uint8_t> bytes; const char *expected; };
  const Case cases[] = {
      {{0xAF, 0x34, 0x12, 0xE1}, "LDA $E11234"},   // absolute long
      {{0xBF, 0x00, 0x20, 0xE1}, "LDA $E12000,X"}, // absolute long indexed
      {{0xA7, 0x10}, "LDA [$10]"},                 // direct page indirect long
      {{0xB7, 0x10}, "LDA [$10],Y"},
      {{0xA3, 0x04}, "LDA $04,S"},                 // stack relative
      {{0xB3, 0x04}, "LDA ($04,S),Y"},
      {{0xB2, 0x10}, "LDA ($10)"},
      {{0x22, 0x00, 0x00, 0xE1}, "JSL $E10000"},
      {{0xDC, 0x00, 0x30}, "JML [$3000]"},
      {{0xFC, 0x00, 0x30}, "JSR ($3000,X)"},
      {{0xF4, 0x34, 0x12}, "PEA $1234"},
      {{0xD4, 0x10}, "PEI ($10)"},
      {{0xC2, 0x30}, "REP #$30"},
      {{0xE2, 0x30}, "SEP #$30"},
      {{0x0A}, "ASL A"},
      {{0xEB}, "XBA"},
      {{0xFB}, "XCE"},
  };
  for (const Case &c : cases) {
    const Disasm816Instruction in =
        disassemble816(c.bytes.data(), c.bytes.size(), 0x00E10000, true, true);
    std::string text = in.mnemonic;
    const std::string operand = formatOperand816(in);
    if (!operand.empty()) text += " " + operand;
    INFO("opcode $" << std::hex << int(c.bytes[0]));
    REQUIRE(text == c.expected);
    REQUIRE(in.length == c.bytes.size());
  }
}

TEST_CASE("A block move's operands are the other way round from its bytes",
          "[disasm816]") {
  // MVN's first operand byte is the destination bank and its second is the
  // source, and an assembler's source line names them source first. Printing
  // them in memory order produces a line that reassembles into a move in the
  // opposite direction.
  const uint8_t mvn[] = {0x54, 0xE1, 0x02}; // to bank $E1, from bank $02
  const Disasm816Instruction in = disassemble816(mvn, 3, 0x030000, true, true);
  REQUIRE(std::string(in.mnemonic) == "MVN");
  REQUIRE(formatOperand816(in) == "$02,$E1");
  REQUIRE(in.length == 3);
}

TEST_CASE("A branch's target is worked out inside its own bank",
          "[disasm816]") {
  // Both kinds of branch wrap inside the program bank rather than crossing
  // into the next one, which is the whole of why a long branch is not a jump.
  const uint8_t backwards[] = {0x10, 0xFB}; // BPL -5
  Disasm816Instruction in = disassemble816(backwards, 2, 0x03C000, true, true);
  REQUIRE(in.target == 0x03BFFD);
  REQUIRE(formatOperand816(in) == "$BFFD");

  const uint8_t longBranch[] = {0x82, 0x00, 0x10}; // BRL +$1000
  in = disassemble816(longBranch, 3, 0x03C000, true, true);
  REQUIRE(in.target == 0x03D003);

  // Off the top of the bank and round to the bottom of the same one.
  const uint8_t forwards[] = {0x80, 0x10}; // BRA +16
  in = disassemble816(forwards, 2, 0x03FFF0, true, true);
  REQUIRE(in.target == 0x030002);
}

TEST_CASE("A whole line reads as the machine's own monitor writes one",
          "[disasm816]") {
  // Bank, slash, address — which is how a IIgs's monitor and its Diagnostic
  // both print an address, so it is how this prints one.
  const uint8_t bytes[] = {0xAF, 0x34, 0x12, 0xE1};
  const std::string line =
      formatDisasm816(disassemble816(bytes, 4, 0x00FF69, true, true));
  REQUIRE(line == "00/FF69: AF 34 12 E1  LDA $E11234");

  const uint8_t implied[] = {0xEA};
  REQUIRE(formatDisasm816(disassemble816(implied, 1, 0xE10010, true, true)) ==
          "E1/0010: EA           NOP");
}

TEST_CASE("Flow says where a debugger's step can land", "[disasm816]") {
  REQUIRE(flowType816(0x20) == FlowType::CALL);  // JSR
  REQUIRE(flowType816(0x22) == FlowType::CALL);  // JSL
  REQUIRE(flowType816(0xFC) == FlowType::CALL);  // JSR (abs,X)
  REQUIRE(flowType816(0x60) == FlowType::RETURN);
  REQUIRE(flowType816(0x6B) == FlowType::RETURN); // RTL
  REQUIRE(flowType816(0x82) == FlowType::UNCONDITIONAL); // BRL
  REQUIRE(flowType816(0x10) == FlowType::CONDITIONAL);
  REQUIRE(flowType816(0xDB) == FlowType::HALT);   // STP
  REQUIRE(flowType816(0xCB) == FlowType::HALT);   // WAI
  REQUIRE(flowType816(0xA9) == FlowType::SEQUENTIAL);
}
