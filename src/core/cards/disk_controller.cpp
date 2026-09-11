/*
 * disk_controller.cpp - The 5.25" drive controller both machines carry
 *
 * Cycle-accurate Logic State Sequencer (LSS) driven by the P6 ROM (341-0028).
 * The sequencer clocks at 2x CPU rate (8 ticks per 4-cycle bit cell), with
 * disk read/write occurring at phase 4 of each 8-phase cycle.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "disk_controller.hpp"
#include "../disk-image/dsk_disk_image.hpp"
#include "../disk-image/woz_disk_image.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

// ===== P6 Sequencer ROM (341-0028, 16-sector) =====
//
// De-scrambled to BAPD (Beneath Apple ProDOS) logical format.
// Source ROM CRC32: b72a2c70
//
// Address: (state << 4) | (Q7 << 3) | (Q6 << 2) | (QA << 1) | pulse
//   state  = 4-bit sequencer state (0-F)
//   Q7/Q6  = mode select (00=read, 01=WP sense, 10=write, 11=load)
//   QA     = data register bit 7 (MSB feedback)
//   pulse  = read bit from disk (1=flux transition)
//
// Data byte: high nibble = next state, low nibble = action code
//   Actions: 0-7=CLR, 8/C=NOP, 9=SL0, A/E=SR+WP, B/F=LOAD, D=SL1
//
const uint8_t DiskController::P6_ROM[256] = {
    // State 0                                          State 1
    0x18,0x18,0x18,0x18,0x0A,0x0A,0x0A,0x0A,  0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
    0x2D,0x2D,0x38,0x38,0x0A,0x0A,0x0A,0x0A,  0x28,0x28,0x28,0x28,0x28,0x28,0x28,0x28,
    // State 2                                          State 3
    0xD8,0x38,0x08,0x28,0x0A,0x0A,0x0A,0x0A,  0x39,0x39,0x39,0x39,0x3B,0x3B,0x3B,0x3B,
    0xD8,0x48,0x48,0x48,0x0A,0x0A,0x0A,0x0A,  0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,
    // State 4                                          State 5
    0xD8,0x58,0xD8,0x58,0x0A,0x0A,0x0A,0x0A,  0x58,0x58,0x58,0x58,0x58,0x58,0x58,0x58,
    0xD8,0x68,0xD8,0x68,0x0A,0x0A,0x0A,0x0A,  0x68,0x68,0x68,0x68,0x68,0x68,0x68,0x68,
    // State 6                                          State 7
    0xD8,0x78,0xD8,0x78,0x0A,0x0A,0x0A,0x0A,  0x78,0x78,0x78,0x78,0x78,0x78,0x78,0x78,
    0xD8,0x88,0xD8,0x88,0x0A,0x0A,0x0A,0x0A,  0x08,0x08,0x88,0x88,0x08,0x08,0x88,0x88,
    // State 8                                          State 9
    0xD8,0x98,0xD8,0x98,0x0A,0x0A,0x0A,0x0A,  0x98,0x98,0x98,0x98,0x98,0x98,0x98,0x98,
    0xD8,0x29,0xD8,0xA8,0x0A,0x0A,0x0A,0x0A,  0xA8,0xA8,0xA8,0xA8,0xA8,0xA8,0xA8,0xA8,
    // State A                                          State B
    0xCD,0xBD,0xD8,0xB8,0x0A,0x0A,0x0A,0x0A,  0xB9,0xB9,0xB9,0xB9,0xBB,0xBB,0xBB,0xBB,
    0xD9,0x59,0xD8,0xC8,0x0A,0x0A,0x0A,0x0A,  0xC8,0xC8,0xC8,0xC8,0xC8,0xC8,0xC8,0xC8,
    // State C                                          State D
    0xD9,0xD9,0xD8,0xA0,0x0A,0x0A,0x0A,0x0A,  0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,
    0xD8,0x08,0xE8,0xE8,0x0A,0x0A,0x0A,0x0A,  0xE8,0xE8,0xE8,0xE8,0xE8,0xE8,0xE8,0xE8,
    // State E                                          State F
    0xFD,0xFD,0xF8,0xF8,0x0A,0x0A,0x0A,0x0A,  0xF8,0xF8,0xF8,0xF8,0xF8,0xF8,0xF8,0xF8,
    0xDD,0x4D,0xE0,0xE0,0x0A,0x0A,0x0A,0x0A,  0x88,0x88,0x08,0x08,0x88,0x88,0x08,0x08,
};

DiskController::DiskController() {
    reset();
}

// ===== ExpansionCard Interface =====

uint8_t DiskController::readIO(uint8_t offset) {
    return handleSoftSwitch(offset & 0x0F, false);
}

void DiskController::writeIO(uint8_t offset, uint8_t value) {
    handleSoftSwitch(offset & 0x0F, true);
    busData_ = value;
}

uint8_t DiskController::peekIO(uint8_t offset) const {
    uint8_t off = offset & 0x0F;

    switch (off) {
    case PHASE0_OFF:
    case PHASE0_ON:
    case PHASE1_OFF:
    case PHASE1_ON:
    case PHASE2_OFF:
    case PHASE2_ON:
    case PHASE3_OFF:
    case PHASE3_ON:
        return (phaseStates_ >> (off / 2)) & 1 ? 0x80 : 0x00;

    case MOTOR_OFF:
    case MOTOR_ON:
        return isMotorOn() ? 0x80 : 0x00;

    case DRIVE1_SELECT:
    case DRIVE2_SELECT:
        return selectedDrive_ == 1 ? 0x80 : 0x00;

    case Q6L:
        return dataRegister_;

    case Q6H:
        if (hasDisk(selectedDrive_)) {
            const DiskImage* disk = diskImages_[selectedDrive_].get();
            return disk->isWriteProtected() ? 0x80 : 0x00;
        }
        return 0x00;

    case Q7L:
        return q7_ ? 0x00 : 0x80;

    case Q7H:
        return q7_ ? 0x80 : 0x00;

    default:
        return 0x00;
    }
}

void DiskController::reset() {
    motorOn_ = false;
    motorOffCycle_ = 0;
    selectedDrive_ = 0;
    q6_ = false;
    q7_ = false;
    phaseStates_ = 0;

    totalCycles_ = 0;
    sequencerState_ = 0;
    dataRegister_ = 0;
    lastLSSCycle_ = 0;
    busData_ = 0;
    lssClock_ = 0;
    writeLevel_ = 0;

    // Reset disk image positioning state (but preserve loaded disks)
    for (int i = 0; i < 2; i++) {
        if (diskImages_[i]) {
            diskImages_[i]->resetState();
        }
    }
}

void DiskController::update(int cycles) {
    totalCycles_ += cycles;
}

void DiskController::setCycleCallback(CycleCallback callback) {
    cycleCallback_ = std::move(callback);
}

size_t DiskController::getStateSize() const {
    return 32;
}

size_t DiskController::serialize(uint8_t* buffer, size_t maxSize) const {
    if (!buffer || maxSize < 16) return 0;

    size_t offset = 0;

    buffer[offset++] = isMotorOn() ? 1 : 0;
    buffer[offset++] = static_cast<uint8_t>(selectedDrive_);
    buffer[offset++] = q6_ ? 1 : 0;
    buffer[offset++] = q7_ ? 1 : 0;
    buffer[offset++] = phaseStates_;
    buffer[offset++] = dataRegister_;

    // Track positions for both drives
    int track0 = 0, track1 = 0;
    if (hasDisk(0)) {
        const DiskImage* img = getDiskImage(0);
        if (img) track0 = img->getQuarterTrack();
    }
    if (hasDisk(1)) {
        const DiskImage* img = getDiskImage(1);
        if (img) track1 = img->getQuarterTrack();
    }
    buffer[offset++] = static_cast<uint8_t>(track0);
    buffer[offset++] = static_cast<uint8_t>(track1);

    // LSS state (new in state version 7)
    buffer[offset++] = sequencerState_;
    buffer[offset++] = busData_;
    buffer[offset++] = lssClock_;
    buffer[offset++] = writeLevel_;

    return offset;
}

size_t DiskController::deserialize(const uint8_t* buffer, size_t size) {
    if (!buffer || size < 8) return 0;

    size_t offset = 0;

    setMotorOn(buffer[offset++] != 0);
    setSelectedDrive(buffer[offset++]);
    setQ6(buffer[offset++] != 0);
    setQ7(buffer[offset++] != 0);
    setPhaseStates(buffer[offset++]);
    setDataLatch(buffer[offset++]);

    int track0 = buffer[offset++];
    int track1 = buffer[offset++];

    if (hasDisk(0)) {
        DiskImage* img = getMutableDiskImage(0);
        if (img) img->setQuarterTrack(track0);
    }
    if (hasDisk(1)) {
        DiskImage* img = getMutableDiskImage(1);
        if (img) img->setQuarterTrack(track1);
    }

    // LSS state (new in state version 7)
    if (offset + 3 <= size) {
        setSequencerState(buffer[offset++]);
        setBusData(buffer[offset++]);
        setLSSClock(buffer[offset++]);
    }
    if (offset < size) {
        writeLevel_ = buffer[offset++] & 0x01;
    }

    return offset;
}

// ===== Disk Operations =====

bool DiskController::insertDisk(int drive, const uint8_t* data, size_t size,
                           const std::string& filename) {
    if (drive < 0 || drive > 1) {
        return false;
    }

    // Determine file type from extension
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(),
                   lowerFilename.begin(), ::tolower);

    std::unique_ptr<DiskImage> image;

    if (lowerFilename.find(".woz") != std::string::npos) {
        image = std::make_unique<WozDiskImage>();
    } else if (lowerFilename.find(".dsk") != std::string::npos ||
               lowerFilename.find(".do") != std::string::npos ||
               lowerFilename.find(".po") != std::string::npos) {
        image = std::make_unique<DskDiskImage>();
    } else {
        // Try to detect format from content
        if (size >= 12 && data[0] == 'W' && data[1] == 'O' && data[2] == 'Z') {
            image = std::make_unique<WozDiskImage>();
        } else if (size == 143360) {
            image = std::make_unique<DskDiskImage>();
        } else {
            return false;
        }
    }

    if (!image->load(data, size, filename)) {
        return false;
    }

    diskImages_[drive] = std::move(image);

    // Reset LSS timing for this drive
    if (drive == selectedDrive_) {
        lastLSSCycle_ = getCycles();
    }

    return true;
}

bool DiskController::insertBlankDisk(int drive) {
    if (drive < 0 || drive > 1) {
        return false;
    }

    auto image = std::make_unique<WozDiskImage>();
    image->createBlank();

    diskImages_[drive] = std::move(image);
    return true;
}

void DiskController::ejectDisk(int drive) {
    if (drive < 0 || drive > 1) {
        return;
    }

    if (selectedDrive_ == drive) {
        motorOn_ = false;
        motorOffCycle_ = 0;
    }

    diskImages_[drive].reset();
}

bool DiskController::hasDisk(int drive) const {
    if (drive < 0 || drive > 1) {
        return false;
    }
    return diskImages_[drive] != nullptr && diskImages_[drive]->isLoaded();
}

const uint8_t* DiskController::getDiskData(int drive, size_t* size) const {
    if (drive < 0 || drive > 1 || !hasDisk(drive)) {
        *size = 0;
        return nullptr;
    }
    return diskImages_[drive]->getSectorData(size);
}

const uint8_t* DiskController::exportDiskData(int drive, size_t* size) {
    if (drive < 0 || drive > 1 || !hasDisk(drive)) {
        *size = 0;
        return nullptr;
    }
    return diskImages_[drive]->exportData(size);
}

const DiskImage* DiskController::getDiskImage(int drive) const {
    if (drive < 0 || drive > 1) {
        return nullptr;
    }
    return diskImages_[drive].get();
}

DiskImage* DiskController::getMutableDiskImage(int drive) {
    if (drive < 0 || drive > 1) {
        return nullptr;
    }
    return diskImages_[drive].get();
}

bool DiskController::isMotorOn() const {
    if (motorOn_ && motorOffCycle_ != 0) {
        if (getCycles() >= motorOffCycle_ + MOTOR_OFF_DELAY_CYCLES) {
            motorOn_ = false;
            motorOffCycle_ = 0;
        }
    }
    return motorOn_;
}

void DiskController::stopMotor() {
    motorOn_ = false;
    motorOffCycle_ = 0;
}

int DiskController::getCurrentTrack() const {
    if (hasDisk(selectedDrive_)) {
        return diskImages_[selectedDrive_]->getTrack();
    }
    return -1;
}

int DiskController::getQuarterTrack() const {
    if (hasDisk(selectedDrive_)) {
        return diskImages_[selectedDrive_]->getQuarterTrack();
    }
    return -1;
}

// ===== Logic State Sequencer =====

void DiskController::clockLSS() {
    DiskImage* disk = diskImages_[selectedDrive_].get();
    if (!disk) {
        if (++lssClock_ > 7) lssClock_ = 0;
        return;
    }

    // Whether the head is currently over a track that has recorded data. When
    // it is not (unformatted / empty track, e.g. a protection check that seeks
    // the head to the inner stop past the last data track), we still clock the
    // sequencer but feed it random weak bits so read loops see noise instead of
    // a frozen data register.
    const bool trackHasData = disk->hasData();

    // Read pulse from disk only at phase 4 of the 8-phase clock.
    // On all other phases, pulse is 0 (inverted -> 1 in address).
    // When Q7=1 (write mode), pulse never affects P6 ROM output,
    // so we skip the read and let writeBit handle head advance.
    uint8_t readPulse = 0;
    if (lssClock_ == 4 && !q7_) {
        readPulse = trackHasData ? disk->readBit()  // reads and advances head
                                 : nextWeakBit();    // empty track -> noise
    }

    // P6 ROM lookup (every tick, BAPD format with inverted pulse)
    uint8_t qa = (dataRegister_ >> 7) & 1;
    uint8_t addr = (sequencerState_ << 4) | (uint8_t(q7_) << 3) |
                   (uint8_t(q6_) << 2) | (qa << 1) | (readPulse ? 0x00 : 0x01);
    uint8_t opcode = P6_ROM[addr];
    uint8_t nextState = (opcode >> 4) & 0x0F;

    // Execute data register action (MAME-derived P6 ROM action decoding)
    switch (opcode & 0x0F) {
    case 0: case 1: case 2: case 3:
    case 4: case 5: case 6: case 7:
        dataRegister_ = 0x00;           // CLR
        break;
    case 0x8: case 0xC:
        break;                           // NOP
    case 0x9:
        dataRegister_ <<= 1;            // SL0 (shift left, 0 in)
        break;
    case 0xA: case 0xE:
        dataRegister_ = (dataRegister_ >> 1) |
            (disk->isWriteProtected() ? 0x80 : 0x00);  // SR + WP sense
        break;
    case 0xB: case 0xF:
        dataRegister_ = busData_;        // LOAD from CPU bus
        break;
    case 0xD:
        dataRegister_ = (dataRegister_ << 1) | 0x01;  // SL1 (shift left, 1 in)
        break;
    }

    // Write mode: output bit and advance head at phase 4.
    // The P6 ROM state bit 3 is the write amplifier LEVEL (magnetic polarity).
    // Disk formats (WOZ/DSK) store flux TRANSITIONS (1 = polarity change).
    // Convert level to transition via XOR with previous level.
    //
    // ENABLE, not the motor, is what gates the write head: the drive is still
    // turning for a second after the CPU switches it off, and nothing is laid
    // down on the disk during it. A //e never notices the difference, because
    // its firmware raises Q7 only when it means to write. A IIgs does: its
    // firmware switches the drive off and immediately writes the IWM's mode
    // register, which is an access to $C0EF and so raises Q7 as a side effect,
    // and without this the machine erases the disk it was about to boot.
    if (lssClock_ == 4 && q7_ && isDriveEnabled()) {
        uint8_t level = (nextState >> 3) & 1;
        disk->writeBit(level ^ writeLevel_);
        writeLevel_ = level;
    }

    sequencerState_ = nextState;
    if (++lssClock_ > 7) lssClock_ = 0;
}

void DiskController::catchUpLSS(uint64_t currentCycle) {
    if (!isMotorOn() || !hasDisk(selectedDrive_)) return;
    if (currentCycle <= lastLSSCycle_) {
        lastLSSCycle_ = currentCycle;
        return;
    }

    // LSS runs at 2x CPU rate (2 ticks per CPU cycle, 8 ticks per bit cell)
    uint64_t elapsedCycles = currentCycle - lastLSSCycle_;
    uint64_t ticks = elapsedCycles * 2;

    // Cap to approximately one disk revolution
    static constexpr uint64_t MAX_CATCHUP_TICKS =
        static_cast<uint64_t>(MAX_CATCHUP_BITS) * 8;
    if (ticks > MAX_CATCHUP_TICKS) ticks = MAX_CATCHUP_TICKS;

    for (uint64_t i = 0; i < ticks; i++) {
        clockLSS();
    }

    lastLSSCycle_ = currentCycle;
}

// ===== Soft Switch Handler =====

uint8_t DiskController::handleSoftSwitch(uint8_t offset, bool isWrite) {
    uint64_t currentCycle = getCycles();

    auto setPhase = [this, currentCycle](int phase, bool on) {
        catchUpLSS(currentCycle);
        if (hasDisk(selectedDrive_)) {
            diskImages_[selectedDrive_]->setPhase(phase, on);
        }
    };

    switch (offset) {
    case PHASE0_OFF:
        phaseStates_ &= ~0x01;
        setPhase(0, false);
        break;
    case PHASE0_ON:
        phaseStates_ |= 0x01;
        setPhase(0, true);
        break;
    case PHASE1_OFF:
        phaseStates_ &= ~0x02;
        setPhase(1, false);
        break;
    case PHASE1_ON:
        phaseStates_ |= 0x02;
        setPhase(1, true);
        break;
    case PHASE2_OFF:
        phaseStates_ &= ~0x04;
        setPhase(2, false);
        break;
    case PHASE2_ON:
        phaseStates_ |= 0x04;
        setPhase(2, true);
        break;
    case PHASE3_OFF:
        phaseStates_ &= ~0x08;
        setPhase(3, false);
        break;
    case PHASE3_ON:
        phaseStates_ |= 0x08;
        setPhase(3, true);
        break;

    case MOTOR_OFF:
        catchUpLSS(currentCycle);
        if (motorOn_ && motorOffCycle_ == 0) {
            motorOffCycle_ = currentCycle;
        }
        return dataRegister_;

    case MOTOR_ON:
        motorOffCycle_ = 0;
        if (!motorOn_) {
            lastLSSCycle_ = currentCycle;
        }
        motorOn_ = true;
        break;

    case DRIVE1_SELECT:
        catchUpLSS(currentCycle);
        selectedDrive_ = 0;
        break;
    case DRIVE2_SELECT:
        catchUpLSS(currentCycle);
        selectedDrive_ = 1;
        break;

    case Q6L:
        catchUpLSS(currentCycle);
        q6_ = false;
        return dataRegister_;

    case Q6H:
        catchUpLSS(currentCycle);
        q6_ = true;
        if (!q7_) {
            // Sense write protect
            if (hasDisk(selectedDrive_)) {
                return diskImages_[selectedDrive_]->isWriteProtected() ? 0x80 : 0x00;
            }
            return 0x00;
        }
        break;

    case Q7L:
        catchUpLSS(currentCycle);
        q7_ = false;
        return dataRegister_;

    case Q7H:
        catchUpLSS(currentCycle);
        q7_ = true;
        break;
    }

    return 0x00;
}

} // namespace a2e
