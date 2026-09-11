/*
 * disk_controller.hpp - The 5.25" drive controller both machines carry
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "expansion_card.hpp"
#include "../disk-image/disk_image.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace a2e {

/**
 * DiskController - the 5.25" drive mechanism, its stepper and its sequencer
 *
 * Two machines in this emulator drive a 5.25" disk and they do it with
 * different parts: a //e or a II+ has a Disk II controller card in a slot, and
 * a //c has an IWM soldered to the board. What is between the chip and the
 * disk is the same in both — two drives, four stepper phases, a motor with a
 * one-second run-on, and Woz's Logic State Sequencer clocked from the P6 ROM
 * at twice the CPU rate — and the IWM was built to be that controller in one
 * package, answering the same sixteen addresses with the same meanings.
 *
 * So the shared part lives here, and a subclass is only what the machine plugs
 * into it: the Disk II card adds the P5A boot ROM in slot 6's ROM space; the
 * IWM has no ROM at all, because a //c's disk firmware is in the system ROM,
 * and puts its own register file in front of the switches.
 *
 * The sixteen soft switches, as a slot decodes them:
 * $00 - Phase 0 off     $01 - Phase 0 on
 * $02 - Phase 1 off     $03 - Phase 1 on
 * $04 - Phase 2 off     $05 - Phase 2 on
 * $06 - Phase 3 off     $07 - Phase 3 on
 * $08 - Motor off       $09 - Motor on
 * $0A - Drive 1 select  $0B - Drive 2 select
 * $0C - Q6L (read)      $0D - Q6H (WP sense/write load)
 * $0E - Q7L (read mode) $0F - Q7H (write mode)
 */
class DiskController : public ExpansionCard {
public:
    DiskController();
    ~DiskController() override = default;

    // Delete copy (disk images are non-copyable)
    DiskController(const DiskController&) = delete;
    DiskController& operator=(const DiskController&) = delete;

    // Allow move
    DiskController(DiskController&&) = default;
    DiskController& operator=(DiskController&&) = default;

    // ===== ExpansionCard Interface =====

    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    bool hasExpansionROM() const override { return false; }

    void reset() override;
    void update(int cycles) override;

    void setCycleCallback(CycleCallback callback) override;

    size_t getStateSize() const override;
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    uint8_t getPreferredSlot() const override { return 6; }

    // ===== Disk Operations =====

    /**
     * Insert a disk image into a drive
     * @param drive Drive number (0 or 1)
     * @param data Pointer to disk image data
     * @param size Size of the data
     * @param filename Original filename (for format detection)
     * @return true on success
     */
    bool insertDisk(int drive, const uint8_t* data, size_t size, const std::string& filename);

    /**
     * Insert a blank, unformatted disk into a drive
     * @param drive Drive number (0 or 1)
     * @return true on success
     */
    bool insertBlankDisk(int drive);

    /**
     * Eject disk from a drive
     * @param drive Drive number (0 or 1)
     */
    void ejectDisk(int drive);

    /**
     * Check if a drive has a disk inserted
     * @param drive Drive number (0 or 1)
     * @return true if disk is inserted
     */
    bool hasDisk(int drive) const;

    /**
     * Get the disk data for saving (DSK format)
     * @param drive Drive number (0 or 1)
     * @param size Output: size of the data
     * @return Pointer to disk data, or nullptr if no disk
     */
    const uint8_t* getDiskData(int drive, size_t* size) const;

    /**
     * Export disk data in its native format for saving
     * This works for both DSK and WOZ formats.
     * @param drive Drive number (0 or 1)
     * @param size Output: size of the exported data
     * @return Pointer to exported data, or nullptr if no disk
     */
    const uint8_t* exportDiskData(int drive, size_t* size);

    /**
     * Get the disk image for a drive (for UI display)
     * @param drive Drive number (0 or 1)
     * @return Pointer to disk image, or nullptr if no disk
     */
    const DiskImage* getDiskImage(int drive) const;

