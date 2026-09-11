/*
 * test_cpu65816.cpp - Unit tests for the 65C816 core
 *
 * The core is tested on its own, against a flat 16MB of memory and nothing
 * else — no machine, no MMU, no IIgs. What is pinned here is the part that
 * makes a 65816 not a 6502 with more opcodes: two modes, registers whose width
 * changes at runtime, a direct page and a stack that can be anywhere in bank
 * zero, and separate banks for code and data.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "cpu65816.hpp"

#include <memory>
#include <vector>

using namespace a2e;

namespace {

// 16MB, which is the whole of what a 65816 can address. Allocated rather than
// declared so a test binary does not carry it in its image.
class FlatBus {
public:
    FlatBus() : memory_(16 * 1024 * 1024, 0) {}

    uint8_t read(uint32_t address) const { return memory_[address & 0xFFFFFF]; }
    void write(uint32_t address, uint8_t value) {
        memory_[address & 0xFFFFFF] = value;
    }

    uint8_t &operator[](uint32_t address) { return memory_[address & 0xFFFFFF]; }

    void load(uint32_t address, const std::vector<uint8_t> &bytes) {
        for (size_t i = 0; i < bytes.size(); i++) {
            memory_[(address + i) & 0xFFFFFF] = bytes[i];
        }
    }

    void setWord(uint32_t address, uint16_t value) {
        memory_[address & 0xFFFFFF] = static_cast<uint8_t>(value);
        memory_[(address + 1) & 0xFFFFFF] = static_cast<uint8_t>(value >> 8);
    }

private:
    std::vector<uint8_t> memory_;
};

struct Fixture {
    FlatBus bus;
    std::unique_ptr<CPU65816> cpu;

    Fixture() {
        cpu = std::make_unique<CPU65816>(
            [this](uint32_t address) { return bus.read(address); },
            [this](uint32_t address, uint8_t value) { bus.write(address, value); });
    }

    // Put a program at $00:0400 and start there.
    void run(const std::vector<uint8_t> &program, int instructions) {
        bus.load(0x000400, program);
        bus.setWord(0x00FFFC, 0x0400);
        cpu->reset();
        for (int i = 0; i < instructions; i++) cpu->executeInstruction();
    }

    // Leave emulation mode first: CLC, XCE.
    void runNative(const std::vector<uint8_t> &program, int instructions) {
        std::vector<uint8_t> full{0x18, 0xFB}; // CLC, XCE
        full.insert(full.end(), program.begin(), program.end());
        run(full, instructions + 2);
    }
};

} // namespace

TEST_CASE("A 65816 comes up as a 6502", "[cpu816]") {
    // Emulation mode, both registers eight bits, the stack in page one, and
    // the reset vector read from bank zero. Every Apple IIgs starts here and
    // most software never leaves.
    Fixture f;
    f.bus.setWord(0x00FFFC, 0x8000);
    f.cpu->reset();

    REQUIRE(f.cpu->getEmulation());
    REQUIRE(f.cpu->accumulator8());
    REQUIRE(f.cpu->index8());
    REQUIRE(f.cpu->getPC() == 0x8000);
    REQUIRE(f.cpu->getPBR() == 0x00);
    REQUIRE(f.cpu->getDBR() == 0x00);
    REQUIRE(f.cpu->getD() == 0x0000);
    REQUIRE((f.cpu->getSP() & 0xFF00) == 0x0100);
}

TEST_CASE("XCE is the whole of the mode switch", "[cpu816]") {
    Fixture f;
    f.run({0x18, 0xFB}, 2); // CLC, XCE

    REQUIRE_FALSE(f.cpu->getEmulation());
    REQUIRE(f.cpu->accumulator8()); // M and X are still set: native, but narrow
    REQUIRE(f.cpu->index8());

    SECTION("and it puts the old mode in the carry, so it can be put back") {
        f.cpu->executeInstruction(); // whatever follows
        Fixture back;
        back.run({0x18, 0xFB, 0x38, 0xFB}, 4); // CLC XCE SEC XCE
        REQUIRE(back.cpu->getEmulation());
    }
}

TEST_CASE("REP and SEP change how wide the registers are", "[cpu816]") {
    Fixture f;
    // Native, then 16-bit accumulator and index, then load both wide.
    f.runNative({0xC2, 0x30,             // REP #$30
                 0xA9, 0x34, 0x12,       // LDA #$1234
                 0xA2, 0x78, 0x56},      // LDX #$5678
                3);

    REQUIRE_FALSE(f.cpu->accumulator8());
    REQUIRE_FALSE(f.cpu->index8());
    REQUIRE(f.cpu->getA() == 0x1234);
    REQUIRE(f.cpu->getX() == 0x5678);

    SECTION("and narrowing the index registers throws the top half away") {
        // Documented, and relied on: SEP #$10 is how a program says "X is
        // under 256 now" and means it.
        f.cpu->executeInstruction(); // (nothing loaded; harmless NOP of zeroes)
        Fixture narrow;
        narrow.runNative({0xC2, 0x30, 0xA2, 0x78, 0x56, 0xE2, 0x10}, 3);
        REQUIRE(narrow.cpu->index8());
        REQUIRE(narrow.cpu->getX() == 0x0078);
    }

    SECTION("but the accumulator's top half survives, because it is B") {
        // SEP #$20 hides the high byte rather than clearing it, which is what
        // makes XBA worth having.
        Fixture hidden;
        hidden.runNative({0xC2, 0x30, 0xA9, 0x34, 0x12, 0xE2, 0x20, 0xEB}, 4);
        REQUIRE(hidden.cpu->accumulator8());
        REQUIRE((hidden.cpu->getA() & 0xFF) == 0x12); // XBA brought B down
    }
}

TEST_CASE("Emulation mode refuses to be sixteen bits", "[cpu816]") {
    // REP #$30 in emulation mode is a program asking for a machine that does
    // not exist. The flags read back set and the registers stay narrow.
    Fixture f;
    f.run({0xC2, 0x30, 0xA9, 0x34}, 2); // REP #$30, LDA #$34

    REQUIRE(f.cpu->getEmulation());
    REQUIRE(f.cpu->accumulator8());
    REQUIRE(f.cpu->index8());
    REQUIRE((f.cpu->getP() & (FLAG816_M | FLAG816_X)) ==
            (FLAG816_M | FLAG816_X));
}

TEST_CASE("The accumulator is sixteen bits wide even when it is eight",
          "[cpu816]") {
    // B is not a separate register you can address; it is the half of the
    // accumulator that is not currently in use, and it survives everything.
    Fixture f;
    f.runNative({0xC2, 0x20,       // REP #$20 — 16-bit A
                 0xA9, 0xCD, 0xAB, // LDA #$ABCD
                 0xE2, 0x20,       // SEP #$20 — 8-bit A
                 0xA9, 0x11,       // LDA #$11 — touches only the low half
                 0xEB},            // XBA
                5);
    REQUIRE((f.cpu->getA() & 0x00FF) == 0xAB); // what was hidden
    REQUIRE((f.cpu->getA() >> 8) == 0x11);     // what was visible
}

TEST_CASE("The direct page can be anywhere in bank zero", "[cpu816]") {
    // A 6502's zero page is page zero. A 65816's is wherever D points, which is
    // how a program gets sixteen-bit-indexed locals without touching page zero
    // at all.
    Fixture f;
    f.bus[0x001234] = 0x42;

    f.runNative({0xC2, 0x20,             // REP #$20
                 0xA9, 0x00, 0x12,       // LDA #$1200
                 0x5B,                   // TCD — direct page at $1200
                 0xE2, 0x20,             // SEP #$20
                 0xA5, 0x34},            // LDA $34 -> $1234
                5);
    REQUIRE(f.cpu->getD() == 0x1200);
    REQUIRE((f.cpu->getA() & 0xFF) == 0x42);
}

TEST_CASE("Code and data live in different banks", "[cpu816]") {
    // The data bank register is what makes a 16-bit address mean something in
    // a 24-bit machine, and it is separate from the bank the code is in.
    Fixture f;
    f.bus[0x123456] = 0x99;

    f.runNative({0xE2, 0x20,       // SEP #$20 — 8-bit A
                 0xA9, 0x12,       // LDA #$12
                 0x48,             // PHA
                 0xAB,             // PLB — data bank = $12
                 0xAD, 0x56, 0x34}, // LDA $3456 -> $12:3456
                5);
    REQUIRE(f.cpu->getDBR() == 0x12);
    REQUIRE((f.cpu->getA() & 0xFF) == 0x99);
    REQUIRE(f.cpu->getPBR() == 0x00); // and the code never left bank zero

    SECTION("and a long address ignores the data bank entirely") {
        Fixture longAddr;
        longAddr.bus[0xAB1234] = 0x77;
        longAddr.runNative({0xE2, 0x20,                   // SEP #$20
                            0xAF, 0x34, 0x12, 0xAB},      // LDA $AB1234
                           2);
        REQUIRE((longAddr.cpu->getA() & 0xFF) == 0x77);
    }
}

TEST_CASE("JSL and RTL carry the bank with them", "[cpu816]") {
    // A 6502's JSR pushes two bytes and can only reach its own 64KB. JSL
    // pushes three and is how a IIgs program calls across a megabyte.
    Fixture f;
    // At $02:8000: LDA #$55 (8-bit), RTL
    f.bus.load(0x028000, {0xA9, 0x55, 0x6B});

    f.runNative({0xE2, 0x20,                   // SEP #$20
                 0x22, 0x00, 0x80, 0x02,       // JSL $028000
                 0xA2, 0x01},                  // LDX #$01 — proof we came back
                5);
    REQUIRE((f.cpu->getA() & 0xFF) == 0x55);
    REQUIRE(f.cpu->getPBR() == 0x00);
    REQUIRE((f.cpu->getX() & 0xFF) == 0x01);
}

TEST_CASE("The stack is a full sixteen bits in native mode", "[cpu816]") {
    // In emulation it is eight bits inside page one and wraps there, which any
    // //e code running on this machine depends on. In native mode it is a
    // pointer into the whole of bank zero.
    Fixture emulation;
    emulation.run({0xA2, 0x00, // LDX #$00
                   0x9A,       // TXS
                   0x48,       // PHA
                   0x48},      // PHA — past the bottom of the page
                  4);
    REQUIRE((emulation.cpu->getSP() & 0xFF00) == 0x0100); // still in page one

    Fixture native;
    native.runNative({0xC2, 0x30,             // REP #$30
                      0xA2, 0x00, 0x20,       // LDX #$2000
                      0x9A,                   // TXS
                      0xA9, 0x34, 0x12,       // LDA #$1234
                      0x48},                  // PHA
                     5);
    REQUIRE(native.cpu->getSP() == 0x1FFE);        // two bytes pushed
    REQUIRE(native.bus[0x002000] == 0x12);         // high byte first
    REQUIRE(native.bus[0x001FFF] == 0x34);
}

TEST_CASE("Sixteen-bit arithmetic carries where sixteen bits carry",
          "[cpu816]") {
    Fixture f;
    f.runNative({0xC2, 0x20,             // REP #$20
                 0xA9, 0xFF, 0xFF,       // LDA #$FFFF
                 0x18,                   // CLC
                 0x69, 0x01, 0x00},      // ADC #$0001
                4);
    REQUIRE(f.cpu->getA() == 0x0000);
    REQUIRE((f.cpu->getP() & FLAG816_C) != 0);
    REQUIRE((f.cpu->getP() & FLAG816_Z) != 0);

    SECTION("and an eight-bit add still carries at 255") {
        Fixture eight;
        eight.runNative({0xE2, 0x20, 0xA9, 0xFF, 0x18, 0x69, 0x01}, 4);
        REQUIRE((eight.cpu->getA() & 0xFF) == 0x00);
        REQUIRE((eight.cpu->getP() & FLAG816_C) != 0);
    }

    SECTION("overflow is about signs, at whichever width") {
        Fixture overflow;
        overflow.runNative({0xC2, 0x20, 0xA9, 0xFF, 0x7F, 0x18, 0x69, 0x01, 0x00},
                           4);
        REQUIRE(overflow.cpu->getA() == 0x8000);
        REQUIRE((overflow.cpu->getP() & FLAG816_V) != 0);
        REQUIRE((overflow.cpu->getP() & FLAG816_N) != 0);
    }
}

TEST_CASE("Block moves move a byte at a time and rewind", "[cpu816]") {
    // MVN is a whole loop in one instruction: it moves a byte, counts down,
    // and backs PC up over itself until the count runs out — so an interrupt
    // can be taken in the middle of a 64KB move rather than after it.
    Fixture f;
    f.bus.load(0x001000, {0x11, 0x22, 0x33, 0x44});

    f.runNative({0xC2, 0x30,                   // REP #$30
                 0xA9, 0x03, 0x00,             // LDA #$0003 — count is n-1
                 0xA2, 0x00, 0x10,             // LDX #$1000 — source
                 0xA0, 0x00, 0x20,             // LDY #$2000 — destination
                 0x54, 0x00, 0x00},            // MVN $00,$00
                4 + 4); // the move runs four times

    REQUIRE(f.bus[0x002000] == 0x11);
    REQUIRE(f.bus[0x002001] == 0x22);
    REQUIRE(f.bus[0x002002] == 0x33);
    REQUIRE(f.bus[0x002003] == 0x44);
    REQUIRE(f.cpu->getA() == 0xFFFF); // the count ran out
    REQUIRE(f.cpu->getDBR() == 0x00); // and the move left DBR where it wrote
}

TEST_CASE("An interrupt in native mode remembers which bank it came from",
          "[cpu816]") {
    Fixture f;
    f.bus.setWord(0x00FFEE, 0x9000); // native IRQ vector
    f.bus.load(0x009000, {0x40});    // RTI

    // Native, interrupts enabled, running in bank zero.
    f.runNative({0x58, 0xEA, 0xEA}, 1); // CLI, then NOPs
    const uint16_t pcBefore = f.cpu->getPC();
    f.cpu->irq();
    f.cpu->executeInstruction();

    REQUIRE(f.cpu->getPC() == 0x9000);
    REQUIRE(f.cpu->getPBR() == 0x00);
    REQUIRE((f.cpu->getP() & FLAG816_I) != 0); // and masked while inside

    f.cpu->executeInstruction(); // RTI
    REQUIRE(f.cpu->getPC() == pcBefore);
    REQUIRE_FALSE(f.cpu->getEmulation());

    SECTION("and an emulation-mode interrupt uses the 6502's vector") {
        Fixture emulation;
        emulation.bus.setWord(0x00FFFE, 0x9100);
        emulation.bus.load(0x009100, {0x40});
        emulation.run({0x58, 0xEA}, 1); // CLI
        emulation.cpu->irq();
        emulation.cpu->executeInstruction();
        REQUIRE(emulation.cpu->getPC() == 0x9100);
    }
}

TEST_CASE("WAI waits and STP stops", "[cpu816]") {
    Fixture f;
    f.run({0xCB, 0xA9, 0x42}, 1); // WAI, then LDA #$42

    REQUIRE(f.cpu->isWaiting());
    for (int i = 0; i < 10; i++) f.cpu->executeInstruction();
    REQUIRE((f.cpu->getA() & 0xFF) != 0x42); // still waiting, nothing ran

    f.cpu->irq();
    f.cpu->executeInstruction(); // wakes
    f.cpu->executeInstruction(); // and runs the LDA
    REQUIRE_FALSE(f.cpu->isWaiting());

    SECTION("STP needs a reset, and nothing else will do") {
        Fixture stopped;
        stopped.run({0xDB, 0xA9, 0x42}, 1);
        REQUIRE(stopped.cpu->isStopped());
        for (int i = 0; i < 10; i++) stopped.cpu->executeInstruction();
        REQUIRE((stopped.cpu->getA() & 0xFF) != 0x42);

        stopped.cpu->reset();
        REQUIRE_FALSE(stopped.cpu->isStopped());
    }
}

TEST_CASE("Emulation mode is a 6502, in the details that catch people out",
          "[cpu816]") {
    SECTION("direct page indexed wraps inside the page, as zero page,X did") {
        Fixture f;
        f.bus[0x000010] = 0x5A;
        // LDX #$20, LDA $F0,X — $F0 + $20 = $110, which wraps to $10.
        f.run({0xA2, 0x20, 0xB5, 0xF0}, 2);
        REQUIRE((f.cpu->getA() & 0xFF) == 0x5A);
    }

    SECTION("but native mode carries out of the page") {
        Fixture f;
        f.bus[0x000110] = 0x6B;
        f.runNative({0xA2, 0x20, 0xB5, 0xF0}, 2);
        REQUIRE((f.cpu->getA() & 0xFF) == 0x6B);
    }
}

TEST_CASE("Cycle counts follow the width and the direct page", "[cpu816]") {
    // Not the cycle *pattern* — which cycle touches which address — but the
    // count, which is what a machine whose video reads a different bus needs.
    Fixture f;
    f.run({0xEA}, 1); // NOP
    REQUIRE(f.cpu->getCycleCount() == 2);

    SECTION("a sixteen-bit load costs a cycle more than an eight-bit one") {
        Fixture eight;
        eight.runNative({0xE2, 0x20, 0xA5, 0x10}, 2); // SEP #$20, LDA $10
        const int narrow = eight.cpu->getCycleCount();

        Fixture sixteen;
        sixteen.runNative({0xC2, 0x20, 0xA5, 0x10}, 2); // REP #$20, LDA $10
        REQUIRE(sixteen.cpu->getCycleCount() == narrow + 1);
    }

    SECTION("and a direct page that is not page-aligned costs one too") {
        Fixture aligned;
        aligned.runNative({0xC2, 0x20, 0xA9, 0x00, 0x12, 0x5B, 0xE2, 0x20,
                           0xA5, 0x10},
                          5);
        const int cheap = aligned.cpu->getCycleCount();

        Fixture misaligned;
        misaligned.runNative({0xC2, 0x20, 0xA9, 0x34, 0x12, 0x5B, 0xE2, 0x20,
                              0xA5, 0x10},
                             5);
        REQUIRE(misaligned.cpu->getCycleCount() == cheap + 1);
    }
}
