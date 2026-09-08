/*
 * test_machine_profile.cpp - Unit tests for the machine profile layer
 *
 * The profile is the single description of a machine that the subsystems read.
 * These tests pin two things: that the //e profile still says what the //e has
 * always been, and that the seam actually carries — an MMU, a Video and an
 * Audio built for a machine agree with each other and with the profile they
 * were handed, rather than each quietly falling back to a compiled-in default.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "audio/audio.hpp"
#include "machine/machine_profile.hpp"
#include "mmu/mmu.hpp"
#include "video/video.hpp"
#include "roms.cpp"

using namespace a2e;

TEST_CASE("The //e profile describes an Apple //e", "[machine]") {
    const auto &m = machineProfile(MachineId::AppleIIe);

    SECTION("identity") {
        REQUIRE(m.id == MachineId::AppleIIe);
        REQUIRE(std::string(m.key) == "apple2e");
        REQUIRE(m.cpu == CPUVariant::CMOS_65C02);
    }

    SECTION("timing") {
        REQUIRE(m.timing.cpuClockHz == Approx(1023000.0));
        REQUIRE(m.timing.cyclesPerScanline == 65);
        REQUIRE(m.timing.hblankCycles == 25);
        REQUIRE(m.timing.visibleColumns == 40);
        REQUIRE(m.timing.scanlinesPerFrame == 262);
        REQUIRE(m.timing.visibleScanlines == 192);
        REQUIRE(m.timing.mixedModeTextScanline == 160);
        REQUIRE(m.timing.cyclesPerFrame() == 17030);
        REQUIRE(m.timing.cyclesPerSample(48000) == Approx(21.3125));
    }

    SECTION("memory") {
        REQUIRE(m.memory.mainRamSize == 64 * 1024);
        REQUIRE(m.memory.auxRamSize == 64 * 1024);
        REQUIRE(m.memory.totalRamSize() == 128 * 1024);
        REQUIRE(m.memory.romSize == 16 * 1024);
        REQUIRE(m.memory.charRomSize == 8 * 1024);
    }

    SECTION("display") {
        // 40 columns of 14 dots each, mapped 1:1 onto pixels, lines doubled.
        REQUIRE(m.display.dotsPerLine == 560);
        REQUIRE(m.display.pixelWidth == 560);
        REQUIRE(m.display.pixelHeight == 384);
        REQUIRE(m.display.framebufferSize() == 560 * 384 * 4);
    }

    SECTION("capabilities") {
        REQUIRE(m.caps.hasAuxRam);
        REQUIRE(m.caps.has80Column);
        REQUIRE(m.caps.hasDoubleHires);
        // A //e kills the burst on text lines; a II+ would not, and that flag
        // is the whole difference between crisp text and fringed text.
        REQUIRE(m.caps.inhibitsBurstInText);
    }

    SECTION("slot 3 holds the built-in 80-column card and cannot be changed") {
        REQUIRE(m.slots[3].fixedCard != nullptr);
        REQUIRE(std::string(m.slots[3].fixedCard) == "80col");

        // Every other slot is the user's to fill.
        for (int slot = 1; slot < MACHINE_SLOT_COUNT; slot++) {
            if (slot == 3) continue;
            REQUIRE(m.slots[slot].fixedCard == nullptr);
        }
    }

    SECTION("default cards match the documented slot map") {
        REQUIRE(std::string(m.slots[4].defaultCard) == "mockingboard");
        REQUIRE(std::string(m.slots[5].defaultCard) == "thunderclock");
        REQUIRE(std::string(m.slots[6].defaultCard) == "disk2");
        REQUIRE(std::string(m.slots[7].defaultCard) == "smartport");
        REQUIRE(m.slots[1].defaultCard == nullptr);
        REQUIRE(m.slots[2].defaultCard == nullptr);
    }
}

TEST_CASE("The registry finds machines by key", "[machine]") {
    REQUIRE(findMachineProfile("apple2e") == &APPLE_IIE_PROFILE);

    SECTION("an unknown key is reported as unknown, not silently a //e") {
        REQUIRE(findMachineProfile("apple2gs") == nullptr);
        REQUIRE(findMachineProfile("") == nullptr);
        REQUIRE(findMachineProfile(nullptr) == nullptr);
    }

    SECTION("every registered machine has a unique, findable key") {
        for (int i = 0; i < MACHINE_COUNT; i++) {
            const auto &m = machineProfileAt(i);
            REQUIRE(findMachineProfile(m.key) == &m);
            REQUIRE(static_cast<int>(m.id) == i);
        }
    }

    SECTION("an out-of-range index falls back rather than reading past the end") {
        REQUIRE(&machineProfileAt(-1) == &APPLE_IIE_PROFILE);
        REQUIRE(&machineProfileAt(MACHINE_COUNT) == &APPLE_IIE_PROFILE);
    }
}

TEST_CASE("Subsystems take their timing from the profile they are given",
          "[machine]") {
    const auto &m = defaultMachineProfile();

    MMU mmu(m);
    mmu.loadROM(roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE,
                roms::ROM_CHAR, roms::ROM_CHAR_SIZE);

    SECTION("the MMU keeps the profile it was constructed with") {
        REQUIRE(&mmu.getMachine() == &m);
    }

    SECTION("Video inherits the MMU's machine rather than taking its own") {
        // The video scanner and the floating bus are the same counters read two
        // ways, so a Video and an MMU that disagreed would be unrepresentable.
        Video video(mmu);
        REQUIRE(&video.getMachine() == &mmu.getMachine());
    }
}

TEST_CASE("VBL follows the profile's visible scanline count", "[machine]") {
    const auto &m = defaultMachineProfile();

    MMU mmu(m);
    mmu.loadROM(roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE,
                roms::ROM_CHAR, roms::ROM_CHAR_SIZE);

    uint64_t cycles = 0;
    mmu.setCycleCallback([&cycles]() { return cycles; });

    // $C019 reads bit 7 clear during vertical blank. The boundary is the first
    // scanline past the visible area, which the profile names.
    const int lastVisibleLine = m.timing.visibleScanlines - 1;
    cycles = static_cast<uint64_t>(lastVisibleLine) * m.timing.cyclesPerScanline;
    REQUIRE((mmu.read(0xC019) & 0x80) == 0x80);

    cycles = static_cast<uint64_t>(m.timing.visibleScanlines) *
             m.timing.cyclesPerScanline;
    REQUIRE((mmu.read(0xC019) & 0x80) == 0x00);

    // And it comes back for the next frame.
    cycles = static_cast<uint64_t>(m.timing.cyclesPerFrame());
    REQUIRE((mmu.read(0xC019) & 0x80) == 0x80);
}

TEST_CASE("Audio measures a sample against the machine's clock", "[machine]") {
    // The profile's clock is what turns a sample count into a span of emulated
    // cycles, and that span is what paces the whole emulator.
    const auto &m = defaultMachineProfile();
    REQUIRE(m.timing.cyclesPerSample(48000) == Approx(21.3125));

    Audio audio(m);
    float buffer[512];

    // A buffer covering exactly the expected cycle span is accepted whole: it
    // is not clamped, so the speaker state at the end is the state we set.
    audio.reset();
    const auto span = static_cast<uint64_t>(256 * m.timing.cyclesPerSample(48000));
    REQUIRE(audio.generateStereoSamples(buffer, 256, span) == 256);
}

TEST_CASE("The legacy constants still agree with the profile", "[machine]") {
    // types.hpp sizes arrays at compile time and so cannot become a profile
    // lookup. machine_profile.hpp static_asserts the pair; this repeats a few
    // at runtime so a failure names which number moved.
    const auto &m = defaultMachineProfile();

    REQUIRE(m.memory.mainRamSize == MAIN_RAM_SIZE);
    REQUIRE(m.memory.auxRamSize == AUX_RAM_SIZE);
    REQUIRE(m.display.pixelWidth == SCREEN_WIDTH);
    REQUIRE(m.display.pixelHeight == SCREEN_HEIGHT);
    REQUIRE(m.display.framebufferSize() == FRAMEBUFFER_SIZE);
    REQUIRE(m.timing.cyclesPerFrame() == CYCLES_PER_FRAME);
    REQUIRE(m.timing.cyclesPerScanline == CYCLES_PER_SCANLINE);
    REQUIRE(m.timing.scanlinesPerFrame == SCANLINES_PER_FRAME);
}