    /**
     * Get mutable disk image for a drive (for state restoration)
     * @param drive Drive number (0 or 1)
     * @return Pointer to disk image, or nullptr if no disk
     */
    DiskImage* getMutableDiskImage(int drive);

    /**
     * Check if motor is currently on
     * @return true if motor is running
     */
    bool isMotorOn() const;

    /**
     * The drive ENABLE line, as the CPU last set it.
     *
     * This is not `isMotorOn()`. That one answers "is the disk still turning",
     * and it stays true for about a second after the CPU switches the drive
     * off, because a motor takes that long to stop — which is the whole point
     * of it, and why a read a moment after $C0E8 still finds data. ENABLE is
     * the wire, and it goes low the instant the CPU touches that address.
     *
     * The difference matters wherever the electronics, rather than the
     * mechanism, is what is being asked about: nothing is written to a disk
     * whose drive is not enabled, however long the platter takes to stop, and
     * an IWM's mode register is writable exactly while ENABLE is low.
     */
    bool isDriveEnabled() const { return motorOn_ && motorOffCycle_ == 0; }

    /**
     * Stop the motor immediately (for warm reset)
     * Does not reset other controller state like track position
     */
    void stopMotor();

    /**
     * Get currently selected drive (0 or 1)
     * @return Selected drive number
     */
    int getSelectedDrive() const { return selectedDrive_; }

    /**
     * Get the current track position from the selected drive's disk image
     * @return Track number (0-34), or -1 if no disk
     */
    int getCurrentTrack() const;

    /**
     * Get the current quarter-track position from the selected drive's disk image
     * @return Quarter-track number (0-139), or -1 if no disk
     */
    int getQuarterTrack() const;

    /**
     * Get the current phase magnet states
     * @return Bit field where bit 0-3 represent phases 0-3 (1 = on, 0 = off)
     */
    uint8_t getPhaseStates() const { return phaseStates_; }

    /**
     * Get Q6 latch state
     * @return true if Q6 is high, false if low
     */
    bool getQ6() const { return q6_; }

    /**
     * Get Q7 latch state
     * @return true if Q7 is high (write mode), false if low (read mode)
     */
    bool getQ7() const { return q7_; }

    /**
     * Get the data register value (LSS shift register)
     * @return Current data register value
     */
    uint8_t getDataLatch() const { return dataRegister_; }

    /**
     * Get the sequencer state (4-bit, 0-15)
     */
    uint8_t getSequencerState() const { return sequencerState_; }

    /**
     * Get the bus data value (last value written by CPU)
     */
    uint8_t getBusData() const { return busData_; }

    // ===== State Restoration Methods =====

    void setSelectedDrive(int drive) { selectedDrive_ = (drive == 0) ? 0 : 1; }
    void setQ6(bool q6) { q6_ = q6; }
    void setQ7(bool q7) { q7_ = q7; }
    void setPhaseStates(uint8_t states) { phaseStates_ = states; }
    void setDataLatch(uint8_t latch) { dataRegister_ = latch; }
    void setMotorOn(bool on) { motorOn_ = on; }
    void setSequencerState(uint8_t s) { sequencerState_ = s & 0x0F; }
    void setBusData(uint8_t d) { busData_ = d; }
    uint8_t getLSSClock() const { return lssClock_; }
    void setLSSClock(uint8_t c) { lssClock_ = c & 0x07; }

protected:
    // Soft switch offsets
    static constexpr uint8_t PHASE0_OFF = 0x00;
    static constexpr uint8_t PHASE0_ON = 0x01;
    static constexpr uint8_t PHASE1_OFF = 0x02;
    static constexpr uint8_t PHASE1_ON = 0x03;
    static constexpr uint8_t PHASE2_OFF = 0x04;
    static constexpr uint8_t PHASE2_ON = 0x05;
    static constexpr uint8_t PHASE3_OFF = 0x06;
    static constexpr uint8_t PHASE3_ON = 0x07;
    static constexpr uint8_t MOTOR_OFF = 0x08;
    static constexpr uint8_t MOTOR_ON = 0x09;
    static constexpr uint8_t DRIVE1_SELECT = 0x0A;
    static constexpr uint8_t DRIVE2_SELECT = 0x0B;
    static constexpr uint8_t Q6L = 0x0C;
    static constexpr uint8_t Q6H = 0x0D;
    static constexpr uint8_t Q7L = 0x0E;
    static constexpr uint8_t Q7H = 0x0F;

