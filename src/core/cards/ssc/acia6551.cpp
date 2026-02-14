/*
 * acia6551.cpp - MOS 6551 ACIA implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "acia6551.hpp"
#include <cstring>

namespace a2e {

// Register offsets
static constexpr uint8_t REG_DATA    = 0;  // Transmit/Receive data
static constexpr uint8_t REG_STATUS  = 1;  // Status (read) / Programmed Reset (write)
static constexpr uint8_t REG_COMMAND = 2;  // Command register
static constexpr uint8_t REG_CONTROL = 3;  // Control register

// Status register bits
static constexpr uint8_t STATUS_IRQ     = 0x80;  // Bit 7: IRQ active
static constexpr uint8_t STATUS_DSR     = 0x40;  // Bit 6: DSR
static constexpr uint8_t STATUS_DCD     = 0x20;  // Bit 5: DCD
static constexpr uint8_t STATUS_TDRE    = 0x10;  // Bit 4: Transmit Data Register Empty
static constexpr uint8_t STATUS_RDRF    = 0x08;  // Bit 3: Receive Data Register Full
static constexpr uint8_t STATUS_OVERRUN = 0x04;  // Bit 2: Overrun error
static constexpr uint8_t STATUS_FRAMING = 0x02;  // Bit 1: Framing error
static constexpr uint8_t STATUS_PARITY  = 0x01;  // Bit 0: Parity error

// Command register bits
static constexpr uint8_t CMD_IRQ_DISABLE = 0x02;  // Bit 1: IRQ disable
static constexpr uint8_t CMD_DTR         = 0x01;  // Bit 0: DTR control
static constexpr uint8_t CMD_TX_MASK     = 0x0C;  // Bits 3-2: Transmit interrupt control

uint8_t ACIA6551::read(uint8_t reg) {
    switch (reg & 0x03) {
        case REG_DATA: {
            // Reading data register clears RDRF and IRQ
            uint8_t data = rxData_;
            statusReg_ &= ~(STATUS_RDRF | STATUS_IRQ | STATUS_OVERRUN);
            irqActive_ = false;

            // Load next byte from buffer if available
            if (rxCount_ > 0) {
                rxData_ = rxBuffer_[rxTail_];
                rxTail_ = (rxTail_ + 1) % RX_BUFFER_SIZE;
                rxCount_--;
                statusReg_ |= STATUS_RDRF;
                updateIRQ();
            }

            return data;
        }

        case REG_STATUS:
            return statusReg_;

        case REG_COMMAND:
            return commandReg_;

        case REG_CONTROL:
            return controlReg_;
    }
    return 0xFF;
}

void ACIA6551::write(uint8_t reg, uint8_t value) {
    switch (reg & 0x03) {
        case REG_DATA:
            // Transmit data
            txData_ = value;
            if (txCallback_) {
                txCallback_(value);
            }
            // TDRE stays set since WebSocket sends instantly
            statusReg_ |= STATUS_TDRE;
            break;

        case REG_STATUS:
            // Writing to status register performs a programmed reset
            // This clears the overrun flag and resets the transmitter/receiver
            statusReg_ &= ~(STATUS_OVERRUN | STATUS_FRAMING | STATUS_PARITY);
            // Programmed reset does NOT clear RDRF or TDRE
            // It disables transmitter IRQ (sets command reg bits 3-2 to 00)
            commandReg_ &= ~CMD_TX_MASK;
            irqActive_ = false;
            statusReg_ &= ~STATUS_IRQ;
            break;

        case REG_COMMAND:
            commandReg_ = value;
            updateIRQ();
            break;

        case REG_CONTROL:
            controlReg_ = value;
            break;
    }
}

uint8_t ACIA6551::peek(uint8_t reg) const {
    switch (reg & 0x03) {
        case REG_DATA:    return rxData_;
        case REG_STATUS:  return statusReg_;
        case REG_COMMAND: return commandReg_;
        case REG_CONTROL: return controlReg_;
    }
    return 0xFF;
}

void ACIA6551::receiveData(uint8_t byte) {
    if (rxCount_ >= RX_BUFFER_SIZE) {
        // Buffer full — set overrun
        statusReg_ |= STATUS_OVERRUN;
        return;
    }

    if (!(statusReg_ & STATUS_RDRF)) {
        // Data register empty — load directly
        rxData_ = byte;
        statusReg_ |= STATUS_RDRF;
    } else {
        // Data register full — buffer it
        rxBuffer_[rxHead_] = byte;
        rxHead_ = (rxHead_ + 1) % RX_BUFFER_SIZE;
        rxCount_++;
    }

    updateIRQ();
}

void ACIA6551::reset() {
    txData_ = 0;
    rxData_ = 0;
    statusReg_ = STATUS_TDRE;  // Transmitter ready
    commandReg_ = 0x00;
    controlReg_ = 0x00;
    rxHead_ = 0;
    rxTail_ = 0;
    rxCount_ = 0;
    irqActive_ = false;

    if (irqCallback_) {
        irqCallback_(false);
    }
}

void ACIA6551::updateIRQ() {
    bool shouldIRQ = false;

    // IRQ on receive if enabled
    if ((statusReg_ & STATUS_RDRF) && !(commandReg_ & CMD_IRQ_DISABLE)) {
        shouldIRQ = true;
    }

    // IRQ on transmit if enabled (command bits 3-2 = 01)
    if ((statusReg_ & STATUS_TDRE) && ((commandReg_ & CMD_TX_MASK) == 0x04)) {
        shouldIRQ = true;
    }

    if (shouldIRQ) {
        statusReg_ |= STATUS_IRQ;
    } else {
        statusReg_ &= ~STATUS_IRQ;
    }

    if (irqActive_ != shouldIRQ) {
        irqActive_ = shouldIRQ;
        if (irqCallback_) {
            irqCallback_(shouldIRQ);
        }
    }
}

size_t ACIA6551::serialize(uint8_t* buffer, size_t maxSize) const {
    if (maxSize < STATE_SIZE) return 0;

    size_t offset = 0;

    // Registers (6 bytes)
    buffer[offset++] = txData_;
    buffer[offset++] = rxData_;
    buffer[offset++] = statusReg_;
    buffer[offset++] = commandReg_;
    buffer[offset++] = controlReg_;
    buffer[offset++] = irqActive_ ? 1 : 0;

    // RX buffer indices (6 bytes)
    buffer[offset++] = static_cast<uint8_t>(rxHead_ & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((rxHead_ >> 8) & 0xFF);
    buffer[offset++] = static_cast<uint8_t>(rxTail_ & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((rxTail_ >> 8) & 0xFF);
    buffer[offset++] = static_cast<uint8_t>(rxCount_ & 0xFF);
    buffer[offset++] = static_cast<uint8_t>((rxCount_ >> 8) & 0xFF);

    // Reserved (4 bytes)
    buffer[offset++] = 0;
    buffer[offset++] = 0;
    buffer[offset++] = 0;
    buffer[offset++] = 0;

    // RX buffer contents (256 bytes)
    std::memcpy(buffer + offset, rxBuffer_, RX_BUFFER_SIZE);
    offset += RX_BUFFER_SIZE;

    // Tail padding (4 bytes)
    buffer[offset++] = 0;
    buffer[offset++] = 0;
    buffer[offset++] = 0;
    buffer[offset++] = 0;

    return offset;
}

size_t ACIA6551::deserialize(const uint8_t* buffer, size_t size) {
    if (size < STATE_SIZE) return 0;

    size_t offset = 0;

    // Registers
    txData_ = buffer[offset++];
    rxData_ = buffer[offset++];
    statusReg_ = buffer[offset++];
    commandReg_ = buffer[offset++];
    controlReg_ = buffer[offset++];
    irqActive_ = buffer[offset++] != 0;

    // RX buffer indices
    rxHead_ = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;
    rxTail_ = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;
    rxCount_ = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;

    // Reserved
    offset += 4;

    // RX buffer contents
    std::memcpy(rxBuffer_, buffer + offset, RX_BUFFER_SIZE);
    offset += RX_BUFFER_SIZE;

    // Tail padding
    offset += 4;

    return offset;
}

} // namespace a2e
