/*
 * test_smartport_card.cpp - Unit tests for SmartPortCard
 *
 * Tests the SmartPort hard drive controller card including:
 * - Construction
 * - Card metadata (name, preferred slot)
 * - No device initially: hasROM false, no images inserted
 * - Image insert/eject operations
 * - Filename tracking
 * - Slot number configuration
 * - Activity tracking
 * - hasROM becomes true when a device is present
 * - Serialization round-trip
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "smartport_card.hpp"

#include <cstring>
#include <vector>

using namespace a2e;

// Helper: create minimal block data (one block = 512 bytes minimum)
static std::vector<uint8_t> createMinimalImage(size_t blocks = 280) {
    return std::vector<uint8_t>(blocks * 512, 0x00);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard constructor creates valid instance", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.getName() != nullptr);
}

// ---------------------------------------------------------------------------
// Card metadata
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard getName returns SmartPort", "[smartport]") {
    SmartPortCard card;
    REQUIRE(std::string(card.getName()) == "SmartPort");
}

TEST_CASE("SmartPortCard getPreferredSlot returns 7", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.getPreferredSlot() == 7);
}

// ---------------------------------------------------------------------------
// No device initially
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard hasROM false when no device present", "[smartport]") {
    SmartPortCard card;
    // hasROM() depends on hasAnyDevice() which should be false with no images
    REQUIRE_FALSE(card.hasROM());
}

TEST_CASE("SmartPortCard no images inserted initially", "[smartport]") {
    SmartPortCard card;
    REQUIRE_FALSE(card.isImageInserted(0));
    REQUIRE_FALSE(card.isImageInserted(1));
}

// ---------------------------------------------------------------------------
// Image insert
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard insertImage makes isImageInserted true", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    bool result = card.insertImage(0, data.data(), data.size(), "test.hdv");
    REQUIRE(result);
    REQUIRE(card.isImageInserted(0));
}

TEST_CASE("SmartPortCard insertImage into device 1", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    bool result = card.insertImage(1, data.data(), data.size(), "disk2.hdv");
    REQUIRE(result);
    REQUIRE(card.isImageInserted(1));
    REQUIRE_FALSE(card.isImageInserted(0)); // device 0 still empty
}

TEST_CASE("SmartPortCard hasROM becomes true after insertImage", "[smartport]") {
    SmartPortCard card;
    REQUIRE_FALSE(card.hasROM());

    auto data = createMinimalImage();
    card.insertImage(0, data.data(), data.size(), "prodos.hdv");

    REQUIRE(card.hasROM());
}

// ---------------------------------------------------------------------------
// Image filename
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard getImageFilename returns inserted filename", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    card.insertImage(0, data.data(), data.size(), "myvolume.po");
    REQUIRE(card.getImageFilename(0) == "myvolume.po");
}

TEST_CASE("SmartPortCard getImageFilename returns empty when no image", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.getImageFilename(0).empty());
    REQUIRE(card.getImageFilename(1).empty());
}

// ---------------------------------------------------------------------------
// Image eject
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard ejectImage makes isImageInserted false", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    card.insertImage(0, data.data(), data.size(), "test.hdv");
    REQUIRE(card.isImageInserted(0));

    card.ejectImage(0);
    REQUIRE_FALSE(card.isImageInserted(0));
}

TEST_CASE("SmartPortCard ejectImage clears filename", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    card.insertImage(0, data.data(), data.size(), "test.hdv");
    card.ejectImage(0);
    REQUIRE(card.getImageFilename(0).empty());
}

TEST_CASE("SmartPortCard hasROM becomes false after ejecting all devices", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    card.insertImage(0, data.data(), data.size(), "test.hdv");
    REQUIRE(card.hasROM());

    card.ejectImage(0);
    REQUIRE_FALSE(card.hasROM());
}

// ---------------------------------------------------------------------------
// Slot number
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard default slot number is 7", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.getSlotNumber() == 7);
}

TEST_CASE("SmartPortCard setSlotNumber changes slot", "[smartport]") {
    SmartPortCard card;

    card.setSlotNumber(5);
    REQUIRE(card.getSlotNumber() == 5);

    card.setSlotNumber(2);
    REQUIRE(card.getSlotNumber() == 2);
}

// ---------------------------------------------------------------------------
// Activity tracking
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard hasActivity is false initially", "[smartport]") {
    SmartPortCard card;
    REQUIRE_FALSE(card.hasActivity());
}

TEST_CASE("SmartPortCard clearActivity does not crash when no activity", "[smartport]") {
    SmartPortCard card;
    card.clearActivity();
    REQUIRE_FALSE(card.hasActivity());
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard reset does not crash", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();
    card.insertImage(0, data.data(), data.size(), "test.hdv");

    card.reset();
    // Image should still be inserted after reset (reset clears state, not images)
    REQUIRE(card.isImageInserted(0));
}

// ---------------------------------------------------------------------------
// I/O space
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard readIO returns a value without crash", "[smartport]") {
    SmartPortCard card;
    uint8_t val = card.readIO(0x00);
    (void)val;
    REQUIRE(true);
}

TEST_CASE("SmartPortCard writeIO does not crash", "[smartport]") {
    SmartPortCard card;
    card.writeIO(0x00, 0x55);
    REQUIRE(true);
}

TEST_CASE("SmartPortCard peekIO returns 0xFF", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.peekIO(0x00) == 0xFF);
}

// ---------------------------------------------------------------------------
// Serialization round-trip
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard getStateSize is greater than zero when image loaded", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();
    card.insertImage(0, data.data(), data.size(), "test.hdv");

    REQUIRE(card.getStateSize() > 0);
}

TEST_CASE("SmartPortCard serialize/deserialize round-trip", "[smartport]") {
    SmartPortCard card1;
    auto data = createMinimalImage();
    card1.insertImage(0, data.data(), data.size(), "saved.hdv");
    card1.setSlotNumber(5);

    // Serialize
    size_t stateSize = card1.getStateSize();
    std::vector<uint8_t> buffer(stateSize);
    size_t written = card1.serialize(buffer.data(), buffer.size());
    REQUIRE(written > 0);
    REQUIRE(written <= stateSize);

    // Deserialize
    SmartPortCard card2;
    size_t consumed = card2.deserialize(buffer.data(), written);
    REQUIRE(consumed > 0);

    // Verify image and filename were preserved
    REQUIRE(card2.isImageInserted(0));
    REQUIRE(card2.getImageFilename(0) == "saved.hdv");
}

// ---------------------------------------------------------------------------
// Multiple devices
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard supports two devices simultaneously", "[smartport]") {
    SmartPortCard card;
    auto data1 = createMinimalImage(280);
    auto data2 = createMinimalImage(560);

    card.insertImage(0, data1.data(), data1.size(), "disk1.hdv");
    card.insertImage(1, data2.data(), data2.size(), "disk2.hdv");

    REQUIRE(card.isImageInserted(0));
    REQUIRE(card.isImageInserted(1));
    REQUIRE(card.getImageFilename(0) == "disk1.hdv");
    REQUIRE(card.getImageFilename(1) == "disk2.hdv");
}

TEST_CASE("SmartPortCard eject one device keeps the other", "[smartport]") {
    SmartPortCard card;
    auto data1 = createMinimalImage();
    auto data2 = createMinimalImage();

    card.insertImage(0, data1.data(), data1.size(), "disk1.hdv");
    card.insertImage(1, data2.data(), data2.size(), "disk2.hdv");

    card.ejectImage(0);

    REQUIRE_FALSE(card.isImageInserted(0));
    REQUIRE(card.isImageInserted(1));
    // hasROM should still be true because device 1 is present
    REQUIRE(card.hasROM());
}

// ---------------------------------------------------------------------------
// MAX_DEVICES constant
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard MAX_DEVICES is 2", "[smartport]") {
    REQUIRE(SmartPortCard::MAX_DEVICES == 2);
}

// ---------------------------------------------------------------------------
// ROM access with device loaded
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard readROM returns valid data when device loaded", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();
    card.insertImage(0, data.data(), data.size(), "test.hdv");

    // The self-built ROM should not be all zeros
    bool allZero = true;
    for (int i = 0; i < 256; ++i) {
        if (card.readROM(static_cast<uint8_t>(i)) != 0x00) {
            allZero = false;
            break;
        }
    }
    REQUIRE_FALSE(allZero);
}

// ---------------------------------------------------------------------------
// The SmartPort protocol, as GS/OS speaks it
// ---------------------------------------------------------------------------

namespace {
// A machine with two banks of memory, a stack in page one, and a card in
// slot 5 whose entry point is being executed rather than read.
struct SmartPortRig {
    std::vector<uint8_t> memory = std::vector<uint8_t>(0x20000, 0);
    SmartPortCard card;
    uint16_t sp = 0x01F0;
    uint8_t a = 0, x = 0, y = 0, p = 0;
    uint16_t pc = 0;
    bool executing = false;

    SmartPortRig() {
        card.setSlotNumber(5);
        card.setMemReadCallback([this](uint16_t at) { return memory[at]; });
        card.setMemWriteCallback([this](uint16_t at, uint8_t v) { memory[at] = v; });
        card.setMemRead24Callback([this](uint32_t at) { return memory[at & 0x1FFFF]; });
        card.setMemWrite24Callback([this](uint32_t at, uint8_t v) { memory[at & 0x1FFFF] = v; });
        card.setGetA([this]() { return a; });
        card.setSetA([this](uint8_t v) { a = v; });
        card.setGetP([this]() { return p; });
        card.setSetP([this](uint8_t v) { p = v; });
        card.setGetSP([this]() { return sp; });
        card.setSetSP([this](uint16_t v) { sp = v; });
        card.setGetPC([this]() { return pc; });
        card.setSetPC([this](uint16_t v) { pc = v; });
        card.setSetX([this](uint8_t v) { x = v; });
        card.setSetY([this](uint8_t v) { y = v; });
        card.setExecutingAt([this](uint16_t at) { return executing && at == 0xC513; });

        std::vector<uint8_t> image(512 * 64, 0);
        card.insertImage(0, image.data(), image.size(), "hd.po");
    }

    // JSR $C513 from `site`, with the inline command and pointer after it,
    // then the CPU fetching the entry point: which is when the card acts.
    void call(uint16_t site, uint8_t command, uint32_t paramList, bool extended) {
        memory[site] = 0x20; memory[site + 1] = 0x13; memory[site + 2] = 0xC5;
        memory[site + 3] = command;
        memory[site + 4] = static_cast<uint8_t>(paramList);
        memory[site + 5] = static_cast<uint8_t>(paramList >> 8);
        if (extended) {
            memory[site + 6] = static_cast<uint8_t>(paramList >> 16);
            memory[site + 7] = 0x00;
        }
        // JSR pushes the address of its own last byte.
        const uint16_t pushed = site + 2;
        memory[sp] = static_cast<uint8_t>(pushed >> 8);
        memory[sp - 1] = static_cast<uint8_t>(pushed);
        sp -= 2;
        executing = true;
        const uint8_t opcode = card.readROM(0x13);
        executing = false;
        REQUIRE(opcode == 0x60); // RTS, to wherever the card put the return
    }

    uint16_t returnAddress() const {
        return static_cast<uint16_t>(memory[sp + 1] | (memory[sp + 2] << 8)) + 1;
    }
    bool carry() const { return (p & 0x01) != 0; }
};
} // namespace

TEST_CASE("SmartPort STATUS code 3 is the Device Information Block",
          "[smartport][protocol]") {
    // This is what GS/OS reads to decide what it has found, and it will not
    // build a driver for a device that cannot answer it: the status byte, a
    // three-byte block count, a name, a type and a version — 25 bytes.
    SmartPortRig rig;
    const uint16_t list = 0x2000, buffer = 0x3000;
    rig.memory[list] = 3; rig.memory[list + 1] = 1;          // count, unit 1
    rig.memory[list + 2] = 0x00; rig.memory[list + 3] = 0x30; // status list
    rig.memory[list + 4] = 0x03;                              // DIB

    rig.call(0x0800, 0x00, list, false);

    REQUIRE_FALSE(rig.carry());
    REQUIRE(rig.a == 0);
    REQUIRE(rig.returnAddress() == 0x0806); // stepped over three inline bytes
    REQUIRE((rig.memory[buffer] & 0x80) != 0);   // a block device
    REQUIRE(rig.memory[buffer + 1] == 64);       // 64 blocks, low byte
    REQUIRE(rig.memory[buffer + 2] == 0);
    REQUIRE(rig.memory[buffer + 3] == 0);
    REQUIRE(rig.memory[buffer + 4] == 16);       // name length
    REQUIRE(rig.memory[buffer + 21] == 0x02);    // hard disk
    REQUIRE((rig.memory[buffer + 22] & 0x80) != 0); // takes extended calls
    REQUIRE(rig.x == 25);                        // bytes returned
    REQUIRE(rig.y == 0);
}

TEST_CASE("An extended SmartPort call has a four-byte pointer and reads into any bank",
          "[smartport][protocol]") {
    // Bit 6 of the command is the extended dialect: the inline pointer is a
    // long, the parameter list's pointer is a long, and the block number is
    // four bytes. GS/OS uses it for everything, because a 65816 wants its
    // buffers in banks other than zero — and a card that stepped over three
    // inline bytes instead of five returned into the middle of the pointer.
    SmartPortRig rig;
    std::vector<uint8_t> image(512 * 64, 0);
    for (int i = 0; i < 512; i++) image[7 * 512 + i] = static_cast<uint8_t>(i);
    rig.card.insertImage(0, image.data(), image.size(), "hd.po");

    const uint32_t list = 0x14000, buffer = 0x18000; // both in bank 1
    rig.memory[list] = 3; rig.memory[list + 1] = 1;
    rig.memory[list + 2] = 0x00; rig.memory[list + 3] = 0x80; rig.memory[list + 4] = 0x01; rig.memory[list + 5] = 0x00;
    rig.memory[list + 6] = 7; rig.memory[list + 7] = 0; rig.memory[list + 8] = 0; rig.memory[list + 9] = 0; // block 7

    rig.call(0x0800, 0x41, list, true);

    REQUIRE_FALSE(rig.carry());
    REQUIRE(rig.returnAddress() == 0x0808); // five inline bytes this time
    for (int i = 0; i < 512; i++) {
        INFO("byte " << i);
        REQUIRE(rig.memory[buffer + i] == static_cast<uint8_t>(i));
    }
}

TEST_CASE("A SmartPort card says it takes extended calls", "[smartport][protocol]") {
    // $CnFB is the SmartPort ID type byte and bit 7 is the promise. GS/OS
    // reads it before it will build a driver for the slot, and passes over a
    // card that says no.
    SmartPortCard card;
    std::vector<uint8_t> image(512 * 8, 0);
    card.insertImage(0, image.data(), image.size(), "hd.po");
    REQUIRE((card.readROM(0xFB) & 0x80) != 0);
    REQUIRE(card.readROM(0xFF) == 0x10);
    // ...and $CnFE counts its volumes: one device fitted, so bits 5-4 are zero.
    REQUIRE((card.readROM(0xFE) & 0x30) == 0x00);
    card.insertImage(1, image.data(), image.size(), "hd2.po");
    REQUIRE((card.readROM(0xFE) & 0x30) == 0x10);
}

TEST_CASE("The IIgs layout puts the entries where the machine's own firmware has them",
          "[smartport][iigs]") {
    // A IIgs's slot 5 firmware has $C5FF = $0A: the ProDOS entry at $C50A and
    // the SmartPort entry at $C50D, and software written for the machine
    // hard-codes those. A card standing in for that firmware has to answer
    // there, with the fall-through boot path still reaching its stub.
    SmartPortCard card;
    card.setSlotNumber(5);
    card.setProDOSEntry(0x0A);
    std::vector<uint8_t> image(512 * 16, 0);
    REQUIRE(card.insertImage(0, image.data(), image.size(), "hd.po"));

    REQUIRE(card.readROM(0xFF) == 0x0A);
    REQUIRE(card.readROM(0x01) == 0x20); // still a ProDOS block device
    REQUIRE(card.readROM(0x0A) == 0x38); // SEC / RTS at the ProDOS entry
    REQUIRE(card.readROM(0x0B) == 0x60);
    REQUIRE(card.readROM(0x0D) == 0x38); // and at the SmartPort entry
    REQUIRE(card.readROM(0x0E) == 0x60);
    REQUIRE(card.readROM(0x08) == 0x80); // BRA over them...
    REQUIRE(card.readROM(0x10) == 0xA2); // ...to the boot stub
    REQUIRE(card.readROM(0x11) == 0x50);
    REQUIRE(card.readROM(0x12) == 0x8E);
    REQUIRE(card.readROM(0x15) == 0x60);
    REQUIRE(card.prodosEntry() == 0x0A);
    REQUIRE(card.smartPortEntry() == 0x0D);

    // And the firmware's status byte: four volumes, removable, interrupting,
    // whatever is fitted — ProDOS 8 1.x needs the drive 2 that implies.
    REQUIRE(card.readROM(0xFE) == 0xBF);

    // The default is a card's own layout.
    SmartPortCard plain;
    plain.setSlotNumber(5);
    REQUIRE(plain.insertImage(0, image.data(), image.size(), "hd.po"));
    REQUIRE(plain.readROM(0xFF) == 0x10);
    REQUIRE(plain.readROM(0x08) == 0xA2);
}
