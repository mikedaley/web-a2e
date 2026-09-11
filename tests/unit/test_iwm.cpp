/*
 * test_iwm.cpp - Unit tests for the IWM, a //c's disk controller
 *
 * The chip is the Disk II controller in one package, so what is tested here is
 * the pair of claims that follow from that: the parts that are the same really
 * are the same code — the sequencer, the stepper and the drives, which is
 * DiskController — and the part that differs is the register file a read sees
 * in front of them.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "iwm.hpp"
#include "disk2_card.hpp"
#include "roms.cpp"

#include <string>
#include <vector>

using namespace a2e;

namespace {

// Soft switch offsets, as the machine decodes $C0E0-$C0EF.
constexpr uint8_t MOTOR_OFF = 0x08;
constexpr uint8_t MOTOR_ON = 0x09;
constexpr uint8_t DRIVE2 = 0x0B;
constexpr uint8_t Q6L = 0x0C;
constexpr uint8_t Q6H = 0x0D;
constexpr uint8_t Q7L = 0x0E;
constexpr uint8_t Q7H = 0x0F;

// A formatted 143KB image: the sector data is empty, but the address fields
// and their prologues are real, because a DSK image is GCR encoded on load.
std::vector<uint8_t> blankDsk() { return std::vector<uint8_t>(143360, 0x00); }

// Spin the drive and read the data register, one bit cell at a time, the way
// firmware does. Returns the nibbles that came back.
std::vector<uint8_t> readNibbles(DiskController &controller, int count) {
    controller.readIO(MOTOR_ON);
    std::vector<uint8_t> nibbles;
    uint8_t last = 0;
    for (int i = 0; i < 400000 && static_cast<int>(nibbles.size()) < count; i++) {
        controller.update(4); // one bit cell
        const uint8_t value = controller.readIO(Q6L);
        // A nibble is complete when bit 7 arrives, and stays in the register
        // until the sequencer starts assembling the next one.
        if ((value & 0x80) && value != last) nibbles.push_back(value);
        last = value;
    }
    return nibbles;
}

} // namespace

// ---------------------------------------------------------------------------
// What the machine has, rather than what is in a slot
// ---------------------------------------------------------------------------

TEST_CASE("An IWM is a drive controller with no ROM", "[iwm]") {
    IWM iwm;

    REQUIRE(std::string(iwm.getName()) == "IWM");
    REQUIRE(iwm.getPreferredSlot() == 6);

    // This is the difference that matters to the MMU: a card's boot ROM
    // answers in its slot's 256 bytes, and a //c's disk firmware is part of
    // the system ROM instead, so there is nothing here to read.
    REQUIRE_FALSE(iwm.hasROM());
    REQUIRE_FALSE(iwm.hasExpansionROM());
}

// ---------------------------------------------------------------------------
// The shared sequencer
// ---------------------------------------------------------------------------

TEST_CASE("An IWM reads the same nibbles as the card", "[iwm][disk]") {
    // The point of the base class, stated as behaviour: two controllers, the
    // same image, the same accesses, the same bit stream off the disk. If the
    // IWM's register file ever got in front of the data register, this is
    // where it would show.
    auto image = blankDsk();

    IWM iwm;
    REQUIRE(iwm.insertDisk(0, image.data(), image.size(), "blank.dsk"));

    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE(card.insertDisk(0, image.data(), image.size(), "blank.dsk"));

    // Enough to clear the sync gap at the start of the track and reach a
    // sector's address field.
    const auto fromIwm = readNibbles(iwm, 400);
    const auto fromCard = readNibbles(card, 400);

    REQUIRE(fromIwm.size() == 400);
    REQUIRE(fromIwm == fromCard);

    SECTION("and they are real nibbles off a formatted track") {
        // Every nibble has bit 7 set, and the address field prologue that
        // starts every sector is in there.
        for (uint8_t nibble : fromIwm) REQUIRE((nibble & 0x80) != 0);

        bool foundPrologue = false;
        for (size_t i = 0; i + 2 < fromIwm.size(); i++) {
            if (fromIwm[i] == 0xD5 && fromIwm[i + 1] == 0xAA &&
                fromIwm[i + 2] == 0x96) {
                foundPrologue = true;
            }
        }
        REQUIRE(foundPrologue);
    }
}

TEST_CASE("An IWM steps and selects drives like the card", "[iwm][disk]") {
    IWM iwm;
    auto image = blankDsk();
    REQUIRE(iwm.insertDisk(1, image.data(), image.size(), "blank.dsk"));

    iwm.readIO(DRIVE2);
    REQUIRE(iwm.getSelectedDrive() == 1);
    REQUIRE(iwm.hasDisk(1));

    // Phases 0 and 1 in turn move the head half a track at a time.
    REQUIRE(iwm.getQuarterTrack() == 0);
    iwm.readIO(0x03); // phase 1 on
    iwm.readIO(0x02); // phase 1 off
    iwm.readIO(0x05); // phase 2 on
    iwm.readIO(0x04); // phase 2 off
    REQUIRE(iwm.getQuarterTrack() > 0);
}

// ---------------------------------------------------------------------------
// The register file, which is the part that is not the card
// ---------------------------------------------------------------------------

TEST_CASE("The Q7/Q6 pair chooses which register a read sees", "[iwm]") {
    IWM iwm;
    auto image = blankDsk();
    REQUIRE(iwm.insertDisk(0, image.data(), image.size(), "blank.dsk"));

    SECTION("Q7 low, Q6 low is the data register") {
        iwm.readIO(Q7L);
        REQUIRE(iwm.readIO(Q6L) == iwm.getDataLatch());
    }

    SECTION("Q7 low, Q6 high is status") {
        iwm.readIO(Q7L);
        const uint8_t status = iwm.readIO(Q6H);
        REQUIRE(status == iwm.readStatus());

        // The motor is in bit 5, and it answers to the same switch the card's
        // does because it is the same motor.
        REQUIRE((status & 0x20) == 0);
        iwm.readIO(MOTOR_ON);
        REQUIRE((iwm.readIO(Q6H) & 0x20) != 0);
    }

    SECTION("Q7 high, Q6 low is the handshake") {
        iwm.readIO(Q6L);
        const uint8_t handshake = iwm.readIO(Q7H);

        // The emulated sequencer takes a byte within the bit cell it was
        // loaded in, so it is always ready and never underruns. Firmware polls
        // bit 7 before loading the next byte, and a chip that never said yes
        // would spin there forever.
        REQUIRE((handshake & 0x80) != 0);
        REQUIRE((handshake & 0x40) != 0);
    }
}

TEST_CASE("The status register reports the drive and the mode", "[iwm]") {
    IWM iwm;

    // SENSE, bit 7, is whichever drive line the ca lines have selected, and
    // the one a 5.25" firmware asks about is write protect. With no disk in
    // the drive there is nothing sensing anything.
    REQUIRE((iwm.readStatus() & 0x80) == 0);

    auto image = blankDsk();
    REQUIRE(iwm.insertDisk(0, image.data(), image.size(), "blank.dsk"));
    const DiskImage *disk = iwm.getDiskImage(0);
    REQUIRE(disk != nullptr);
    REQUIRE(((iwm.readStatus() & 0x80) != 0) == disk->isWriteProtected());

    SECTION("and hands back the mode the firmware wrote") {
        iwm.readIO(Q6H);
        iwm.readIO(Q7H);
        iwm.writeIO(Q7H, 0x1F);
        REQUIRE(iwm.getModeRegister() == 0x1F);
        REQUIRE((iwm.readStatus() & 0x1F) == 0x1F);

        // Only five bits of it exist; the rest read back as zero whatever was
        // written, which is what the firmware's own check expects to see.
        iwm.writeIO(Q7H, 0xFF);
        REQUIRE(iwm.getModeRegister() == 0x1F);
    }
}

TEST_CASE("The mode register is only writable with the motor off", "[iwm]") {
    // The chip is set up before a drive is started, and cannot then be
    // disturbed by the byte stream going to the disk: with the motor running,
    // a write to the same address is data for the sequencer instead.
    IWM iwm;
    auto image = blankDsk();
    REQUIRE(iwm.insertDisk(0, image.data(), image.size(), "blank.dsk"));

    iwm.readIO(Q6H);
    iwm.readIO(Q7H);
    iwm.writeIO(Q7H, 0x17);
    REQUIRE(iwm.getModeRegister() == 0x17);

    iwm.readIO(MOTOR_ON);
    iwm.writeIO(Q7H, 0x0A);
    REQUIRE(iwm.getModeRegister() == 0x17); // unchanged
    REQUIRE(iwm.getBusData() == 0x0A);      // the byte went to the disk instead

    iwm.update(1000); // the clock has to be off zero for a pending off to arm
    iwm.readIO(MOTOR_OFF);
    iwm.update(2000000); // the motor's one-second run-on expires
    REQUIRE_FALSE(iwm.isMotorOn());
    iwm.writeIO(Q7H, 0x0A);
    REQUIRE(iwm.getModeRegister() == 0x0A);
}

TEST_CASE("A peek reads a register without touching a state line", "[iwm]") {
    // The debugger looks at the same sixteen addresses the CPU uses, and every
    // one of them is a switch: a peek that went through readIO would move the
    // head or start the motor just by being watched.
    IWM iwm;

    const uint8_t motorBefore = iwm.isMotorOn() ? 1 : 0;
    iwm.peekIO(MOTOR_ON);
    REQUIRE((iwm.isMotorOn() ? 1 : 0) == motorBefore);

    iwm.readIO(Q7L);
    iwm.readIO(Q6H);
    REQUIRE(iwm.peekIO(0x00) == iwm.readStatus());
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

TEST_CASE("An IWM's state carries its mode register", "[iwm][state]") {
    IWM saved;
    saved.readIO(Q6H);
    saved.readIO(Q7H);
    saved.writeIO(Q7H, 0x1B);
    saved.readIO(DRIVE2);

    std::vector<uint8_t> buffer(saved.getStateSize());
    const size_t written = saved.serialize(buffer.data(), buffer.size());
    REQUIRE(written > 0);

    IWM restored;
    REQUIRE(restored.deserialize(buffer.data(), written) > 0);
    REQUIRE(restored.getModeRegister() == 0x1B);
    REQUIRE(restored.getSelectedDrive() == saved.getSelectedDrive());
    REQUIRE(restored.getQ6() == saved.getQ6());
    REQUIRE(restored.getQ7() == saved.getQ7());
}

TEST_CASE("Reset clears the mode register", "[iwm]") {
    IWM iwm;
    iwm.readIO(Q6H);
    iwm.readIO(Q7H);
    iwm.writeIO(Q7H, 0x1F);
    REQUIRE(iwm.getModeRegister() == 0x1F);

    iwm.reset();
    REQUIRE(iwm.getModeRegister() == 0);
    REQUIRE_FALSE(iwm.isMotorOn());
}
