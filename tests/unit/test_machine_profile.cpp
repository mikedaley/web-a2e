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
#include "cards/thunderclock/thunderclock_card.hpp"
#include "emulator.hpp"
#include "machine/machine_profile.hpp"
#include "mmu/mmu.hpp"
#include "video/video.hpp"
#include "roms.cpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

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

    SECTION("slot 0 exists, which is where its language card goes") {
        // A //e carries the language card on the motherboard and has no slot
        // 0 at all; on a II+ the same bank switching arrives on a card.
        REQUIRE(m.firstSlot == 0);
        REQUIRE(m.hasSlot(0));
        REQUIRE_FALSE(machineProfile(MachineId::AppleIIe).hasSlot(0));
        REQUIRE(m.caps.hasLanguageCard);
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

// Run a machine for roughly the given number of frames of emulated time.
static void runFrames(Emulator &e, int frames) {
    const int cyclesPerFrame = e.getMachine().timing.cyclesPerFrame();
    for (int i = 0; i < frames; i++) e.runCycles(cyclesPerFrame);
}

// The text the machine is showing, with blank lines dropped.
static std::vector<std::string> visibleLines(Emulator &e) {
    const char *raw = e.readScreenText(0, 0, 23, 39);
    std::vector<std::string> lines;
    if (!raw) return lines;

    std::string text(raw);
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(start, nl - start);
        if (line.find_first_not_of(' ') != std::string::npos) {
            lines.push_back(line);
        }
        if (nl == text.size()) break;
        start = nl + 1;
    }
    return lines;
}

// Whether any visible line shows the Applesoft prompt.
static bool showsApplesoftPrompt(Emulator &e) {
    for (const auto &line : visibleLines(e)) {
        if (line.find(']') != std::string::npos) return true;
    }
    return false;
}

TEST_CASE("Each machine boots to its own prompt", "[machine][boot]") {
    // The real test of the profile layer: two machines, two CPUs, two ROMs at
    // two different base addresses, one emulator.
    //
    // Both ship with a Disk II and no disk in it, so both sit at their boot
    // screen waiting for a drive that will never answer. Ctrl+Reset is what
    // drops a real machine into BASIC, and it is what these do here.
    auto bootToBasic = [](Emulator &e) {
        runFrames(e, 120);
        e.warmReset();
        runFrames(e, 120);
    };

    SECTION("the //e reaches Applesoft") {
        Emulator e(MachineId::AppleIIe);
        e.init();
        REQUIRE(e.hasSystemROM());

        bootToBasic(e);

        // Reaching the prompt means the reset vector, the ROM mapping and the
        // CPU all did their jobs.
        INFO("screen: " << visibleLines(e).back());
        REQUIRE(showsApplesoftPrompt(e));
    }

    SECTION("the II+ reaches Applesoft on an NMOS 6502 and a $D000 ROM") {
        // Skipped rather than failed when the optional II+ ROMs are not in the
        // build — the machine is still described, it just cannot start.
        if (!Emulator::isMachineRunnable(MachineId::AppleIIPlus)) {
            WARN("II+ ROMs not built in; skipping the boot test");
            return;
        }

        Emulator e(MachineId::AppleIIPlus);
        e.init();
        REQUIRE(e.hasSystemROM());
        REQUIRE(e.getMachine().cpu == CPUVariant::NMOS_6502);

        bootToBasic(e);

        INFO("screen: " << visibleLines(e).back());
        REQUIRE(showsApplesoftPrompt(e));

        // And it got there without ever setting a //e switch.
        const auto &sw = e.getMMU().getSoftSwitches();
        REQUIRE_FALSE(sw.col80);
        REQUIRE_FALSE(sw.ramrd);
        REQUIRE_FALSE(sw.altzp);
    }
}

