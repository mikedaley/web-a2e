/*
 * disk2_card.hpp - Disk II controller card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../disk_controller.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace a2e {

/**
 * Disk2Card - Disk II Controller Card
 *
 * The controller card a //e or a II+ takes in slot 6. Everything between the
 * card and the disk — the drives, the stepper, the motor and the sequencer —
 * is DiskController; what the card adds is the part in a socket on it: the
 * 256-byte P5A boot ROM (341-0027) that answers at $C600-$C6FF and is what
 * makes a slot bootable.
 *
 * The card does not use expansion ROM ($C800-$CFFF).
 */
class Disk2Card : public DiskController {
public:
    Disk2Card();
    Disk2Card(const uint8_t* rom, size_t romSize);
    ~Disk2Card() override = default;

    Disk2Card(const Disk2Card&) = delete;
    Disk2Card& operator=(const Disk2Card&) = delete;

    uint8_t readROM(uint8_t offset) override;
    bool hasROM() const override { return true; }

    const char* getName() const override { return "Disk II"; }

    /**
     * Load the P5A ROM (341-0027)
     * @param rom ROM data
     * @param size ROM size (should be 256 bytes)
     */
    void loadROM(const uint8_t* rom, size_t size);

private:
    std::array<uint8_t, 256> rom_;
};

} // namespace a2e
