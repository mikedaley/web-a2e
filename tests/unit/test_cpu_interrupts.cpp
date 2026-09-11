/*
 * test_cpu_interrupts.cpp - 65C02 interrupt handling tests
 *
 * Tests BRK, IRQ, NMI behavior, RTI, and level-triggered IRQ
 * via the IRQ status callback mechanism.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "test_helpers.hpp"

// ============================================================================
// BRK Instruction
// ============================================================================

TEST_CASE("BRK instruction behavior", "[cpu][interrupt]") {

    SECTION("BRK pushes PC+2 onto stack") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x1000);
        // NOP at handler to prevent undefined behavior
        f.mem.loadProgram(0x1000, {0xEA});
        f.loadAndReset(0x0400, {0x00, 0xEA}); // BRK, padding
        uint8_t spBefore = f.cpu->getSP();
        f.cpu->executeInstruction();

        // BRK pushes PC+2 (address after BRK + signature byte)
        uint8_t retHi = f.mem[0x0100 + spBefore];
        uint8_t retLo = f.mem[0x0100 + spBefore - 1];
        uint16_t retAddr = (retHi << 8) | retLo;
        REQUIRE(retAddr == 0x0402);
    }

    SECTION("BRK pushes P with B flag set") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x1000);
        f.mem.loadProgram(0x1000, {0xEA});
        f.loadAndReset(0x0400, {0x00, 0xEA});
        uint8_t spBefore = f.cpu->getSP();
        f.cpu->executeInstruction();

        // P is pushed after the two PC bytes, at SP+3 from final SP
        uint8_t pushedP = f.mem[0x0100 + spBefore - 2];
        REQUIRE((pushedP & a2e::FLAG_B) != 0); // B flag set in pushed P
        REQUIRE((pushedP & a2e::FLAG_U) != 0); // U flag always set
    }

    SECTION("BRK jumps to IRQ vector") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x1000);
        f.mem.loadProgram(0x1000, {0xEA});
        f.loadAndReset(0x0400, {0x00, 0xEA});
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getPC() == 0x1000);
    }

    SECTION("BRK sets I flag") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x1000);
        f.mem.loadProgram(0x1000, {0xEA});
        f.loadAndReset(0x0400, {0x00, 0xEA});
        f.cpu->setFlag(a2e::FLAG_I, false);
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getFlag(a2e::FLAG_I) == true);
    }

    SECTION("BRK clears D flag on 65C02") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x1000);
        f.mem.loadProgram(0x1000, {0xEA});
        // SED; BRK
        f.loadAndReset(0x0400, {0xF8, 0x00, 0xEA});
        test::runInstructions(*f.cpu, 2);
        // On 65C02, BRK clears D flag
        REQUIRE(f.cpu->getFlag(a2e::FLAG_D) == false);
    }
}

// ============================================================================
// IRQ - Maskable Interrupt
// ============================================================================

TEST_CASE("IRQ behavior", "[cpu][interrupt]") {

    SECTION("IRQ fires when I flag is clear") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x2000);
        f.mem.loadProgram(0x2000, {0xEA}); // NOP at handler
        // CLI; NOP; NOP
        f.loadAndReset(0x0400, {0x58, 0xEA, 0xEA});
        f.cpu->executeInstruction(); // CLI

        // Trigger IRQ
        f.cpu->irq();
        f.cpu->executeInstruction(); // This NOP should complete, then IRQ fires
        f.cpu->executeInstruction(); // Process the IRQ

        REQUIRE(f.cpu->getPC() == 0x2001); // One instruction into handler (NOP executed)
    }

    SECTION("IRQ does not fire when I flag is set") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x2000);
        f.mem.loadProgram(0x2000, {0xEA});
        // SEI; NOP; NOP
        f.loadAndReset(0x0400, {0x78, 0xEA, 0xEA});
        f.cpu->executeInstruction(); // SEI

        f.cpu->irq();
        f.cpu->executeInstruction(); // NOP at $0401
        f.cpu->executeInstruction(); // NOP at $0402

        // Should still be executing main code, not at handler
        REQUIRE(f.cpu->getPC() == 0x0403);
    }

    SECTION("IRQ pushes PC and P with B flag clear") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x2000);
        f.mem.loadProgram(0x2000, {0xEA, 0xEA});
        // CLI; NOP (will be interrupted after this)
        f.loadAndReset(0x0400, {0x58, 0xEA, 0xEA});
        f.cpu->executeInstruction(); // CLI
        f.cpu->irq();
        uint8_t spBefore = f.cpu->getSP();
        f.cpu->executeInstruction(); // NOP completes
        f.cpu->executeInstruction(); // IRQ serviced

        // P pushed during IRQ should have B flag CLEAR (unlike BRK)
        uint8_t pushedP = f.mem[0x0100 + spBefore - 2];
        REQUIRE((pushedP & a2e::FLAG_B) == 0);
    }

    SECTION("IRQ sets I flag") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x2000);
        f.mem.loadProgram(0x2000, {0xEA});
        f.loadAndReset(0x0400, {0x58, 0xEA}); // CLI; NOP
        f.cpu->executeInstruction(); // CLI
        f.cpu->irq();
        f.cpu->executeInstruction(); // NOP
        f.cpu->executeInstruction(); // IRQ serviced

        REQUIRE(f.cpu->getFlag(a2e::FLAG_I) == true);
    }
}

// ============================================================================
// NMI - Non-Maskable Interrupt
// ============================================================================

TEST_CASE("NMI behavior", "[cpu][interrupt]") {

    SECTION("NMI fires regardless of I flag") {
        test::CPUTestFixture f;
        f.mem.setNMIVector(0x3000);
        f.mem.loadProgram(0x3000, {0xEA, 0xEA});
        // SEI; NOP (NMI should still fire even with I set)
        f.loadAndReset(0x0400, {0x78, 0xEA, 0xEA});
        f.cpu->executeInstruction(); // SEI
        f.cpu->nmi();
        f.cpu->executeInstruction(); // NOP completes
        f.cpu->executeInstruction(); // NMI serviced

        // Should be at NMI handler
        REQUIRE(f.cpu->getPC() == 0x3001);
    }

    SECTION("NMI pushes PC and P") {
        test::CPUTestFixture f;
        f.mem.setNMIVector(0x3000);
        f.mem.loadProgram(0x3000, {0xEA, 0xEA});
        f.loadAndReset(0x0400, {0xEA, 0xEA}); // NOP; NOP
        uint8_t spBefore = f.cpu->getSP();
        f.cpu->nmi();
        f.cpu->executeInstruction(); // NOP completes
        f.cpu->executeInstruction(); // NMI serviced

        // SP should have moved down by 3 (PCH, PCL, P)
        REQUIRE(f.cpu->getSP() == static_cast<uint8_t>(spBefore - 3));
    }

    SECTION("NMI jumps to NMI vector") {
        test::CPUTestFixture f;
        f.mem.setNMIVector(0x3000);
        f.mem.loadProgram(0x3000, {0xEA});
        f.loadAndReset(0x0400, {0xEA}); // NOP
        f.cpu->nmi();
        f.cpu->executeInstruction(); // NOP completes
        f.cpu->executeInstruction(); // NMI serviced
        REQUIRE(f.cpu->getPC() == 0x3001); // After executing NOP at $3000
    }
}

// ============================================================================
// IRQ Status Callback (Level-Triggered IRQs)
// ============================================================================

TEST_CASE("IRQ status callback for level-triggered IRQs", "[cpu][interrupt]") {
    // The 6502's IRQ input is a level, sampled once per instruction, and not an
    // event: a device holding the line low is asking again every instruction
    // until something services it. That is why a handler which returns without
    // clearing its device is re-entered immediately on real hardware, and it is
    // what the status callback is for.

    // Handler: increment a counter and RTI. Main: CLI, then NOPs forever.
    auto fixture = [](test::CPUTestFixture &f) {
        f.mem.setIRQVector(0x2000);
        f.mem.loadProgram(0x2000,
                          {0xA5, 0x10, 0x18, 0x69, 0x01, 0x85, 0x10, 0x40});
        f.mem[0x10] = 0x00;
        // CLI, then a NOP spinning on itself. A run of NOPs would fall off the
        // end into zeroed memory, and a zero is BRK — which vectors to this
        // same handler and would be counted as an interrupt that never
        // happened.
        f.loadAndReset(0x0400, {0x58, 0xEA, 0x4C, 0x01, 0x04});
    };

    SECTION("a held line is taken again after every RTI") {
        test::CPUTestFixture f;
        fixture(f);
        bool irqActive = true;
        f.cpu->setIRQStatusCallback([&irqActive]() { return irqActive; });

        f.cpu->executeInstruction(); // CLI
        test::runInstructions(*f.cpu, 60);

        // Nothing ever called irq(). The line alone is the whole signal, and a
        // handler that leaves it asserted is asked again.
        REQUIRE(f.mem[0x10] >= 3);
    }

    SECTION("and once the device lets go, the interrupts stop") {
        test::CPUTestFixture f;
        fixture(f);
        bool irqActive = true;
        f.cpu->setIRQStatusCallback([&irqActive]() { return irqActive; });

        f.cpu->executeInstruction(); // CLI
        test::runInstructions(*f.cpu, 40);
        const uint8_t serviced = f.mem[0x10];
        REQUIRE(serviced >= 1);

        // Letting go stops it. Not instantly — an interrupt the CPU has
        // already taken has to run, and a latched pulse is still owed a
        // handler — so what is asserted is that it settles and stays settled,
        // rather than a count that would only be pinning where in the handler
        // the release happened to land.
        irqActive = false;
        test::runInstructions(*f.cpu, 40);
        const uint8_t settled = f.mem[0x10];

        test::runInstructions(*f.cpu, 400);
        REQUIRE(f.mem[0x10] == settled);
        REQUIRE(settled < serviced + 40); // and it stopped promptly, not eventually
    }

    SECTION("a device that only pulses the line is still heard") {
        // Not everything exposes a level. An edge latches until the CPU can
        // take it, which is what makes a device that interrupts while I is set
        // — inside somebody else's handler — arrive rather than vanish.
        test::CPUTestFixture f;
        fixture(f);
        f.cpu->setIRQStatusCallback([]() { return false; });

        f.cpu->executeInstruction(); // CLI
        f.cpu->irq();
        test::runInstructions(*f.cpu, 20);

        REQUIRE(f.mem[0x10] == 1); // once, and only once
    }
}

// ============================================================================
// RTI - Return from Interrupt
// ============================================================================

TEST_CASE("RTI instruction", "[cpu][interrupt]") {

    SECTION("RTI restores P and PC from stack") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x2000);
        // Handler: LDA #$42; RTI
        f.mem.loadProgram(0x2000, {0xA9, 0x42, 0x40});

        // Main: CLC; CLI; NOP; LDX #$99
        f.loadAndReset(0x0400, {0x18, 0x58, 0xEA, 0xA2, 0x99});
        f.cpu->executeInstruction(); // CLC
        f.cpu->executeInstruction(); // CLI

        uint8_t pBeforeIRQ = f.cpu->getP();
        f.cpu->irq();
        f.cpu->executeInstruction(); // NOP completes
        f.cpu->executeInstruction(); // IRQ serviced, now at handler

        // Execute handler: LDA #$42
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0x42);

        // RTI should restore PC and P
        f.cpu->executeInstruction(); // RTI
        // PC should be back to where we were interrupted
        // After RTI, should execute LDX #$99
        f.cpu->executeInstruction(); // LDX #$99
        REQUIRE(f.cpu->getX() == 0x99);
    }

    SECTION("RTI restores flags correctly") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x2000);
        // Handler: SEC; RTI (modifies carry, but RTI restores original flags)
        f.mem.loadProgram(0x2000, {0x38, 0x40});

        // Main: CLC; CLI; NOP
        f.loadAndReset(0x0400, {0x18, 0x58, 0xEA, 0xEA});
        f.cpu->executeInstruction(); // CLC -> C=0
        f.cpu->executeInstruction(); // CLI

        f.cpu->irq();
        // IRQ is serviced at the start of the next executeInstruction call,
        // before fetching any opcode. So the IRQ fires immediately.
        f.cpu->executeInstruction(); // IRQ serviced (pushes PC and P, jumps to handler)

        f.cpu->executeInstruction(); // SEC -> C=1 in handler
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);

        f.cpu->executeInstruction(); // RTI -> restores original P with C=0
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }
}
