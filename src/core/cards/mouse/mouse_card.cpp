/*
 * mouse_card.cpp - Apple Mouse Interface card implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "mouse_card.hpp"
#include "roms.cpp" // For embedded ROM data
#include "../types.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

// Mode byte bits
static constexpr uint8_t MODE_MOUSE_ON      = (1 << 0);
static constexpr uint8_t MODE_INT_MOVEMENT   = (1 << 1);
static constexpr uint8_t MODE_INT_BUTTON     = (1 << 2);
static constexpr uint8_t MODE_INT_VBL        = (1 << 3);

// Status byte bits
static constexpr uint8_t STAT_PREV_BUTTON1             = (1 << 0);
static constexpr uint8_t STAT_INT_MOVEMENT              = (1 << 1);
static constexpr uint8_t STAT_INT_BUTTON                = (1 << 2);
static constexpr uint8_t STAT_INT_VBL                   = (1 << 3);
static constexpr uint8_t STAT_CURR_BUTTON1              = (1 << 4);
static constexpr uint8_t STAT_MOVEMENT_SINCE_READMOUSE  = (1 << 5);
static constexpr uint8_t STAT_PREV_BUTTON0              = (1 << 6);
static constexpr uint8_t STAT_CURR_BUTTON0              = (1 << 7);

// PIA register select (lower 2 bits of offset)
static constexpr uint8_t PIA_PORT_A = 0;  // Port A data / DDRA
static constexpr uint8_t PIA_CRA    = 1;  // Control Register A
static constexpr uint8_t PIA_PORT_B = 2;  // Port B data / DDRB
static constexpr uint8_t PIA_CRB    = 3;  // Control Register B

// Command codes (high nibble of command byte)
static constexpr uint8_t CMD_SET   = 0x00;
static constexpr uint8_t CMD_READ  = 0x10;
static constexpr uint8_t CMD_SERV  = 0x20;
static constexpr uint8_t CMD_CLEAR = 0x30;
static constexpr uint8_t CMD_POS   = 0x40;
static constexpr uint8_t CMD_INIT  = 0x50;
static constexpr uint8_t CMD_CLAMP = 0x60;
static constexpr uint8_t CMD_HOME  = 0x70;

MouseCard::MouseCard()
    : rom_(roms::ROM_MOUSE)
    , romSize_(roms::ROM_MOUSE_SIZE)
{
    reset();
}

void MouseCard::reset() {
    // Reset PIA
    ddra_ = 0;
    ddrb_ = 0;
    ora_ = 0;
    orb_ = 0;
    ira_ = 0;
    irb_ = 0;
    cra_ = 0;
    crb_ = 0;

    // Reset mouse state
    mouseX_ = 0;
    mouseY_ = 0;
    mouseButton_ = false;
    lastButton_ = false;
    moved_ = false;
    buttonChanged_ = false;
    mode_ = 0;

    // Default clamp bounds
    clampMinX_ = 0;
    clampMaxX_ = 1023;
    clampMinY_ = 0;
    clampMaxY_ = 1023;

    // Reset interrupt state
    irqActive_ = false;
    vblInterruptPending_ = false;
    moveInterruptPending_ = false;
    buttonInterruptPending_ = false;

    // Reset VBL tracking
    wasInVBL_ = false;

    // Reset protocol state machine
    byState_ = 0;
    std::memset(byBuff_, 0, sizeof(byBuff_));
    nBuffPos_ = 0;
    nDataLen_ = 1;
    by6821B_ = 0x40;    // BIT6 set (MCU ready signal)
    irb_ = by6821B_;    // Reflect in Port B input
    lastCommand_ = 0;

    // Reset snapshot
    snapX_ = 0;
    snapY_ = 0;
}

// ============================================================================
// PIA Register Access
// ============================================================================

uint8_t MouseCard::readIO(uint8_t offset) {
    uint8_t reg = offset & 0x03;

    switch (reg) {
        case PIA_PORT_A: {
            if (cra_ & 0x04) {
                // Data register selected: return (output & DDR) | (input & ~DDR)
                uint8_t value = (ora_ & ddra_) | (ira_ & ~ddra_);
                // Reading data port clears IRQ flags (bits 7,6) in CRA
                cra_ &= 0x3F;
                updateIRQState();
                return value;
            } else {
                // DDR selected
                return ddra_;
            }
        }

        case PIA_CRA:
            return cra_;

        case PIA_PORT_B: {
            if (crb_ & 0x04) {
                // Data register selected
                uint8_t value = (orb_ & ddrb_) | (irb_ & ~ddrb_);
                // Reading data port clears IRQ flags (bits 7,6) in CRB
                crb_ &= 0x3F;
                updateIRQState();
                return value;
            } else {
                return ddrb_;
            }
        }

        case PIA_CRB:
            return crb_;
    }

    return 0xFF;
}

void MouseCard::writeIO(uint8_t offset, uint8_t value) {
    uint8_t reg = offset & 0x03;

    switch (reg) {
        case PIA_PORT_A: {
            if (cra_ & 0x04) {
                // Write to output register A
                ora_ = value;
            } else {
                // Write to DDR A
                ddra_ = value;
            }
            break;
        }

        case PIA_CRA:
            // Bits 6,7 are read-only IRQ flags
            cra_ = (cra_ & 0xC0) | (value & 0x3F);
            updateIRQState();
            break;

        case PIA_PORT_B: {
            if (crb_ & 0x04) {
                // Write to output register B
                orb_ = value;
                on6821_B(value);  // Detect clock transitions
            } else {
                // Write to DDR B
                ddrb_ = value;
            }
            break;
        }

        case PIA_CRB:
            crb_ = (crb_ & 0xC0) | (value & 0x3F);
            updateIRQState();
            break;
    }
}

uint8_t MouseCard::peekIO(uint8_t offset) const {
    uint8_t reg = offset & 0x03;
    switch (reg) {
        case PIA_PORT_A:
            if (cra_ & 0x04) {
                return (ora_ & ddra_) | (ira_ & ~ddra_);
            }
            return ddra_;
        case PIA_CRA: return cra_;
        case PIA_PORT_B:
            if (crb_ & 0x04) {
                return (orb_ & ddrb_) | (irb_ & ~ddrb_);
            }
            return ddrb_;
        case PIA_CRB: return crb_;
    }
    return 0xFF;
}

// ============================================================================
// ROM Access (banked, firmware runs natively)
// ============================================================================

uint8_t MouseCard::readROM(uint8_t offset) {
    // Port B bits 1-3 select which 256-byte page of the 2KB ROM
    uint16_t romBank = static_cast<uint16_t>(by6821B_ & 0x0E) << 7;
    uint16_t romOffset = romBank | offset;
    if (rom_ && romOffset < romSize_) {
        return rom_[romOffset];
    }
    return 0xFF;
}

// ============================================================================
// PIA Command Protocol (AppleWin-style)
// ============================================================================

void MouseCard::on6821_B(uint8_t byData) {
    // Detect transitions on Port B bits 1-5 to determine when the firmware
    // is writing a command/data byte or reading a response byte.
    // Matches AppleWin's CMouseInterface::On6821_B() exactly.

    uint8_t byDiff = (by6821B_ ^ byData) & 0x3E;  // Only check bits 1-5
    if (!byDiff) return;

    // Update bits 1-5 from firmware output; bits 0,6,7 are MCU-managed
    by6821B_ &= ~0x3E;
    by6821B_ |= byData & 0x3E;

    // BIT5: Write strobe (firmware writing data to MCU)
    if (byDiff & 0x20) {
        if (byData & 0x20) {
            // Rising edge: MCU signals "ready to read"
            by6821B_ |= 0x80;  // Set BIT7
        } else {
            // Falling edge: clock data in from Port A
            byBuff_[nBuffPos_++] = ora_;
            if (nBuffPos_ == 1) {
                onCommand();
            }
            if (nBuffPos_ == nDataLen_ || nBuffPos_ > 7) {
                onWrite();
                nBuffPos_ = 0;
            }
            by6821B_ &= ~0x80;  // Clear BIT7 for next reading
        }
    }

    // BIT4: Read strobe (firmware reading data from MCU)
    if (byDiff & 0x10) {
        if (byData & 0x10) {
            // Rising edge: MCU prepares next value
            by6821B_ &= ~0x40;  // Clear BIT6
        } else {
            // Falling edge: advance buffer and load next byte
            if (nBuffPos_) {
                nBuffPos_++;
            }
            if (nBuffPos_ == nDataLen_ || nBuffPos_ > 7) {
                nBuffPos_ = 0;  // Read complete, ready for next command
            } else {
                ira_ = byBuff_[nBuffPos_];  // Load next response byte
            }
            by6821B_ |= 0x40;  // Set BIT6 for next writing
        }
    }

    // Reflect MCU handshake signals in Port B input register
    irb_ = by6821B_;
}

void MouseCard::onCommand() {
    // Dispatch based on command byte high nibble.
    // Matches AppleWin's CMouseInterface::OnCommand() exactly.
    // nDataLen_ = total bytes in this transaction (command + data).
    // For read-back commands, byBuff_[1..N] are filled with response data.

    uint8_t cmd = byBuff_[0] & 0xF0;
    lastCommand_ = byBuff_[0];

    switch (cmd) {
        case CMD_SET:
            // Mode is in the low nibble of the command byte itself
            nDataLen_ = 1;
            mode_ = byBuff_[0] & 0x0F;
            break;

        case CMD_READ: {
            // Snapshot position, build 5-byte read response
            nDataLen_ = 6;  // command + 5 response bytes

            // Build status: keep only "moved since last read", then add buttons
            uint8_t status = 0;
            if (moved_) status |= STAT_MOVEMENT_SINCE_READMOUSE;
            snapX_ = mouseX_;
            snapY_ = mouseY_;

            if (lastButton_) status |= STAT_PREV_BUTTON0;
            lastButton_ = mouseButton_;
            if (mouseButton_) status |= STAT_CURR_BUTTON0;

            byBuff_[1] = static_cast<uint8_t>(snapX_ & 0xFF);
            byBuff_[2] = static_cast<uint8_t>((snapX_ >> 8) & 0xFF);
            byBuff_[3] = static_cast<uint8_t>(snapY_ & 0xFF);
            byBuff_[4] = static_cast<uint8_t>((snapY_ >> 8) & 0xFF);
            byBuff_[5] = status;

            moved_ = false;
            break;
        }

        case CMD_SERV: {
            // Return interrupt status, deassert IRQ
            nDataLen_ = 2;

            uint8_t irqStatus = 0;
            if (moveInterruptPending_)   irqStatus |= STAT_INT_MOVEMENT;
            if (buttonInterruptPending_) irqStatus |= STAT_INT_BUTTON;
            if (vblInterruptPending_)    irqStatus |= STAT_INT_VBL;
            byBuff_[1] = irqStatus;

            // Deassert IRQ (pending flags cleared so next event re-triggers)
            vblInterruptPending_ = false;
            moveInterruptPending_ = false;
            buttonInterruptPending_ = false;
            irqActive_ = false;
            cra_ &= 0x3F;
            break;
        }

        case CMD_CLEAR:
            nDataLen_ = 1;
            nBuffPos_ = 0;
            mouseX_ = 0;
            mouseY_ = 0;
            snapX_ = 0;
            snapY_ = 0;
            lastButton_ = false;
            moved_ = false;
            break;

        case CMD_POS:
            nDataLen_ = 5;  // command + 4 data bytes
            break;

        case CMD_INIT:
            nDataLen_ = 3;  // command + 2 protocol bytes
            byBuff_[1] = 0xFF;  // Acknowledgment byte
            break;

        case CMD_CLAMP:
            nDataLen_ = 5;  // command + 4 data bytes
            break;

        case CMD_HOME:
            nDataLen_ = 1;
            mouseX_ = 0;
            mouseY_ = 0;
            break;

        default:
            nDataLen_ = 1;
            break;
    }

    // Preload first response byte into Port A input for firmware to read
    ira_ = byBuff_[1];
}

void MouseCard::onWrite() {
    // Process buffered write data after all expected bytes received.
    // Matches AppleWin's CMouseInterface::OnWrite() data format.

    switch (byBuff_[0] & 0xF0) {
        case CMD_CLAMP: {
            // Buffer: [cmd, minLo, maxLo, minHi, maxHi]
            // Axis from bit 0 of command: 0=X, 1=Y
            int16_t minVal = static_cast<int16_t>(
                byBuff_[1] | (byBuff_[3] << 8));
            int16_t maxVal = static_cast<int16_t>(
                byBuff_[2] | (byBuff_[4] << 8));

            if (byBuff_[0] & 1) {
                // Clamp Y
                clampMinY_ = minVal;
                clampMaxY_ = maxVal;
                if (mouseY_ < clampMinY_) mouseY_ = clampMinY_;
                if (mouseY_ > clampMaxY_) mouseY_ = clampMaxY_;
            } else {
                // Clamp X
                clampMinX_ = minVal;
                clampMaxX_ = maxVal;
                if (mouseX_ < clampMinX_) mouseX_ = clampMinX_;
                if (mouseX_ > clampMaxX_) mouseX_ = clampMaxX_;
            }
            break;
        }

        case CMD_POS:
            // Buffer: [cmd, Xlo, Xhi, Ylo, Yhi]
            mouseX_ = static_cast<int16_t>(
                byBuff_[1] | (byBuff_[2] << 8));
            mouseY_ = static_cast<int16_t>(
                byBuff_[3] | (byBuff_[4] << 8));
            // Apply clamping
            if (mouseX_ < clampMinX_) mouseX_ = clampMinX_;
            if (mouseX_ > clampMaxX_) mouseX_ = clampMaxX_;
            if (mouseY_ < clampMinY_) mouseY_ = clampMinY_;
            if (mouseY_ > clampMaxY_) mouseY_ = clampMaxY_;
            break;

        case CMD_INIT:
            clampMinX_ = 0;
            clampMaxX_ = 1023;
            clampMinY_ = 0;
            clampMaxY_ = 1023;
            mouseX_ = 0;
            mouseY_ = 0;
            snapX_ = 0;
            snapY_ = 0;
            break;

        default:
            break;
    }
}

// ============================================================================
// VBL Interrupt Generation
// ============================================================================

void MouseCard::update(int cycles) {
    if (!cycleCallback_) return;

    uint64_t totalCycles = cycleCallback_();

    // Detect VBL transition (first non-visible scanline). Where that falls is
    // the machine's business, not the card's, so it comes from the profile.
    const auto &timing = machine_->timing;
    uint64_t cycleInFrame = totalCycles % timing.cyclesPerFrame();
    int scanline = static_cast<int>(cycleInFrame / timing.cyclesPerScanline);
    bool inVBL = (scanline >= timing.visibleScanlines);

    // Detect transition into VBL
    if (inVBL && !wasInVBL_) {
        if (mode_ & MODE_INT_VBL) {
            vblInterruptPending_ = true;
        }

        // Also check for movement/button interrupts at VBL
        if ((mode_ & MODE_INT_MOVEMENT) && moved_) {
            moveInterruptPending_ = true;
        }
        if ((mode_ & MODE_INT_BUTTON) && buttonChanged_) {
            buttonInterruptPending_ = true;
            buttonChanged_ = false;
        }

        // Fire IRQ if any interrupt is pending and mouse is enabled
        if ((mode_ & MODE_MOUSE_ON) &&
            (vblInterruptPending_ || moveInterruptPending_ || buttonInterruptPending_)) {
            irqActive_ = true;
            cra_ |= 0x80;
            if (irqCallback_) {
                irqCallback_();
            }
        }
    }

    wasInVBL_ = inVBL;
}

// ============================================================================
// Mouse Input
// ============================================================================

void MouseCard::addDelta(int dx, int dy) {
    if (dx == 0 && dy == 0) return;

    mouseX_ += static_cast<int16_t>(dx);
    mouseY_ += static_cast<int16_t>(dy);

    // Apply clamping
    if (mouseX_ < clampMinX_) mouseX_ = clampMinX_;
    if (mouseX_ > clampMaxX_) mouseX_ = clampMaxX_;
    if (mouseY_ < clampMinY_) mouseY_ = clampMinY_;
    if (mouseY_ > clampMaxY_) mouseY_ = clampMaxY_;

    moved_ = true;
}

void MouseCard::setMouseButton(bool pressed) {
    if (pressed != mouseButton_) {
        lastButton_ = mouseButton_;
        mouseButton_ = pressed;
        buttonChanged_ = true;
    }
}

void MouseCard::updateIRQState() {
    // IRQ is active if any PIA IRQ flag is set and the corresponding
    // control register enables it
    bool piaIRQ = (cra_ & 0x80) || (crb_ & 0x80);
    if (!piaIRQ) {
        irqActive_ = false;
    }
}

uint8_t MouseCard::readExpansionROM(uint16_t offset) {
    // The mouse card does not use expansion ROM - all firmware is
    // accessed through banked slot ROM. This should not be called.
    (void)offset;
    return 0xFF;
}

// ============================================================================
// State Serialization
// ============================================================================

size_t MouseCard::serialize(uint8_t* buffer, size_t maxSize) const {
    if (maxSize < STATE_SIZE) return 0;

    size_t off = 0;

    // PIA state (8 bytes)
    buffer[off++] = ddra_;
    buffer[off++] = ddrb_;
    buffer[off++] = ora_;
    buffer[off++] = orb_;
    buffer[off++] = ira_;
    buffer[off++] = irb_;
    buffer[off++] = cra_;
    buffer[off++] = crb_;

    // Mouse position (8 bytes)
    buffer[off++] = static_cast<uint8_t>(mouseX_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((mouseX_ >> 8) & 0xFF);
    buffer[off++] = static_cast<uint8_t>(mouseY_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((mouseY_ >> 8) & 0xFF);

    // Clamp bounds (8 bytes)
    buffer[off++] = static_cast<uint8_t>(clampMinX_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((clampMinX_ >> 8) & 0xFF);
    buffer[off++] = static_cast<uint8_t>(clampMaxX_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((clampMaxX_ >> 8) & 0xFF);
    buffer[off++] = static_cast<uint8_t>(clampMinY_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((clampMinY_ >> 8) & 0xFF);
    buffer[off++] = static_cast<uint8_t>(clampMaxY_ & 0xFF);
    buffer[off++] = static_cast<uint8_t>((clampMaxY_ >> 8) & 0xFF);

    // Flags (4 bytes)
    buffer[off++] = (mouseButton_ ? 1 : 0) | (lastButton_ ? 2 : 0)
                  | (moved_ ? 4 : 0) | (buttonChanged_ ? 8 : 0);
    buffer[off++] = (irqActive_ ? 1 : 0) | (vblInterruptPending_ ? 2 : 0)
                  | (moveInterruptPending_ ? 4 : 0) | (buttonInterruptPending_ ? 8 : 0);
    buffer[off++] = lastCommand_;
    buffer[off++] = mode_;

    // Slot number (1 byte)
    buffer[off++] = slotNum_;

    // VBL tracking (1 byte)
    buffer[off++] = wasInVBL_ ? 1 : 0;

    // Protocol state (4 bytes: by6821B_, byState_, nBuffPos_, nDataLen_)
    buffer[off++] = by6821B_;
    buffer[off++] = byState_;
    buffer[off++] = nBuffPos_;
    buffer[off++] = nDataLen_;

    // Reserved (to reach STATE_SIZE=36)
    while (off < STATE_SIZE) {
        buffer[off++] = 0;
    }

    return off;
}

size_t MouseCard::deserialize(const uint8_t* buffer, size_t size) {
    if (size < STATE_SIZE) return 0;

    size_t off = 0;

    // PIA state
    ddra_ = buffer[off++];
    ddrb_ = buffer[off++];
    ora_ = buffer[off++];
    orb_ = buffer[off++];
    ira_ = buffer[off++];
    irb_ = buffer[off++];
    cra_ = buffer[off++];
    crb_ = buffer[off++];

    // Mouse position
    mouseX_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;
    mouseY_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;

    // Clamp bounds
    clampMinX_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;
    clampMaxX_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;
    clampMinY_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;
    clampMaxY_ = static_cast<int16_t>(buffer[off] | (buffer[off + 1] << 8));
    off += 2;

    // Flags
    uint8_t flags1 = buffer[off++];
    mouseButton_ = (flags1 & 1) != 0;
    lastButton_ = (flags1 & 2) != 0;
    moved_ = (flags1 & 4) != 0;
    buttonChanged_ = (flags1 & 8) != 0;

    uint8_t flags2 = buffer[off++];
    irqActive_ = (flags2 & 1) != 0;
    vblInterruptPending_ = (flags2 & 2) != 0;
    moveInterruptPending_ = (flags2 & 4) != 0;
    buttonInterruptPending_ = (flags2 & 8) != 0;

    lastCommand_ = buffer[off++];
    mode_ = buffer[off++];

    // Slot number
    slotNum_ = buffer[off++];

    // VBL tracking
    wasInVBL_ = buffer[off++] != 0;

    // Protocol state
    by6821B_ = buffer[off++];
    byState_ = buffer[off++];
    nBuffPos_ = buffer[off++];
    nDataLen_ = buffer[off++];

    // Restore snapshot from current position
    snapX_ = mouseX_;
    snapY_ = mouseY_;

    // Skip reserved
    off = STATE_SIZE;

    return off;
}

} // namespace a2e
