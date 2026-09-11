/*
 * serial_port.cpp - A //c's built-in serial ports
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "serial_port.hpp"

namespace a2e {

namespace {
// The ACIA answers at the slot's offsets 8-B, as it does on an SSC: $C098 and
// $C0A8 are the data register of ports 1 and 2.
constexpr uint8_t ACIA_SELECT = 0x08;
constexpr uint8_t ACIA_REGISTER = 0x03;
} // namespace

SerialPort::SerialPort(uint8_t port) : port_(port == 2 ? 2 : 1) {
    acia_.setIRQCallback([this](bool active) {
        if (active && irqCallback_) irqCallback_();
    });
    reset();
}

uint8_t SerialPort::readIO(uint8_t offset) {
    if (offset & ACIA_SELECT) return acia_.read(offset & ACIA_REGISTER);

    // A card would have DIP switches to read here. A port has no switches to
    // set: its speed and format are the firmware's, kept in the machine's own
    // screen holes rather than in hardware.
    return 0x00;
}

void SerialPort::writeIO(uint8_t offset, uint8_t value) {
    if (offset & ACIA_SELECT) acia_.write(offset & ACIA_REGISTER, value);
}

uint8_t SerialPort::peekIO(uint8_t offset) const {
    if (offset & ACIA_SELECT) return acia_.peek(offset & ACIA_REGISTER);
    return 0x00;
}

void SerialPort::reset() {
    acia_.reset();
}

void SerialPort::setSerialTxCallback(SerialTxCallback cb) {
    acia_.setTxCallback([cb = std::move(cb)](uint8_t byte) {
        if (cb) cb(byte);
    });
}

void SerialPort::serialReceive(uint8_t byte) {
    acia_.receiveData(byte);
}

size_t SerialPort::serialize(uint8_t* buffer, size_t maxSize) const {
    if (!buffer || maxSize < STATE_SIZE) return 0;

    size_t offset = 0;
    buffer[offset++] = port_;
    buffer[offset++] = 0; // reserved
    buffer[offset++] = 0;
    buffer[offset++] = 0;

    const size_t aciaBytes = acia_.serialize(buffer + offset, maxSize - offset);
    if (aciaBytes == 0) return 0;
    return offset + aciaBytes;
}

size_t SerialPort::deserialize(const uint8_t* buffer, size_t size) {
    if (!buffer || size < STATE_SIZE) return 0;

    // The port number is which socket this object is, not state to restore: a
    // machine's port 2 stays port 2 whatever a saved state says.
    size_t offset = 4;
    const size_t aciaBytes = acia_.deserialize(buffer + offset, size - offset);
    if (aciaBytes == 0) return 0;
    return offset + aciaBytes;
}

} // namespace a2e
