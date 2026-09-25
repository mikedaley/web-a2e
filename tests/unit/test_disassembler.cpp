/*
 * test_disassembler.cpp - Unit tests for 65C02 disassembler
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "disassembler.hpp"

#include <cstring>

using namespace a2e;

// ============================================================================
// disassembleInstruction tests
// ============================================================================

TEST_CASE("disassembleInstruction NOP (0xEA)", "[disasm][single]") {
    uint8_t data[] = {0xEA};
    auto instr = disassembleInstruction(data, sizeof(data), 0x1000);

    CHECK(instr.address == 0x1000);
    CHECK(instr.length == 1);
    CHECK(instr.opcode == 0xEA);
    CHECK(std::string(instr.mnemonic) == "NOP");
    CHECK(instr.mode == static_cast<uint8_t>(AddrMode::IMP));
}

TEST_CASE("disassembleInstruction LDA immediate (0xA9)", "[disasm][single]") {
    uint8_t data[] = {0xA9, 0x42};
    auto instr = disassembleInstruction(data, sizeof(data), 0x2000);

    CHECK(instr.address == 0x2000);
    CHECK(instr.length == 2);
    CHECK(instr.opcode == 0xA9);
    CHECK(instr.operand1 == 0x42);
    CHECK(std::string(instr.mnemonic) == "LDA");
    CHECK(instr.mode == static_cast<uint8_t>(AddrMode::IMM));
}

TEST_CASE("disassembleInstruction JMP absolute (0x4C)", "[disasm][single]") {
    uint8_t data[] = {0x4C, 0x00, 0xC0};
    auto instr = disassembleInstruction(data, sizeof(data), 0x0300);

    CHECK(instr.address == 0x0300);
    CHECK(instr.length == 3);
    CHECK(instr.opcode == 0x4C);
    CHECK(instr.operand1 == 0x00);
    CHECK(instr.operand2 == 0xC0);
    CHECK(instr.target == 0xC000);
    CHECK(std::string(instr.mnemonic) == "JMP");
    CHECK(instr.mode == static_cast<uint8_t>(AddrMode::ABS));
}

TEST_CASE("disassembleInstruction BRK (0x00)", "[disasm][single]") {
    uint8_t data[] = {0x00};
    auto instr = disassembleInstruction(data, sizeof(data), 0x0400);

    CHECK(instr.address == 0x0400);
    CHECK(instr.length == 1);
    CHECK(instr.opcode == 0x00);
    CHECK(std::string(instr.mnemonic) == "BRK");
}

TEST_CASE("disassembleInstruction branch instruction calculates target", "[disasm][single]") {
    // BNE with forward branch (+5)
    uint8_t data[] = {0xD0, 0x05};
    auto instr = disassembleInstruction(data, sizeof(data), 0x1000);

    CHECK(instr.length == 2);
    CHECK(std::string(instr.mnemonic) == "BNE");
    // Target = address + 2 + offset = 0x1000 + 2 + 5 = 0x1007
    CHECK(instr.target == 0x1007);
}

TEST_CASE("disassembleInstruction branch with negative offset", "[disasm][single]") {
    // BEQ with backward branch (-10 = 0xF6)
    uint8_t data[] = {0xF0, 0xF6};
    auto instr = disassembleInstruction(data, sizeof(data), 0x1000);

    CHECK(instr.length == 2);
    CHECK(std::string(instr.mnemonic) == "BEQ");
    // Target = 0x1000 + 2 + (-10) = 0x0FF8
    CHECK(instr.target == 0x0FF8);
}

TEST_CASE("disassembleInstruction null data returns zero-length", "[disasm][single]") {
    auto instr = disassembleInstruction(nullptr, 0, 0x0000);
    CHECK(instr.length == 0);
}

// ============================================================================
// disassembleBlock tests
// ============================================================================

TEST_CASE("disassembleBlock multiple instructions", "[disasm][block]") {
    // NOP; LDA #$42; RTS
    uint8_t data[] = {0xEA, 0xA9, 0x42, 0x60};
    auto result = disassembleBlock(data, sizeof(data), 0x0800);

    REQUIRE(result.instructions.size() == 3);

    CHECK(std::string(result.instructions[0].mnemonic) == "NOP");
    CHECK(result.instructions[0].address == 0x0800);
    CHECK(result.instructions[0].length == 1);

    CHECK(std::string(result.instructions[1].mnemonic) == "LDA");
    CHECK(result.instructions[1].address == 0x0801);
    CHECK(result.instructions[1].length == 2);

    CHECK(std::string(result.instructions[2].mnemonic) == "RTS");
    CHECK(result.instructions[2].address == 0x0803);
    CHECK(result.instructions[2].length == 1);
}

TEST_CASE("disassembleBlock empty data returns empty result", "[disasm][block]") {
    auto result = disassembleBlock(nullptr, 0, 0x0000);
    CHECK(result.instructions.empty());
}

// ============================================================================
// disassembleWithFlowAnalysis tests
// ============================================================================

TEST_CASE("disassembleWithFlowAnalysis traces code paths", "[disasm][flow]") {
    // Simple subroutine:
    // 0x1000: LDA #$01  (A9 01)
    // 0x1002: BNE $1005 (D0 01)
    // 0x1004: NOP       (EA)
    // 0x1005: RTS       (60)
    uint8_t data[] = {0xA9, 0x01, 0xD0, 0x01, 0xEA, 0x60};
    std::vector<uint16_t> entries = {0x1000};

    auto result = disassembleWithFlowAnalysis(data, sizeof(data), 0x1000, entries);

    // Flow analysis should find all reachable instructions
    REQUIRE(result.instructions.size() >= 3);

    // Check that we traced both the branch-taken and fall-through paths
    bool foundLDA = false, foundRTS = false, foundBNE = false;
    for (const auto& instr : result.instructions) {
        if (std::string(instr.mnemonic) == "LDA") foundLDA = true;
        if (std::string(instr.mnemonic) == "BNE") foundBNE = true;
        if (std::string(instr.mnemonic) == "RTS") foundRTS = true;
    }
    CHECK(foundLDA);
    CHECK(foundBNE);
    CHECK(foundRTS);
}

TEST_CASE("disassembleWithFlowAnalysis convenience overload", "[disasm][flow]") {
    uint8_t data[] = {0xEA, 0x60}; // NOP; RTS
    auto result = disassembleWithFlowAnalysis(data, sizeof(data), 0x0800);

    REQUIRE(result.instructions.size() == 2);
    CHECK(std::string(result.instructions[0].mnemonic) == "NOP");
    CHECK(std::string(result.instructions[1].mnemonic) == "RTS");
}

// ============================================================================
// getInstructionLength tests
// ============================================================================

TEST_CASE("getInstructionLength implied is 1", "[disasm][length]") {
    // NOP (0xEA) is implied
    CHECK(getInstructionLength(0xEA) == 1);
    // RTS (0x60) is implied
    CHECK(getInstructionLength(0x60) == 1);
    // BRK (0x00) is implied
    CHECK(getInstructionLength(0x00) == 1);
    // INX (0xE8) is implied
    CHECK(getInstructionLength(0xE8) == 1);
}

TEST_CASE("getInstructionLength immediate is 2", "[disasm][length]") {
    // LDA # (0xA9)
    CHECK(getInstructionLength(0xA9) == 2);
    // LDX # (0xA2)
    CHECK(getInstructionLength(0xA2) == 2);
    // CMP # (0xC9)
    CHECK(getInstructionLength(0xC9) == 2);
}

TEST_CASE("getInstructionLength absolute is 3", "[disasm][length]") {
    // JMP abs (0x4C)
    CHECK(getInstructionLength(0x4C) == 3);
    // JSR abs (0x20)
    CHECK(getInstructionLength(0x20) == 3);
    // LDA abs (0xAD)
    CHECK(getInstructionLength(0xAD) == 3);
    // STA abs (0x8D)
    CHECK(getInstructionLength(0x8D) == 3);
}

TEST_CASE("getInstructionLength relative (branches) is 2", "[disasm][length]") {
    // BNE (0xD0)
    CHECK(getInstructionLength(0xD0) == 2);
    // BEQ (0xF0)
    CHECK(getInstructionLength(0xF0) == 2);
    // BCC (0x90)
    CHECK(getInstructionLength(0x90) == 2);
}

TEST_CASE("getInstructionLength zero page is 2", "[disasm][length]") {
    // LDA zp (0xA5)
    CHECK(getInstructionLength(0xA5) == 2);
    // STA zp (0x85)
    CHECK(getInstructionLength(0x85) == 2);
}

// ============================================================================
// getMnemonic tests
// ============================================================================

TEST_CASE("getMnemonic returns correct mnemonic strings", "[disasm][mnemonic]") {
    CHECK(std::string(getMnemonic(0xEA)) == "NOP");
    CHECK(std::string(getMnemonic(0xA9)) == "LDA");
    CHECK(std::string(getMnemonic(0x4C)) == "JMP");
    CHECK(std::string(getMnemonic(0x20)) == "JSR");
    CHECK(std::string(getMnemonic(0x60)) == "RTS");
    CHECK(std::string(getMnemonic(0x00)) == "BRK");
    CHECK(std::string(getMnemonic(0xE8)) == "INX");
    CHECK(std::string(getMnemonic(0xC8)) == "INY");
    CHECK(std::string(getMnemonic(0x48)) == "PHA");
    CHECK(std::string(getMnemonic(0x68)) == "PLA");
}

// ============================================================================
// getAddressingMode tests
// ============================================================================

TEST_CASE("getAddressingMode returns correct modes", "[disasm][addrmode]") {
    CHECK(getAddressingMode(0xEA) == AddrMode::IMP);   // NOP - implied
    CHECK(getAddressingMode(0xA9) == AddrMode::IMM);   // LDA # - immediate
    CHECK(getAddressingMode(0xA5) == AddrMode::ZP);    // LDA zp - zero page
    CHECK(getAddressingMode(0xAD) == AddrMode::ABS);   // LDA abs - absolute
    CHECK(getAddressingMode(0xB5) == AddrMode::ZPX);   // LDA zp,X
    CHECK(getAddressingMode(0xBD) == AddrMode::ABX);   // LDA abs,X
    CHECK(getAddressingMode(0xB9) == AddrMode::ABY);   // LDA abs,Y
    CHECK(getAddressingMode(0xA1) == AddrMode::IZX);   // LDA (zp,X)
    CHECK(getAddressingMode(0xB1) == AddrMode::IZY);   // LDA (zp),Y
    CHECK(getAddressingMode(0x6C) == AddrMode::IND);   // JMP (abs)
    CHECK(getAddressingMode(0xD0) == AddrMode::REL);   // BNE rel
    CHECK(getAddressingMode(0x0A) == AddrMode::ACC);   // ASL A
}

// ============================================================================
// getInstructionCategory tests
// ============================================================================

TEST_CASE("getInstructionCategory returns correct categories", "[disasm][category]") {
    // Branch instructions
    CHECK(getInstructionCategory(0x4C) == InstrCategory::BRANCH);   // JMP
    CHECK(getInstructionCategory(0x20) == InstrCategory::BRANCH);   // JSR
    CHECK(getInstructionCategory(0xD0) == InstrCategory::BRANCH);   // BNE
    CHECK(getInstructionCategory(0x60) == InstrCategory::BRANCH);   // RTS

    // Load/store instructions
    CHECK(getInstructionCategory(0xA9) == InstrCategory::LOAD);     // LDA #
    CHECK(getInstructionCategory(0x85) == InstrCategory::LOAD);     // STA zp
    CHECK(getInstructionCategory(0xA2) == InstrCategory::LOAD);     // LDX #

    // Math/logic instructions
    CHECK(getInstructionCategory(0x69) == InstrCategory::MATH);     // ADC
    CHECK(getInstructionCategory(0x29) == InstrCategory::MATH);     // AND #
    CHECK(getInstructionCategory(0xC9) == InstrCategory::MATH);     // CMP #

    // Stack instructions
    CHECK(getInstructionCategory(0x48) == InstrCategory::STACK);    // PHA
    CHECK(getInstructionCategory(0x68) == InstrCategory::STACK);    // PLA
    CHECK(getInstructionCategory(0x00) == InstrCategory::STACK);    // BRK

    // Flag instructions
    CHECK(getInstructionCategory(0x18) == InstrCategory::FLAG);     // CLC
    CHECK(getInstructionCategory(0x38) == InstrCategory::FLAG);     // SEC
    CHECK(getInstructionCategory(0x78) == InstrCategory::FLAG);     // SEI
}

// ============================================================================
// getFlowType tests
// ============================================================================

TEST_CASE("getFlowType SEQUENTIAL for normal instructions", "[disasm][flow]") {
    CHECK(getFlowType(0xEA) == FlowType::SEQUENTIAL);   // NOP
    CHECK(getFlowType(0xA9) == FlowType::SEQUENTIAL);   // LDA #
    CHECK(getFlowType(0x85) == FlowType::SEQUENTIAL);   // STA zp
    CHECK(getFlowType(0xE8) == FlowType::SEQUENTIAL);   // INX
    CHECK(getFlowType(0x48) == FlowType::SEQUENTIAL);   // PHA
}

TEST_CASE("getFlowType CONDITIONAL for branches", "[disasm][flow]") {
    CHECK(getFlowType(0xD0) == FlowType::CONDITIONAL);  // BNE
    CHECK(getFlowType(0xF0) == FlowType::CONDITIONAL);  // BEQ
    CHECK(getFlowType(0x90) == FlowType::CONDITIONAL);  // BCC
    CHECK(getFlowType(0xB0) == FlowType::CONDITIONAL);  // BCS
    CHECK(getFlowType(0x10) == FlowType::CONDITIONAL);  // BPL
    CHECK(getFlowType(0x30) == FlowType::CONDITIONAL);  // BMI
}

TEST_CASE("getFlowType UNCONDITIONAL for JMP absolute and BRA", "[disasm][flow]") {
    CHECK(getFlowType(0x4C) == FlowType::UNCONDITIONAL); // JMP abs
    CHECK(getFlowType(0x80) == FlowType::UNCONDITIONAL); // BRA (65C02)
}

TEST_CASE("getFlowType CALL for JSR", "[disasm][flow]") {
    CHECK(getFlowType(0x20) == FlowType::CALL);          // JSR
}

TEST_CASE("getFlowType RETURN for RTS and RTI", "[disasm][flow]") {
    CHECK(getFlowType(0x60) == FlowType::RETURN);        // RTS
    CHECK(getFlowType(0x40) == FlowType::RETURN);        // RTI
}

TEST_CASE("getFlowType HALT for BRK, STP, WAI", "[disasm][flow]") {
    CHECK(getFlowType(0x00) == FlowType::HALT);          // BRK
    CHECK(getFlowType(0xDB) == FlowType::HALT);          // STP
    CHECK(getFlowType(0xCB) == FlowType::HALT);          // WAI
}

TEST_CASE("getFlowType INDIRECT for JMP indirect", "[disasm][flow]") {
    CHECK(getFlowType(0x6C) == FlowType::INDIRECT);      // JMP (abs)
    CHECK(getFlowType(0x7C) == FlowType::INDIRECT);      // JMP (abs,X) - 65C02
}

// ============================================================================
// alignedDisassemblyStart: where a listing centred on the PC begins
// ============================================================================

#include "disasm_align.hpp"

#include <vector>

namespace {

// Walk forward from `start` and return the boundaries reached up to and
// including `centre`, or an empty list if the walk overshoots.
template <typename LengthAt>
std::vector<uint16_t> walkTo(uint16_t start, uint16_t centre, LengthAt lengthAt) {
    std::vector<uint16_t> out;
    int at = start;
    while (at < centre) {
        out.push_back(static_cast<uint16_t>(at));
        at += lengthAt(static_cast<uint16_t>(at));
    }
    if (at != centre) return {};
    out.push_back(centre);
    return out;
}

// Instruction length as the 65C02 disassembler decodes the byte at `at`.
struct MemoryLengths {
    const std::vector<uint8_t>& mem;
    int operator()(uint16_t at) const {
        return getInstructionLength(mem[at]);
    }
};

} // namespace

TEST_CASE("alignedDisassemblyStart lists the PC after an empty slot (issue #76)",
          "[disasm][align]") {
    // Slot 5 empty: $C500-$C5FF read the floating bus, $A0 on every byte.
    // Slot 6's boot ROM begins LDX #$20 at $C600.
    std::vector<uint8_t> mem(0x10000, 0xA0);
    const uint8_t boot[] = {0xA2, 0x20, 0xA0, 0x00, 0xA2, 0x03, 0x86, 0x3C, 0x8A, 0x0A};
    std::copy(std::begin(boot), std::end(boot), mem.begin() + 0xC600);
    MemoryLengths lengths{mem};

    const uint16_t start = alignedDisassemblyStart(0xC600, 6, 3, lengths);
    const auto boundaries = walkTo(start, 0xC600, lengths);

    REQUIRE_FALSE(boundaries.empty());
    CHECK(boundaries.back() == 0xC600);
    // Six LDY #$A0 above it, starting on an even byte so the pairs line up.
    CHECK(boundaries.size() == 7);
    CHECK(start == 0xC5F4);
}

TEST_CASE("alignedDisassemblyStart keeps real code as context",
          "[disasm][align]") {
    std::vector<uint8_t> mem(0x10000, 0x00);
    // $1000: LDA #$01 / STA $0300 / INX / JMP $1000 / <centre> NOP
    const uint8_t code[] = {0xA9, 0x01, 0x8D, 0x00, 0x03, 0xE8, 0x4C, 0x00, 0x10, 0xEA};
    std::copy(std::begin(code), std::end(code), mem.begin() + 0x1000);
    MemoryLengths lengths{mem};

    const uint16_t start = alignedDisassemblyStart(0x1009, 4, 3, lengths);
    const auto boundaries = walkTo(start, 0x1009, lengths);

    REQUIRE(boundaries.size() == 5);
    CHECK(boundaries[0] == 0x1000);
    CHECK(boundaries[1] == 0x1002);
    CHECK(boundaries[2] == 0x1005);
    CHECK(boundaries[3] == 0x1006);
    CHECK(boundaries[4] == 0x1009);
}

TEST_CASE("alignedDisassemblyStart falls back to the centre when nothing aligns",
          "[disasm][align]") {
    // Every byte above the centre decodes as an instruction that runs one
    // byte past it, so no walk can land on the centre; the listing then
    // starts there with no context rather than somewhere that hides it.
    const uint16_t centre = 0x2001;
    auto overshoot = [](uint16_t at) {
        return at < 0x2001 ? (0x2001 - at) + 1 : 1;
    };
    CHECK(alignedDisassemblyStart(centre, 6, 3, overshoot) == centre);
}

TEST_CASE("alignedDisassemblyStart with no context asked for is the centre",
          "[disasm][align]") {
    auto one = [](uint16_t) { return 1; };
    CHECK(alignedDisassemblyStart(0x0000, 0, 3, one) == 0x0000);
    CHECK(alignedDisassemblyStart(0x0003, 6, 3, one) == 0x0000);
}