    // Motor timeout: ~1 second at 1.023 MHz
    static constexpr uint64_t MOTOR_OFF_DELAY_CYCLES = 1023000;

    // LSS timing: 4 CPU cycles per bit cell
    static constexpr int CYCLES_PER_BIT = 4;

    // Maximum catch-up: ~one disk revolution (~51200 bits for standard track)
    static constexpr uint32_t MAX_CATCHUP_BITS = 53000;

    // P6 sequencer ROM (341-0028, 256x4 bits, de-scrambled to logical format)
    static const uint8_t P6_ROM[256];

    // Controller state
    mutable bool motorOn_ = false;
    mutable uint64_t motorOffCycle_ = 0;
    int selectedDrive_ = 0;
    bool q6_ = false;
    bool q7_ = false;
    uint8_t phaseStates_ = 0;

    // Timing state
    uint64_t totalCycles_ = 0;

    // LSS state (P6 ROM clocked at 2x CPU rate = 8 ticks per bit cell)
    uint8_t sequencerState_ = 0;    // 4-bit state (0-15)
    uint8_t dataRegister_ = 0;      // 8-bit shift register
    uint64_t lastLSSCycle_ = 0;     // Last cycle LSS was clocked
    uint8_t busData_ = 0;           // CPU bus data for LOAD operations
    uint8_t lssClock_ = 0;         // 8-phase clock (0-7), disk I/O at phase 4
    uint8_t writeLevel_ = 0;       // Previous write amplifier level for transition encoding
    uint32_t weakBitLfsr_ = 0x2545F4914F6CDD1Du & 0xFFFFFFFFu; // noise PRNG state (non-zero)

    // Disk images for each drive
    std::unique_ptr<DiskImage> diskImages_[2];

    // Cycle callback
    CycleCallback cycleCallback_;

    /**
     * Get current cycle count, using callback if available
     */
    uint64_t getCycles() const {
        if (cycleCallback_) {
            return cycleCallback_();
        }
        return totalCycles_;
    }

    /**
     * Generate a random "weak bit" for reading an unformatted/empty track.
     * Real drives read random flux noise from the MC3470 when the head is over
     * a track with no recorded data; a 1 appears with ~30% probability, which
     * yields plausible nibbles rather than a hung read loop.
     */
    uint8_t nextWeakBit() {
        // xorshift32
        weakBitLfsr_ ^= weakBitLfsr_ << 13;
        weakBitLfsr_ ^= weakBitLfsr_ >> 17;
        weakBitLfsr_ ^= weakBitLfsr_ << 5;
        return (weakBitLfsr_ < 0x4CCCCCCCu) ? 1 : 0; // ~30% ones
    }

    /**
     * Handle soft switch access
     * @param offset Offset (0x00-0x0F)
     * @param isWrite true if write access, false if read
     * @return byte value for reads
     */
    uint8_t handleSoftSwitch(uint8_t offset, bool isWrite);

    /**
     * Clock the Logic State Sequencer by one tick (runs at 2x CPU rate).
     * 8 ticks = 1 bit cell. Disk read/write occurs at phase 4 only.
     */
    void clockLSS();

    /**
     * Advance the LSS to the current CPU cycle
     * @param currentCycle Current total CPU cycle count
     */
    void catchUpLSS(uint64_t currentCycle);
};

} // namespace a2e
