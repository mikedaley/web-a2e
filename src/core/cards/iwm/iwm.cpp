/*
 * iwm.cpp - The Integrated Woz Machine, a //c's disk controller
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iwm.hpp"

namespace a2e {

namespace {
// The mode register is five bits wide; the top three read back as zero.
constexpr uint8_t MODE_MASK = 0x1F;

// Status register
constexpr uint8_t STATUS_SENSE = 0x80; // The line ca2/ca1/ca0 selected
constexpr uint8_t STATUS_MOTOR = 0x20; // ENABLE, the drive motor

// Handshake register
constexpr uint8_t HANDSHAKE_READY = 0x80;    // Write data register is empty
constexpr uint8_t HANDSHAKE_NO_UNDERRUN = 0x40;
} // namespace

IWM::Register IWM::selectedRegister() const {
    if (!q7_) return q6_ ? Register::Status : Register::Data;
    return q6_ ? Register::Write : Register::Handshake;
}

uint8_t IWM::readStatus() const {
    // SENSE is whichever drive line the ca lines have selected. The firmware
    // asks for exactly one of them on a 5.25" drive — write protect — and the
    // sequencer already senses it the card's way, so that is what is reported.
    uint8_t status = mode_ & MODE_MASK;
    const DiskImage* disk = getDiskImage(selectedDrive_);
    if (disk && disk->isWriteProtected()) status |= STATUS_SENSE;
    if (isMotorOn()) status |= STATUS_MOTOR;
    return status;
}

uint8_t IWM::readHandshake() const {
    // The emulated sequencer consumes a written byte within the bit cell it
    // was loaded in, so the write data register is always empty when asked and
    // there is never an underrun. A real chip can report both, and firmware
    // polls bit 7 before loading the next byte; answering "ready" is what lets
    // that loop run rather than spin.
    return HANDSHAKE_READY | HANDSHAKE_NO_UNDERRUN;
}

uint8_t IWM::readIO(uint8_t offset) {
    // Every one of the sixteen addresses is a state line, set by odd addresses
    // and cleared by even ones; which register the read returns is then the
    // Q7/Q6 pair's business, not the address's. The card behaves the same way
    // — its reads all return the data latch — so the switch handling, the
    // sequencer catch-up and the stepper are the shared ones.
    const uint8_t fromSwitch = handleSoftSwitch(offset & 0x0F, false);

    switch (selectedRegister()) {
    case Register::Data:
        return fromSwitch;
    case Register::Status:
        return readStatus();
    case Register::Handshake:
        return readHandshake();
    case Register::Write:
        break;
    }
    return dataRegister_;
}

void IWM::writeIO(uint8_t offset, uint8_t value) {
    handleSoftSwitch(offset & 0x0F, true);

    if (selectedRegister() != Register::Write) return;

    // With both latches high the chip takes a byte, and which byte depends on
    // whether it has a disk turning: the mode register is only writable while
    // the motor is off, which is how the firmware sets the chip up before it
    // starts a drive and cannot disturb it mid-write afterwards.
    if (isMotorOn()) {
        busData_ = value;
    } else {
        mode_ = value & MODE_MASK;
    }
}

uint8_t IWM::peekIO(uint8_t offset) const {
    // A peek is the debugger looking, so it answers from the state the chip is
    // already in rather than touching a state line on the way past.
    (void)offset;
    switch (selectedRegister()) {
    case Register::Status:
        return readStatus();
    case Register::Handshake:
        return readHandshake();
    case Register::Data:
    case Register::Write:
        break;
    }
    return dataRegister_;
}

void IWM::reset() {
    DiskController::reset();
    mode_ = 0;
}

size_t IWM::serialize(uint8_t* buffer, size_t maxSize) const {
    size_t offset = DiskController::serialize(buffer, maxSize);
    if (offset == 0 || offset >= maxSize) return offset;
    buffer[offset++] = mode_;
    return offset;
}

size_t IWM::deserialize(const uint8_t* buffer, size_t size) {
    size_t offset = DiskController::deserialize(buffer, size);
    if (offset == 0) return 0;
    // A state written before the chip had a mode register leaves it at zero,
    // which is what the firmware finds on a machine that has just come up.
    mode_ = (offset < size) ? (buffer[offset++] & MODE_MASK) : 0;
    return offset;
}

} // namespace a2e
