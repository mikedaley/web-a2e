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
#include "emulator.hpp"
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

TEST_CASE("The II+ profile describes an Apple II Plus", "[machine]") {
    const auto &m = machineProfile(MachineId::AppleIIPlus);

    SECTION("identity") {
        REQUIRE(std::string(m.key) == "apple2plus");
        // The II+ predates the 65C02. The CPU core models both variants and
        // the profile is what selects one.
        REQUIRE(m.cpu == CPUVariant::NMOS_6502);
    }

    SECTION("the video timing is the same circuit as a //e") {
        const auto &iie = machineProfile(MachineId::AppleIIe);
        REQUIRE(m.timing.cpuClockHz == iie.timing.cpuClockHz);
        REQUIRE(m.timing.cyclesPerScanline == iie.timing.cyclesPerScanline);
        REQUIRE(m.timing.scanlinesPerFrame == iie.timing.scanlinesPerFrame);
        REQUIRE(m.timing.visibleScanlines == iie.timing.visibleScanlines);
        REQUIRE(m.display.pixelWidth == iie.display.pixelWidth);
        REQUIRE(m.display.pixelHeight == iie.display.pixelHeight);
    }

    SECTION("it has no auxiliary bank and none of what depends on one") {
        REQUIRE(m.memory.auxRamSize == 0);
        REQUIRE_FALSE(m.caps.hasAuxRam);
        REQUIRE_FALSE(m.caps.has80Column);
        REQUIRE_FALSE(m.caps.hasDoubleHires);
        REQUIRE_FALSE(m.caps.hasAltCharSet);
        REQUIRE_FALSE(m.caps.hasLowercase);
    }

    SECTION("48KB on the motherboard, and 12KB of ROM starting at $D000") {
        REQUIRE(m.memory.mainRamSize == 48 * 1024);
        REQUIRE(m.memory.romSize == 12 * 1024);
        REQUIRE(m.memory.romBaseAddress == 0xD000);
        // Nothing on the motherboard answers at $C100-$CFFF.
        REQUIRE_FALSE(m.caps.hasInternalSlotRom);
    }

    SECTION("it never inhibits colour burst") {
        // This is the whole difference between a //e's crisp white text and a
        // II+ fringing green and violet in every mode.
        REQUIRE_FALSE(m.caps.inhibitsBurstInText);
        REQUIRE(machineProfile(MachineId::AppleIIe).caps.inhibitsBurstInText);
    }

    SECTION("slot 0 exists and the language card is not built in") {
        REQUIRE(m.firstSlot == 0);
        REQUIRE(m.hasSlot(0));
        REQUIRE_FALSE(machineProfile(MachineId::AppleIIe).hasSlot(0));
        REQUIRE_FALSE(m.caps.hasLanguageCard);
    }

    SECTION("nothing is fixed in a slot the way the //e's 80-column card is") {
        for (int slot = 0; slot < MACHINE_SLOT_COUNT; slot++) {
            REQUIRE(m.slots[slot].fixedCard == nullptr);
        }
    }
}

TEST_CASE("Every registered profile is internally consistent", "[machine]") {
    // profileIsSelfConsistent and profileFitsCompiledStorage run as
    // static_asserts at build time, so a broken profile cannot compile. This
    // repeats them at runtime so a failure names the machine that broke.
    for (int i = 0; i < MACHINE_COUNT; i++) {
        const auto &m = machineProfileAt(i);
        INFO("machine: " << m.key);
        REQUIRE(profileIsSelfConsistent(m));
        REQUIRE(profileFitsCompiledStorage(m));
    }
}