TEST_CASE("Character ROMs are normalised to one layout", "[machine][video]") {
    // The //e and the II+ hold the same glyphs but store them differently: a
    // //e puts bit 0 at the left of a glyph row and the blank scanline last, a
    // II+ puts bit 6 at the left and the blank scanline first. The renderer
    // reads one layout, so the ROM is rewritten when it is loaded.
    //
    // Getting this wrong is not subtle and it is not safe: every character on
    // screen comes out mirrored. That is exactly what the II+ did before the
    // normalisation existed, and its boot banner read "]["  backwards.
    if (!Emulator::isMachineRunnable(MachineId::AppleIIPlus)) {
        WARN("II+ ROMs not built in; skipping the character ROM test");
        return;
    }

    Emulator iie(MachineId::AppleIIe);
    iie.init();
    Emulator iiPlus(MachineId::AppleIIPlus);
    iiPlus.init();

    SECTION("both machines present identical letters to the renderer") {
        // Cells 1-26 are 'A' to 'Z', which both ROM revisions draw the same
        // way. Compared rather than merely inspected because a mirrored or
        // row-shifted glyph fails here immediately and unmistakably.
        //
        // The comparison stops at 'Z' on purpose. Beyond the letters the two
        // ROMs genuinely differ — they are parts eight years apart, and the
        // //e draws its underscore on a different scanline from the II+ — so
        // insisting on byte equality there would be asserting something that
        // was never true.
        for (int cell = 1; cell <= 26; cell++) {
            for (int row = 0; row < 8; row++) {
                const uint16_t at = static_cast<uint16_t>(cell * 8 + row);
                INFO("cell " << cell << " row " << row);
                REQUIRE(iiPlus.getMMU().readCharROM(at) ==
                        iie.getMMU().readCharROM(at));
            }
        }
    }

    SECTION("an asymmetric glyph is not mirrored") {
        // 'F' is the clearest test there is: a mirrored F is obvious, and a
        // symmetric letter like 'A' or 'H' would pass either way.
        const uint16_t f = 6 * 8;

        // Row 0 is the full top bar; row 3 the shorter middle bar. Both start
        // at the left of the glyph, so bit 1 is set and bit 6 is not.
        const uint8_t top = iiPlus.getMMU().readCharROM(f);
        const uint8_t middle = iiPlus.getMMU().readCharROM(f + 3);
        REQUIRE((top & 0x02) != 0);
        REQUIRE((middle & 0x02) != 0);

        // The stem is on the left, so no row reaches the rightmost dot.
        for (int row = 0; row < 8; row++) {
            REQUIRE((iiPlus.getMMU().readCharROM(f + row) & 0x40) == 0);
        }

        // And the blank scanline ends the cell rather than starting it.
        REQUIRE(iiPlus.getMMU().readCharROM(f + 7) == 0);
        REQUIRE(iiPlus.getMMU().readCharROM(f) != 0);
    }
}

TEST_CASE("A machine with one character set ignores the UK switch",
          "[machine][video]") {
    // The UK set is a second bank inside the //e's 8KB character ROM, reached
    // by adding 0x1000 to the glyph offset. A II+'s generator is 2KB and holds
    // a single set, so that offset lands past the end of the image and every
    // glyph comes back blank — a screen showing nothing but the cursor, which
    // survives because it is the inverse of a blank and so still solid.
    if (!Emulator::isMachineRunnable(MachineId::AppleIIPlus)) {
        WARN("II+ ROMs not built in; skipping the UK character set test");
        return;
    }

    auto rendersText = [](Emulator &e) {
        // Put a word on the top line and count the lit dots in it.
        const char *word = "HELLO";
        for (int i = 0; word[i]; i++) {
            e.getMMU().writeRAM(static_cast<uint16_t>(0x400 + i),
                                static_cast<uint8_t>(word[i] | 0x80));
        }
        e.getVideo().forceRenderFrame();

        const uint8_t *fb = e.getVideo().getFramebuffer();
        const int width = e.getMachine().display.pixelWidth;
        int lit = 0;
        for (int row = 0; row < 14; row++) {
            for (int x = 0; x < 70; x++) {
                const size_t o = static_cast<size_t>(row) * width * 4 +
                                 static_cast<size_t>(x) * 4;
                if (fb[o] > 90 || fb[o + 1] > 90 || fb[o + 2] > 90) lit++;
            }
        }
        return lit;
    };

    Emulator iiPlus(MachineId::AppleIIPlus);
    iiPlus.init();
    const int normal = rendersText(iiPlus);
    REQUIRE(normal > 100); // The word is plainly there

    iiPlus.getVideo().setUKCharacterSet(true);
    REQUIRE(rendersText(iiPlus) == normal); // ...and the switch changes nothing

    SECTION("while a //e does have a second set to switch to") {
        REQUIRE(machineProfile(MachineId::AppleIIe).caps.hasUkCharSet);
        REQUIRE_FALSE(machineProfile(MachineId::AppleIIPlus).caps.hasUkCharSet);

        Emulator iie(MachineId::AppleIIe);
        iie.init();
        const int us = rendersText(iie);
        iie.getVideo().setUKCharacterSet(true);
        REQUIRE(rendersText(iie) > 100); // Still legible, just a different set
        REQUIRE(us > 100);
    }
}

