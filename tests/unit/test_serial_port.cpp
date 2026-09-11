/*
 * test_serial_port.cpp - Unit tests for a //c's built-in serial ports
 *
 * The chip is the SSC's 6551 and is tested with the card; what is tested here
 * is the port around it — where it answers, what it does not have, and that
 * the two directions of the line reach it.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "serial_port.hpp"

#include <string>
#include <vector>

using namespace a2e;

namespace {
// The ACIA sits at the slot's offsets 8-B: $C098-$C09B for port 1 and
// $C0A8-$C0AB for port 2.
constexpr uint8_t DATA = 0x08;
constexpr uint8_t STATUS = 0x09;
constexpr uint8_t COMMAND = 0x0A;
constexpr uint8_t CONTROL = 0x0B;

constexpr uint8_t STATUS_TDRE = 0x10;
constexpr uint8_t STATUS_RDRF = 0x08;
} // namespace

TEST_CASE("A serial port knows which socket it is", "[serial]") {
    SerialPort printer(1);
    SerialPort modem(2);

    REQUIRE(printer.getPort() == 1);
    REQUIRE(std::string(printer.getName()) == "Serial Port 1");
    REQUIRE(printer.getPreferredSlot() == 1);

    REQUIRE(modem.getPort() == 2);
    REQUIRE(std::string(modem.getName()) == "Serial Port 2");
    REQUIRE(modem.getPreferredSlot() == 2);
}

TEST_CASE("A serial port has no ROM, because a //c's firmware is not in one",
          "[serial]") {
    // The difference from the Super Serial Card that matters to the MMU: the
    // card's firmware is in a socket on it, and a //c's is part of the system
    // ROM along with everything else its slot addresses answer with.
    SerialPort port(1);
    REQUIRE_FALSE(port.hasROM());
    REQUIRE_FALSE(port.hasExpansionROM());
}

TEST_CASE("The ACIA answers at offsets 8 to B and nowhere else", "[serial]") {
    SerialPort port(1);

    // The control and command registers read back what was written, which is
    // how firmware confirms it is talking to a chip at all.
    port.writeIO(CONTROL, 0x1E);
    port.writeIO(COMMAND, 0x0B);
    REQUIRE(port.readIO(CONTROL) == 0x1E);
    REQUIRE(port.readIO(COMMAND) == 0x0B);
    REQUIRE((port.readIO(STATUS) & STATUS_TDRE) != 0);

    SECTION("and a card's DIP switches are not there to read") {
        // An SSC answers its switch settings at offsets 1 and 2. A port has no
        // switches: its speed and format are the firmware's.
        for (uint8_t offset = 0; offset < 8; offset++) {
            INFO("offset " << static_cast<int>(offset));
            REQUIRE(port.readIO(offset) == 0x00);
        }
    }
}

TEST_CASE("What the machine writes goes out of the port", "[serial]") {
    SerialPort port(1);
    std::vector<uint8_t> sent;
    port.setSerialTxCallback([&sent](uint8_t byte) { sent.push_back(byte); });

    for (char c : std::string("HI")) {
        port.writeIO(DATA, static_cast<uint8_t>(c));
    }

    REQUIRE(sent.size() == 2);
    REQUIRE(sent[0] == 'H');
    REQUIRE(sent[1] == 'I');
}

TEST_CASE("What arrives at the port reaches the machine", "[serial]") {
    SerialPort port(2);

    REQUIRE((port.readIO(STATUS) & STATUS_RDRF) == 0);
    port.serialReceive('A');
    REQUIRE((port.readIO(STATUS) & STATUS_RDRF) != 0);

    SECTION("a peek leaves it there for the machine to read") {
        REQUIRE(port.peekIO(DATA) == 'A');
        REQUIRE((port.readIO(STATUS) & STATUS_RDRF) != 0);
    }

    SECTION("and reading it takes it") {
        REQUIRE(port.readIO(DATA) == 'A');
        REQUIRE((port.readIO(STATUS) & STATUS_RDRF) == 0);
    }

    SECTION("bytes queue up in order behind the one being read") {
        port.serialReceive('B');
        port.serialReceive('C');
        REQUIRE(port.readIO(DATA) == 'A');
        REQUIRE(port.readIO(DATA) == 'B');
        REQUIRE(port.readIO(DATA) == 'C');
    }
}

TEST_CASE("A serial port's state survives a round trip", "[serial][state]") {
    SerialPort saved(2);
    saved.writeIO(CONTROL, 0x1E);
    saved.writeIO(COMMAND, 0x0B);
    saved.serialReceive('Z');

    std::vector<uint8_t> buffer(saved.getStateSize());
    const size_t written = saved.serialize(buffer.data(), buffer.size());
    REQUIRE(written > 0);

    SerialPort restored(2);
    REQUIRE(restored.deserialize(buffer.data(), written) > 0);
    REQUIRE(restored.readIO(CONTROL) == 0x1E);
    REQUIRE(restored.readIO(COMMAND) == 0x0B);
    REQUIRE(restored.readIO(DATA) == 'Z');

    SECTION("but not which socket it is, because that is not state") {
        // A machine's port 2 is port 2 whatever a saved state claims.
        SerialPort otherPort(1);
        REQUIRE(otherPort.deserialize(buffer.data(), written) > 0);
        REQUIRE(otherPort.getPort() == 1);
    }
}

TEST_CASE("A reset leaves the port ready to transmit", "[serial]") {
    SerialPort port(1);
    port.serialReceive('X');
    port.reset();

    REQUIRE((port.readIO(STATUS) & STATUS_TDRE) != 0);
    REQUIRE((port.readIO(STATUS) & STATUS_RDRF) == 0);
    REQUIRE_FALSE(port.isIRQActive());
}