TEST_CASE("The registry finds machines by key", "[machine]") {
    REQUIRE(findMachineProfile("apple2e") == &APPLE_IIE_PROFILE);
    REQUIRE(findMachineProfile("apple2plus") == &APPLE_II_PLUS_PROFILE);

    SECTION("keys are distinct") {
        REQUIRE(std::string(APPLE_IIE_PROFILE.key) !=
                std::string(APPLE_II_PLUS_PROFILE.key));
    }

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

TEST_CASE("A machine without an auxiliary bank ignores the //e's switches",
          "[machine]") {
    // $C000-$C00F are the //e's memory and display management switches. On a
    // II+ that range manages no memory at all, and this is the single guard
    // that makes every 80-column and double-resolution path unreachable —
    // Video selects those modes from the 80COL switch, which can never be set.
    MMU iiPlus(machineProfile(MachineId::AppleIIPlus));

    iiPlus.write(0xC00D, 0); // 80COL on
    iiPlus.write(0xC001, 0); // 80STORE on
    iiPlus.write(0xC003, 0); // RAMRD on
    iiPlus.write(0xC005, 0); // RAMWRT on
    iiPlus.write(0xC009, 0); // ALTZP on
    iiPlus.write(0xC00F, 0); // ALTCHARSET on

    const auto &sw = iiPlus.getSoftSwitches();
    REQUIRE_FALSE(sw.col80);
    REQUIRE_FALSE(sw.store80);
    REQUIRE_FALSE(sw.ramrd);
    REQUIRE_FALSE(sw.ramwrt);
    REQUIRE_FALSE(sw.altzp);
    REQUIRE_FALSE(sw.altCharSet);

    SECTION("while a //e honours every one of them") {
        MMU iie(machineProfile(MachineId::AppleIIe));
        iie.write(0xC00D, 0);
        iie.write(0xC001, 0);
        iie.write(0xC003, 0);
        REQUIRE(iie.getSoftSwitches().col80);
        REQUIRE(iie.getSoftSwitches().store80);
        REQUIRE(iie.getSoftSwitches().ramrd);
    }

    SECTION("the keyboard strobe still works, since it is not one of them") {
        // $C010 sits just past the guarded range and belongs to every machine.
        bool cleared = false;
        iiPlus.setKeyStrobeCallback([&cleared]() { cleared = true; });
        iiPlus.write(0xC010, 0);
        REQUIRE(cleared);
    }
}

TEST_CASE("Colour burst follows the machine, not the video mode", "[machine]") {
    // A //e kills the burst on text lines, so an all-text screen is crisp
    // white. A II+ sends a reference on every line whatever the mode, which is
    // why its text fringes green and violet. Video reads the flag, so the
    // behaviour follows from the profile alone.
    struct Fixture {
        MMU mmu;
        Video video;
        explicit Fixture(const MachineProfile &m) : mmu(m), video(mmu) {
            mmu.loadROM(roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE,
                        roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
        }
    };

    Fixture iie(machineProfile(MachineId::AppleIIe));
    Fixture iiPlus(machineProfile(MachineId::AppleIIPlus));

    // Full text mode: every visible line is a text line.
    iie.mmu.write(0xC051, 0);
    iiPlus.mmu.write(0xC051, 0);

    iie.video.forceRenderFrame();
    iiPlus.video.forceRenderFrame();

    REQUIRE_FALSE(iie.video.isChromaEnabled());
    REQUIRE(iiPlus.video.isChromaEnabled());
}

TEST_CASE("A machine cannot be started without its ROM", "[machine]") {
    // The //e's ROM is built in unconditionally. The II+ set is optional, so
    // this asserts the relationship rather than a fixed answer: whether the
    // machine is runnable must match whether its ROM is actually present.
    REQUIRE(Emulator::isMachineRunnable(MachineId::AppleIIe));

    Emulator iie(MachineId::AppleIIe);
    iie.init();
    REQUIRE(iie.hasSystemROM());
    REQUIRE(iie.getMachine().id == MachineId::AppleIIe);

    Emulator iiPlus(MachineId::AppleIIPlus);
    iiPlus.init();
    REQUIRE(iiPlus.getMachine().id == MachineId::AppleIIPlus);
    REQUIRE(iiPlus.hasSystemROM() ==
            Emulator::isMachineRunnable(MachineId::AppleIIPlus));
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
