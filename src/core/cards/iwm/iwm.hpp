/*
 * iwm.hpp - The Integrated Woz Machine, a //c's disk controller
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../disk_controller.hpp"
#include <cstdint>

namespace a2e {

/**
 * IWM - Integrated Woz Machine (344-0041)
 *
 * A //c has no slot 6 to put a Disk II card in; it has this chip soldered to
 * the board, its internal drive on one of the two drive connectors and the
 * external 5.25" port on the other. The chip is the Disk II controller in one
 * package, and it decodes slot 6's sixteen addresses — $C0E0-$C0EF — with the
 * same meanings, which is why the //c's firmware is recognisably the same disk
 * code and why the drives and the sequencer are DiskController's, shared with
 * the card.
 *
 * What it adds is a register file. On a Disk II every read returns the data
 * latch; on an IWM a read returns whichever of four registers the Q7/Q6 pair
 * selects:
 *
 *   Q7 Q6
 *    0  0   data register — the sequencer's shift register, as the card's
 *    0  1   status: SENSE in bit 7, the motor in bit 5, the mode in bits 4-0
 *    1  0   handshake: write-data ready in bit 7, underrun in bit 6
 *    1  1   write: a write loads the data register, or the mode register when
 *           the motor is off
 *
 * The mode register is where a //c's firmware asks for the timings it wants —
 * the clock, the bit cell, whether the handshake is latched. Nothing in this
 * emulation runs off it: the sequencer is clocked from the P6 ROM at the one
 * rate a 5.25" drive uses, which is the mode a //c selects and the only mode
 * its drives can be read in. It is stored and read back because the firmware
 * writes it and expects to see it again in the status register, and a chip
 * that forgot it would look broken to the code that checks.
 *
 * There is no ROM. A card's boot ROM lives in its slot's 256 bytes; a //c's
 * disk firmware is part of the 16KB system ROM, which is also why $C600 boots
 * a //c at all with nothing in a socket.
 */
class IWM : public DiskController {
public:
    IWM() = default;
    ~IWM() override = default;

    IWM(const IWM&) = delete;
    IWM& operator=(const IWM&) = delete;

    // ===== ExpansionCard Interface =====

    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    // Nothing answers in slot 6's ROM space: the firmware is in the system ROM.
    uint8_t readROM(uint8_t /*offset*/) override { return 0xFF; }
    bool hasROM() const override { return false; }

    void reset() override;

    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    const char* getName() const override { return "IWM"; }

    // ===== IWM Specific =====

    /** The mode register as the firmware last wrote it (bits 4-0). */
    uint8_t getModeRegister() const { return mode_; }

    /** The status register: SENSE, the motor, and the mode. */
    uint8_t readStatus() const;

    /** The handshake register: write-data ready, and the underrun flag. */
    uint8_t readHandshake() const;

private:
    // Which register a read sees, from the Q7/Q6 pair.
    enum class Register { Data, Status, Handshake, Write };
    Register selectedRegister() const;

    uint8_t mode_ = 0; // Mode register, bits 4-0
};

} // namespace a2e
