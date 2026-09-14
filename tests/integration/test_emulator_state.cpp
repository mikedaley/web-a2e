/*
 * test_emulator_state.cpp - Integration tests for Emulator state serialization
 *
 * Tests exportState, importState, round-trip fidelity, CPU state preservation,
 * and error handling for invalid data.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"

#include <cstring>
#include <vector>

using namespace a2e;

// ---------------------------------------------------------------------------
// Export state
// ---------------------------------------------------------------------------

TEST_CASE("Emulator exportState returns non-null data with size > 0", "[emulator][state]") {
    Emulator emu;
    emu.init();

    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);

    REQUIRE(data != nullptr);
    REQUIRE(size > 0);
}

TEST_CASE("Emulator exportState size is reasonable", "[emulator][state]") {
    Emulator emu;
    emu.init();

    size_t size = 0;
    emu.exportState(&size);

    // State includes 128KB RAM (main + aux), 16KB LC RAM, CPU state,
    // soft switches, disk state, etc.  Should be at least 128KB.
    REQUIRE(size >= 128 * 1024);
    // But not unreasonably large (under 2MB)
    REQUIRE(size < 2 * 1024 * 1024);
}

// ---------------------------------------------------------------------------
// Import state
// ---------------------------------------------------------------------------

TEST_CASE("Emulator importState accepts previously exported data", "[emulator][state]") {
    Emulator emu;
    emu.init();

    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    REQUIRE(data != nullptr);

    // Copy the exported data since import may reset internal buffers
    std::vector<uint8_t> stateCopy(data, data + size);

    bool result = emu.importState(stateCopy.data(), stateCopy.size());
    REQUIRE(result);
}

// ---------------------------------------------------------------------------
// Round-trip: memory preserved
// ---------------------------------------------------------------------------

TEST_CASE("Emulator state round-trip preserves memory", "[emulator][state]") {
    Emulator emu;
    emu.init();

    // Write known values to several RAM locations
    emu.writeMemory(0x0300, 0xDE);
    emu.writeMemory(0x0301, 0xAD);
    emu.writeMemory(0x0302, 0xBE);
    emu.writeMemory(0x0303, 0xEF);

    // Export
    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    // Reset clears memory
    emu.reset();
    // Verify memory was cleared (reset re-initializes)
    // Note: after reset, ROM re-initializes; RAM at $0300 should be cleared
    REQUIRE(emu.readMemory(0x0300) != 0xDE);

    // Import previously saved state
    bool result = emu.importState(stateCopy.data(), stateCopy.size());
    REQUIRE(result);

    // Memory should be restored
    REQUIRE(emu.readMemory(0x0300) == 0xDE);
    REQUIRE(emu.readMemory(0x0301) == 0xAD);
    REQUIRE(emu.readMemory(0x0302) == 0xBE);
    REQUIRE(emu.readMemory(0x0303) == 0xEF);
}

// ---------------------------------------------------------------------------
// Round-trip: CPU state preserved
// ---------------------------------------------------------------------------

TEST_CASE("Emulator state round-trip preserves CPU registers", "[emulator][state]") {
    Emulator emu;
    emu.init();

    // Set specific CPU register values
    emu.setA(0x42);
    emu.setX(0x13);
    emu.setY(0x77);

    // Export
    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    // Reset changes registers
    emu.reset();
    REQUIRE(emu.getA() != 0x42);

    // Import
    bool result = emu.importState(stateCopy.data(), stateCopy.size());
    REQUIRE(result);

    REQUIRE(emu.getA() == 0x42);
    REQUIRE(emu.getX() == 0x13);
    REQUIRE(emu.getY() == 0x77);
}

// ---------------------------------------------------------------------------
// Invalid data
// ---------------------------------------------------------------------------

TEST_CASE("Emulator importState rejects garbage data", "[emulator][state]") {
    Emulator emu;
    emu.init();

    // Create garbage data that does not have a valid magic header
    std::vector<uint8_t> garbage(1024, 0xFF);
    bool result = emu.importState(garbage.data(), garbage.size());
    REQUIRE_FALSE(result);
}

TEST_CASE("Emulator importState rejects too-small data", "[emulator][state]") {
    Emulator emu;
    emu.init();

    // Data smaller than the minimum header (8 bytes for magic + version)
    std::vector<uint8_t> tooSmall(4, 0x00);
    bool result = emu.importState(tooSmall.data(), tooSmall.size());
    REQUIRE_FALSE(result);
}

TEST_CASE("Emulator importState rejects empty data", "[emulator][state]") {
    Emulator emu;
    emu.init();

    bool result = emu.importState(nullptr, 0);
    REQUIRE_FALSE(result);
}

// ---------------------------------------------------------------------------
// Round-trip: BASIC running state
// ---------------------------------------------------------------------------

TEST_CASE("Emulator state round-trip restores BASIC running flag", "[emulator][state][basic]") {
    Emulator emu;
    emu.init();

    // basicProgramRunning_ is normally set by the $D912 ROM hook, which a
    // restored mid-RUN state will never cross again. Stand in for that by
    // setting CURLIN ($75/$76) to a real line number, as the ROM does while a
    // program executes.
    emu.writeMemory(0x0075, 0x0A);  // CURLIN lo = line 10
    emu.writeMemory(0x0076, 0x00);  // CURLIN hi != $FF -> not direct mode

    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    emu.reset();
    REQUIRE_FALSE(emu.isBasicProgramRunning());

    REQUIRE(emu.importState(stateCopy.data(), stateCopy.size()));
    REQUIRE(emu.isBasicProgramRunning());
}

TEST_CASE("Emulator state round-trip leaves BASIC idle at the prompt", "[emulator][state][basic]") {
    Emulator emu;
    emu.init();

    // CURLIN+1 == $FF is the ROM's direct-mode marker: sitting at the ] prompt.
    emu.writeMemory(0x0076, 0xFF);

    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    REQUIRE(emu.importState(stateCopy.data(), stateCopy.size()));
    REQUIRE_FALSE(emu.isBasicProgramRunning());
}

// ---------------------------------------------------------------------------
// The other machines
// ---------------------------------------------------------------------------

TEST_CASE("A II+ state round-trips its memory", "[emulator][state][machine]") {
    Emulator emu(MachineId::AppleIIPlus);
    emu.init();
    emu.writeMemory(0x0300, 0x5A);

    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    emu.reset();
    REQUIRE(emu.readMemory(0x0300) != 0x5A);
    REQUIRE(emu.importState(stateCopy.data(), stateCopy.size()));
    REQUIRE(emu.readMemory(0x0300) == 0x5A);
}

TEST_CASE("A //c state keeps its built-in serial port's registers", "[emulator][state][machine]") {
    Emulator emu(MachineId::AppleIIc);
    emu.init();

    // The modem port's ACIA control register: baud rate, word length, clock.
    emu.writeMemory(0xC0AB, 0x1E);
    REQUIRE(emu.peekMemory(0xC0AB) == 0x1E);

    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    emu.reset();
    REQUIRE(emu.peekMemory(0xC0AB) != 0x1E);
    REQUIRE(emu.importState(stateCopy.data(), stateCopy.size()));
    REQUIRE(emu.peekMemory(0xC0AB) == 0x1E);
}

TEST_CASE("A //c state keeps the mouse's pending movement", "[emulator][state][machine]") {
    Emulator emu(MachineId::AppleIIc);
    emu.init();
    REQUIRE(emu.getMouseIOU() != nullptr);
    emu.getMouseIOU()->addDelta(7, -3);
    REQUIRE(emu.getMouseIOU()->pendingX() == 7);

    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    emu.reset();
    REQUIRE(emu.getMouseIOU()->pendingX() == 0);
    REQUIRE(emu.importState(stateCopy.data(), stateCopy.size()));
    REQUIRE(emu.getMouseIOU()->pendingX() == 7);
    REQUIRE(emu.getMouseIOU()->pendingY() == -3);
}

TEST_CASE("A state refits the cards it was saved with", "[emulator][state][cards]") {
    Emulator saved;
    saved.init();
    REQUIRE(saved.setSlotCard(2, "ssc"));
    REQUIRE(saved.setSlotCard(1, "parallel"));
    REQUIRE(saved.setSlotCard(4, "empty")); // the Mockingboard it shipped with, pulled

    size_t size = 0;
    const uint8_t* data = saved.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    Emulator restored;
    restored.init();
    REQUIRE(restored.setSlotCard(5, "thunderclock")); // a card the state does not have
    REQUIRE(std::string(restored.getSlotCardName(2)) == "empty");
    REQUIRE(std::string(restored.getSlotCardName(4)) == "mockingboard");

    REQUIRE(restored.importState(stateCopy.data(), stateCopy.size()));
    REQUIRE(std::string(restored.getSlotCardName(1)) == "parallel");
    REQUIRE(std::string(restored.getSlotCardName(2)) == "ssc");
    REQUIRE(std::string(restored.getSlotCardName(4)) == "empty");
    REQUIRE(std::string(restored.getSlotCardName(5)) == "empty");
    REQUIRE(std::string(restored.getSlotCardName(6)) == "disk2");
}

TEST_CASE("A state from another machine is refused", "[emulator][state][machine]") {
    Emulator iie;
    iie.init();
    size_t size = 0;
    const uint8_t* data = iie.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    Emulator iic(MachineId::AppleIIc);
    iic.init();
    iic.writeMemory(0x0300, 0x77);
    REQUIRE_FALSE(iic.importState(stateCopy.data(), stateCopy.size()));
    // Refused before anything was touched
    REQUIRE(iic.readMemory(0x0300) == 0x77);

    // A IIgs's header: the same magic, its own version, its own id.
    std::vector<uint8_t> iigsHeader = {'A', 'E', '2', 'S', 1, 0, 0, 0, 3, 0, 0, 0};
    iigsHeader.resize(64, 0);
    REQUIRE_FALSE(iie.importState(iigsHeader.data(), iigsHeader.size()));
}
