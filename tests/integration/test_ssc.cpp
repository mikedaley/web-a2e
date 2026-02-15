/*
 * test_ssc.cpp - Tests for the Super Serial Card and ACIA 6551
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"
#include "cards/ssc/acia6551.hpp"
#include "cards/ssc/ssc_card.hpp"

using namespace a2e;

// ===========================================================================
// ACIA 6551 Unit Tests
// ===========================================================================

TEST_CASE("ACIA6551 reset state", "[ssc][acia]") {
    ACIA6551 acia;

    // TDRE should be set (ready to transmit), everything else clear
    REQUIRE(acia.getStatusReg() == 0x10);
    REQUIRE(acia.getCommandReg() == 0x00);
    REQUIRE(acia.getControlReg() == 0x00);
    REQUIRE(acia.isIRQActive() == false);
}

TEST_CASE("ACIA6551 read status register", "[ssc][acia]") {
    ACIA6551 acia;
    REQUIRE(acia.read(1) == 0x10);  // TDRE set
}

TEST_CASE("ACIA6551 write and read command register", "[ssc][acia]") {
    ACIA6551 acia;
    acia.write(2, 0x0B);  // DTR on, IRQ disabled, TX IRQ off
    REQUIRE(acia.read(2) == 0x0B);
}

TEST_CASE("ACIA6551 write and read control register", "[ssc][acia]") {
    ACIA6551 acia;
    acia.write(3, 0x1E);  // 9600 baud, internal clock, 8N1
    REQUIRE(acia.read(3) == 0x1E);
}

TEST_CASE("ACIA6551 transmit fires callback", "[ssc][acia]") {
    ACIA6551 acia;
    uint8_t sent = 0;
    acia.setTxCallback([&](uint8_t b) { sent = b; });

    acia.write(0, 0x41);  // Transmit 'A'
    REQUIRE(sent == 0x41);
}

TEST_CASE("ACIA6551 receive sets RDRF", "[ssc][acia]") {
    ACIA6551 acia;

    // Initially no data
    REQUIRE((acia.getStatusReg() & 0x08) == 0);

    acia.receiveData(0x42);  // Receive 'B'

    // RDRF should be set
    REQUIRE((acia.getStatusReg() & 0x08) == 0x08);
}

TEST_CASE("ACIA6551 read data clears RDRF", "[ssc][acia]") {
    ACIA6551 acia;
    acia.receiveData(0x42);

    uint8_t data = acia.read(0);  // Read data register
    REQUIRE(data == 0x42);

    // RDRF should be cleared
    REQUIRE((acia.getStatusReg() & 0x08) == 0);
}

TEST_CASE("ACIA6551 receive multiple bytes buffers correctly", "[ssc][acia]") {
    ACIA6551 acia;

    acia.receiveData(0x41);  // 'A' — goes directly to data register
    acia.receiveData(0x42);  // 'B' — buffered
    acia.receiveData(0x43);  // 'C' — buffered

    REQUIRE(acia.read(0) == 0x41);
    // After reading A, B should load into data register
    REQUIRE((acia.getStatusReg() & 0x08) == 0x08);  // RDRF still set
    REQUIRE(acia.read(0) == 0x42);
    REQUIRE((acia.getStatusReg() & 0x08) == 0x08);  // RDRF still set
    REQUIRE(acia.read(0) == 0x43);
    REQUIRE((acia.getStatusReg() & 0x08) == 0);      // RDRF cleared
}

TEST_CASE("ACIA6551 programmed reset clears errors", "[ssc][acia]") {
    ACIA6551 acia;
    acia.write(2, 0x04);  // Set TX IRQ control

    acia.write(1, 0x00);  // Write to status = programmed reset

    // TX interrupt control should be cleared
    REQUIRE((acia.getCommandReg() & 0x0C) == 0x00);
}

TEST_CASE("ACIA6551 IRQ fires on receive when enabled", "[ssc][acia]") {
    ACIA6551 acia;
    bool irqFired = false;
    acia.setIRQCallback([&](bool active) { if (active) irqFired = true; });

    // Enable IRQ (bit 1 = 0 means enabled) + DTR
    acia.write(2, 0x01);

    acia.receiveData(0x41);
    REQUIRE(irqFired == true);
    REQUIRE(acia.isIRQActive() == true);
    REQUIRE((acia.getStatusReg() & 0x80) == 0x80);  // IRQ bit in status
}

TEST_CASE("ACIA6551 IRQ does not fire when disabled", "[ssc][acia]") {
    ACIA6551 acia;
    bool irqFired = false;
    acia.setIRQCallback([&](bool active) { if (active) irqFired = true; });

    // Disable IRQ (bit 1 = 1)
    acia.write(2, 0x02);

    acia.receiveData(0x41);
    REQUIRE(irqFired == false);
    REQUIRE(acia.isIRQActive() == false);
}

TEST_CASE("ACIA6551 serialization round-trip", "[ssc][acia]") {
    ACIA6551 acia;
    acia.write(2, 0x0B);
    acia.write(3, 0x1E);
    acia.receiveData(0x41);

    uint8_t buf[ACIA6551::STATE_SIZE];
    size_t written = acia.serialize(buf, sizeof(buf));
    REQUIRE(written == ACIA6551::STATE_SIZE);

    ACIA6551 acia2;
    size_t read = acia2.deserialize(buf, written);
    REQUIRE(read == ACIA6551::STATE_SIZE);

    REQUIRE(acia2.getCommandReg() == 0x0B);
    REQUIRE(acia2.getControlReg() == 0x1E);
    REQUIRE((acia2.getStatusReg() & 0x08) == 0x08);  // RDRF set
}

// ===========================================================================
// SSC Card Integration Tests
// ===========================================================================

TEST_CASE("SSC card installs in slot 2", "[ssc][emulator]") {
    Emulator emu;
    emu.init();

    REQUIRE(emu.setSlotCard(2, "ssc") == true);
    REQUIRE(std::string(emu.getSlotCardName(2)) == "ssc");
    REQUIRE(emu.isSSCInstalled() == true);
}

TEST_CASE("SSC card removes cleanly", "[ssc][emulator]") {
    Emulator emu;
    emu.init();

    emu.setSlotCard(2, "ssc");
    emu.setSlotCard(2, "empty");

    REQUIRE(std::string(emu.getSlotCardName(2)) == "empty");
    REQUIRE(emu.isSSCInstalled() == false);
}

TEST_CASE("SSC DIP switch defaults readable via I/O", "[ssc][emulator]") {
    Emulator emu;
    emu.init();
    emu.setSlotCard(2, "ssc");

    // SW1 at $C0A1, SW2 at $C0A2
    REQUIRE(emu.peekMemory(0xC0A1) == 0x16);
    REQUIRE(emu.peekMemory(0xC0A2) == 0x00);
}

TEST_CASE("SSC ACIA status readable via I/O", "[ssc][emulator]") {
    Emulator emu;
    emu.init();
    emu.setSlotCard(2, "ssc");

    // Status register at $C0A9 — TDRE should be set
    REQUIRE(emu.peekMemory(0xC0A9) == 0x10);
}

TEST_CASE("SSC serial receive makes data available", "[ssc][emulator]") {
    Emulator emu;
    emu.init();
    emu.setSlotCard(2, "ssc");

    emu.serialReceive(0x48);  // 'H'

    // RDRF should be set in status
    REQUIRE((emu.peekMemory(0xC0A9) & 0x08) == 0x08);
}

TEST_CASE("SSC slot ROM has real firmware", "[ssc][emulator]") {
    Emulator emu;
    emu.init();
    emu.setSlotCard(2, "ssc");

    // SSC ROM ID bytes: $Cn05=38, $Cn07=18, $Cn0B=01
    REQUIRE(emu.peekMemory(0xC205) == 0x38);
    REQUIRE(emu.peekMemory(0xC207) == 0x18);
    REQUIRE(emu.peekMemory(0xC20B) == 0x01);
}

TEST_CASE("SSC transmit fires callback", "[ssc][emulator]") {
    Emulator emu;
    emu.init();
    emu.setSlotCard(2, "ssc");

    uint8_t sent = 0;
    emu.setSerialTxCallback([&](uint8_t b) { sent = b; });

    // Write to data register at $C0A8
    emu.writeMemory(0xC0A8, 0x55);
    REQUIRE(sent == 0x55);
}
