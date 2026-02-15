/*
 * ssc_card.cpp - Apple Super Serial Card implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "ssc_card.hpp"
#include "roms.cpp" // For embedded ROM data
#include <cstring>

namespace a2e {

SSCCard::SSCCard()
    : rom_(roms::ROM_SSC)
    , romSize_(roms::ROM_SSC_SIZE)
{
    // Wire ACIA IRQ callback to our slot IRQ
    acia_.setIRQCallback([this](bool active) {
        if (active && irqCallback_) {
            irqCallback_();
        }
    });

    reset();
}

uint8_t SSCCard::readIO(uint8_t offset) {
    // Offsets with bit 3 set → route to ACIA (offsets 8-B)
    if (offset & 0x08) {
        return acia_.read(offset & 0x03);
    }

    // DIP switch reads
    switch (offset & 0x0F) {
        case 0x01: return sw1_;
        case 0x02: return sw2_;
        default:   return 0x00;
    }
}

void SSCCard::writeIO(uint8_t offset, uint8_t value) {
    // Offsets with bit 3 set → route to ACIA
    if (offset & 0x08) {
        acia_.write(offset & 0x03, value);
    }
}

uint8_t SSCCard::peekIO(uint8_t offset) const {
    if (offset & 0x08) {
        return acia_.peek(offset & 0x03);
    }

    switch (offset & 0x0F) {
        case 0x01: return sw1_;
        case 0x02: return sw2_;
        default:   return 0x00;
    }
}

uint8_t SSCCard::readROM(uint8_t offset) {
    // SSC slot ROM ($Cn00-$CnFF) maps to the LAST 256 bytes of the 2KB ROM
    // (offset $700-$7FF in the ROM chip)
    if (rom_ && romSize_ >= 2048) {
        return rom_[0x700 + offset];
    }
    return 0xFF;
}

uint8_t SSCCard::readExpansionROM(uint16_t offset) {
    // Expansion ROM maps the full 2KB ROM at $C800-$CFFF
    if (rom_ && offset < romSize_) {
        return rom_[offset];
    }
    return 0xFF;
}

void SSCCard::reset() {
    acia_.reset();
}

void SSCCard::update(int cycles) {
    // No baud rate throttling for now — data delivered as fast as it arrives
    (void)cycles;
}

void SSCCard::setSerialTxCallback(SerialTxCallback cb) {
    acia_.setTxCallback([cb = std::move(cb)](uint8_t byte) {
        if (cb) cb(byte);
    });
}

void SSCCard::serialReceive(uint8_t byte) {
    acia_.receiveData(byte);
}

size_t SSCCard::serialize(uint8_t* buffer, size_t maxSize) const {
    if (maxSize < STATE_SIZE) return 0;

    size_t offset = 0;

    // DIP switches and slot number
    buffer[offset++] = sw1_;
    buffer[offset++] = sw2_;
    buffer[offset++] = slotNumber_;
    buffer[offset++] = 0; // reserved

    // ACIA state
    size_t aciaBytes = acia_.serialize(buffer + offset, maxSize - offset);
    if (aciaBytes == 0) return 0;
    offset += aciaBytes;

    return offset;
}

size_t SSCCard::deserialize(const uint8_t* buffer, size_t size) {
    if (size < STATE_SIZE) return 0;

    size_t offset = 0;

    // DIP switches and slot number
    sw1_ = buffer[offset++];
    sw2_ = buffer[offset++];
    slotNumber_ = buffer[offset++];
    offset++; // reserved

    // ACIA state
    size_t aciaBytes = acia_.deserialize(buffer + offset, size - offset);
    if (aciaBytes == 0) return 0;
    offset += aciaBytes;

    return offset;
}

} // namespace a2e