TEST_CASE("Text fringes on a II+ and not on a //e", "[machine][video]") {
    // The consequence of caps.inhibitsBurstInText, and the most visible
    // difference between the two machines: an all-text screen on a //e kills
    // the burst and comes out crisp white, while a II+ sends a reference on
    // every line and its text picks up colour on a colour monitor. This is
    // what real hardware did, and it is why II+ owners used green screens for
    // text work.
    auto colouredPixels = [](Emulator &e, VideoColorMode mode) {
        e.getVideo().setColorMode(mode);
        const char *word = "HELLO WORLD";
        for (int i = 0; word[i]; i++) {
            e.getMMU().writeRAM(static_cast<uint16_t>(0x400 + i),
                                static_cast<uint8_t>(word[i] | 0x80));
        }
        e.getVideo().forceRenderFrame();

        const uint8_t *fb = e.getVideo().getFramebuffer();
        const int width = e.getMachine().display.pixelWidth;
        int coloured = 0;
        for (int row = 0; row < 16; row++) {
            for (int x = 0; x < 160; x++) {
                const size_t o = static_cast<size_t>(row) * width * 4 +
                                 static_cast<size_t>(x) * 4;
                const int r = fb[o], g = fb[o + 1], b = fb[o + 2];
                const int hi = std::max(r, std::max(g, b));
                const int lo = std::min(r, std::min(g, b));
                if (hi - lo > 40) coloured++; // Not a grey
            }
        }
        return coloured;
    };

    Emulator iie(MachineId::AppleIIe);
    iie.init();

    SECTION("a //e's text is grey in every mode, because it kills the burst") {
        REQUIRE(colouredPixels(iie, VideoColorMode::PIXEL_EXACT) == 0);
        REQUIRE(colouredPixels(iie, VideoColorMode::RGB_MONITOR) == 0);
        REQUIRE(colouredPixels(iie, VideoColorMode::COMPOSITE) == 0);
        REQUIRE(colouredPixels(iie, VideoColorMode::MONOCHROME) == 0);
    }

    if (!Emulator::isMachineRunnable(MachineId::AppleIIPlus)) return;

    Emulator iiPlus(MachineId::AppleIIPlus);
    iiPlus.init();

    SECTION("a II+'s text carries colour wherever a decoder is running") {
        // Even the sharp modes tint the strokes when the burst is live; what
        // they refuse to do is let the colour spread past them.
        REQUIRE(colouredPixels(iiPlus, VideoColorMode::PIXEL_EXACT) > 0);
        REQUIRE(colouredPixels(iiPlus, VideoColorMode::COMPOSITE) >
                colouredPixels(iiPlus, VideoColorMode::PIXEL_EXACT));
    }

    SECTION("...but a monochrome monitor has no chroma to show") {
        REQUIRE(colouredPixels(iiPlus, VideoColorMode::MONOCHROME) == 0);
    }
}

