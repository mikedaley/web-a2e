/*
 * serial_port.hpp - A //c's built-in serial ports
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../expansion_card.hpp"
#include "../ssc/acia6551.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace a2e {

/**
 * SerialPort - one of the two serial ports on a //c's back panel
 *
 * A //c has no slots, but it decodes slot 1 and slot 2 and puts a 6551 ACIA
 * behind each: port 1 is the printer port and port 2 the modem port. The chip
 * is the one the Super Serial Card carries, at the same four addresses within
 * the slot's sixteen — $C098-$C09B for port 1, $C0A8-$C0AB for port 2 — which
 * is why a //c runs the same serial code a //e runs against an SSC.
 *
 * There is no base class shared with SSCCard, deliberately. What the two have
 * in common is the ACIA, and they already share it the way hardware does: both
 * compose an ACIA6551 and wire its transmit, receive and interrupt lines. What
 * is left over is a card's DIP switches and its 2KB ROM against a port's
 * nothing, and a base class holding four forwarding methods would describe a
 * part that does not exist.
 *
 * Which is the real difference here: a port has no ROM. An SSC's firmware is
 * in a socket on the card; a //c's is in the system ROM, along with the rest of
 * what its slot addresses answer with.
 *
 * The two ports are not identical on a real machine — port 1 brings out no
 * handshake lines and port 2 brings out the full set — but that is a
 * difference in the cable, not in the chip, and nothing is modelled that could
 * tell them apart. The port number is carried so the host can say which socket
 * on the back it is talking to.
 */
class SerialPort : public ExpansionCard {
public:
    using SerialTxCallback = std::function<void(uint8_t)>;

    explicit SerialPort(uint8_t port);
    ~SerialPort() override = default;

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // ===== ExpansionCard Interface =====

    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    // Nothing answers in the slot's ROM space: a //c's serial firmware is part
    // of the system ROM.
    uint8_t readROM(uint8_t /*offset*/) override { return 0xFF; }
    bool hasROM() const override { return false; }
    bool hasExpansionROM() const override { return false; }

    void reset() override;
    void update(int cycles) override { (void)cycles; }

    void setIRQCallback(IRQCallback callback) override {
        irqCallback_ = std::move(callback);
    }
    bool isIRQActive() const override { return acia_.isIRQActive(); }

    const char* getName() const override {
        return port_ == 1 ? "Serial Port 1" : "Serial Port 2";
    }
    uint8_t getPreferredSlot() const override { return port_; }

    static constexpr size_t STATE_SIZE = 4 + ACIA6551::STATE_SIZE;
    size_t getStateSize() const override { return STATE_SIZE; }
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    // ===== The port itself =====

    /** Which socket on the back: 1 is the printer port, 2 the modem port. */
    uint8_t getPort() const { return port_; }

    /** Where a byte the machine transmits goes. */
    void setSerialTxCallback(SerialTxCallback cb);

    /** A byte arriving from whatever is on the other end of the cable. */
    void serialReceive(uint8_t byte);

    const ACIA6551& getACIA() const { return acia_; }

private:
    ACIA6551 acia_;
    uint8_t port_;
    IRQCallback irqCallback_;
};

} // namespace a2e