TEST_CASE("Mixed mode looks the same on both machines", "[machine][video]") {
    // Worth pinning because it surprises people, including the person who
    // wrote the burst model.
    //
    // A //e does inhibit the burst on the four text rows of a mixed screen —
    // burstForScanline returns false for them. But the burst is not what the
    // decoders are handed: they get the colour killer, which works a field at
    // a time, and 160 of the 192 lines are graphics carrying burst, so the
    // killer never engages. The text rows are therefore decoded in colour
    // exactly like the graphics above them.
    //
    // The consequence is that inhibitsBurstInText only ever changes a *full
    // text* screen. In mixed mode the two machines are pixel-identical, and a
    // //e's bottom rows fringe just as a II+'s do. Making them differ would
    // need a colour killer that reacts within a field, which is not what a
    // real monitor does.
    if (!Emulator::isMachineRunnable(MachineId::AppleIIPlus)) return;

    auto render = [](MachineId id, bool text, bool mixed) {
        auto e = std::make_unique<Emulator>(id);
        e->init();
        e->getMMU().read(text ? 0xC051 : 0xC050);  // TEXT / GRAPHICS
        e->getMMU().read(mixed ? 0xC053 : 0xC052); // MIXED on / off
        e->getMMU().read(0xC057);                  // HIRES

        // Something dense enough to carry colour across the graphics area.
        for (int line = 0; line < 192; line++) {
            for (int col = 0; col < 40; col++) {
                const uint16_t addr = static_cast<uint16_t>(
                    0x2000 + ((line & 7) << 10) + (((line >> 3) & 7) << 7) +
                    ((line >> 6) * 40) + col);
                e->getMMU().writeRAM(addr, 0x55);
            }
        }
        const char *word = "STATUS LINE";
        for (int row = 20; row < 24; row++) {
            const uint16_t base = static_cast<uint16_t>(
                0x400 + ((row & 7) * 0x80) + ((row >> 3) * 40));
            for (int i = 0; word[i]; i++) {
                e->getMMU().writeRAM(static_cast<uint16_t>(base + i),
                                     static_cast<uint8_t>(word[i] | 0x80));
            }
        }
        e->getVideo().setColorMode(VideoColorMode::COMPOSITE);
        e->getVideo().forceRenderFrame();
        return e;
    };

    // Colour in the bottom four text rows: framebuffer lines 320-383.
    auto colourInTextRows = [](Emulator &e) {
        const uint8_t *fb = e.getVideo().getFramebuffer();
        const int width = e.getMachine().display.pixelWidth;
        int coloured = 0;
        for (int line = 320; line < 384; line++) {
            for (int x = 0; x < 280; x++) {
                const size_t o = static_cast<size_t>(line) * width * 4 +
                                 static_cast<size_t>(x) * 4;
                const int r = fb[o], g = fb[o + 1], b = fb[o + 2];
                if (std::max(r, std::max(g, b)) - std::min(r, std::min(g, b)) > 40)
                    coloured++;
            }
        }
        return coloured;
    };

    SECTION("mixed hi-res: both fringe, and by the same amount") {
        auto iie = render(MachineId::AppleIIe, false, true);
        auto iiPlus = render(MachineId::AppleIIPlus, false, true);

        REQUIRE(iie->getVideo().isChromaEnabled());
        REQUIRE(iiPlus->getVideo().isChromaEnabled());
        REQUIRE(colourInTextRows(*iie) > 0);
        REQUIRE(colourInTextRows(*iie) == colourInTextRows(*iiPlus));
    }

    SECTION("full text is where the two machines part company") {
        auto iie = render(MachineId::AppleIIe, true, false);
        auto iiPlus = render(MachineId::AppleIIPlus, true, false);

        // No burst anywhere on a //e, so the killer engages and text is grey.
        REQUIRE_FALSE(iie->getVideo().isChromaEnabled());
        REQUIRE(colourInTextRows(*iie) == 0);

        // A II+ sends a reference on every line whatever the mode.
        REQUIRE(iiPlus->getVideo().isChromaEnabled());
        REQUIRE(colourInTextRows(*iiPlus) > 0);
    }
}

TEST_CASE("A II+ has a slot 0 and a //e does not", "[machine]") {
    // slots_ is indexed by slot number and has room for slot 0; which machines
    // actually have one is the profile's answer.
    MMU iiPlus(machineProfile(MachineId::AppleIIPlus));
    MMU iie(machineProfile(MachineId::AppleIIe));

    REQUIRE(iiPlus.isSlotEmpty(0));
    auto card = std::make_unique<ThunderclockCard>();
    REQUIRE(iiPlus.insertCard(0, std::move(card)) == nullptr);
    REQUIRE_FALSE(iiPlus.isSlotEmpty(0));
    REQUIRE(iiPlus.getCard(0) != nullptr);

    SECTION("a //e refuses slot 0 and hands the card back") {
        auto rejected = std::make_unique<ThunderclockCard>();
        auto *raw = rejected.get();
        auto returned = iie.insertCard(0, std::move(rejected));
        REQUIRE(returned.get() == raw);
        REQUIRE(iie.isSlotEmpty(0));
    }

    SECTION("both machines still take an ordinary slot") {
        REQUIRE(iie.insertCard(5, std::make_unique<ThunderclockCard>()) == nullptr);
        REQUIRE_FALSE(iie.isSlotEmpty(5));
        REQUIRE(iiPlus.insertCard(5, std::make_unique<ThunderclockCard>()) == nullptr);
        REQUIRE_FALSE(iiPlus.isSlotEmpty(5));
    }

    SECTION("a slot neither machine has is still refused") {
        auto tooHigh = std::make_unique<ThunderclockCard>();
        auto *raw = tooHigh.get();
        REQUIRE(iiPlus.insertCard(8, std::move(tooHigh)).get() == raw);
    }
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
