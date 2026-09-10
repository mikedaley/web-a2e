/*
 * test_emulator.cpp - Integration tests for the Emulator class
 *
 * Tests the full emulator coordinator including initialization, reset,
 * execution, memory access, video, beam position, soft switches,
 * slot management, screen text, disassembly, and speed control.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"

using namespace a2e;

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST_CASE("Emulator init does not crash", "[emulator][init]") {
    Emulator emu;
    REQUIRE_NOTHROW(emu.init());
}

TEST_CASE("Emulator PC points to reset vector destination after init", "[emulator][init]") {
    Emulator emu;
    emu.init();

    // After reset, the 65C02 reads the reset vector at $FFFC/$FFFD and sets
    // PC to that address.  The Apple IIe ROM reset vector points into the
    // $FA00-$FFFF range (monitor / reset handler).
    uint16_t pc = emu.getPC();
    REQUIRE(pc >= 0xC000);
    REQUIRE(pc <= 0xFFFF);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_CASE("Emulator reset returns PC to reset vector", "[emulator][reset]") {
    Emulator emu;
    emu.init();

    uint16_t pcAfterInit = emu.getPC();

    // Run some cycles to move PC away from the reset address
    emu.runCycles(1000);
    REQUIRE(emu.getPC() != pcAfterInit);

    // Reset should return PC to the same reset vector destination
    emu.reset();
    REQUIRE(emu.getPC() == pcAfterInit);
}

TEST_CASE("Emulator warmReset preserves memory but resets PC", "[emulator][reset]") {
    Emulator emu;
    emu.init();

    uint16_t pcAfterInit = emu.getPC();

    // Write a known value into low RAM
    emu.writeMemory(0x0300, 0xAB);
    REQUIRE(emu.readMemory(0x0300) == 0xAB);

    // Run some cycles to move PC
    emu.runCycles(1000);

    // Warm reset: memory preserved, PC returns to reset vector
    emu.warmReset();
    REQUIRE(emu.getPC() == pcAfterInit);
    REQUIRE(emu.readMemory(0x0300) == 0xAB);
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

TEST_CASE("Emulator runCycles advances cycles and PC", "[emulator][execution]") {
    Emulator emu;
    emu.init();

    uint16_t pcBefore = emu.getPC();
    uint64_t cyclesBefore = emu.getTotalCycles();

    emu.runCycles(100);

    REQUIRE(emu.getTotalCycles() > cyclesBefore);
    REQUIRE(emu.getPC() != pcBefore);
}

TEST_CASE("Emulator stepInstruction executes one instruction", "[emulator][execution]") {
    Emulator emu;
    emu.init();

    uint64_t cyclesBefore = emu.getTotalCycles();
    uint16_t pcBefore = emu.getPC();

    emu.stepInstruction();

    // At least 2 cycles for the shortest 65C02 instruction, PC should have moved
    REQUIRE(emu.getTotalCycles() > cyclesBefore);
    REQUIRE(emu.getTotalCycles() <= cyclesBefore + 7); // max 7 cycles for any instruction
    REQUIRE(emu.getPC() != pcBefore);
}

// ---------------------------------------------------------------------------
// Pause
// ---------------------------------------------------------------------------

TEST_CASE("Emulator setPaused and isPaused", "[emulator][pause]") {
    Emulator emu;
    emu.init();

    REQUIRE_FALSE(emu.isPaused());

    emu.setPaused(true);
    REQUIRE(emu.isPaused());

    // runCycles should not advance when paused
    uint64_t cyclesBefore = emu.getTotalCycles();
    emu.runCycles(1000);
    REQUIRE(emu.getTotalCycles() == cyclesBefore);

    emu.setPaused(false);
    REQUIRE_FALSE(emu.isPaused());
}

// ---------------------------------------------------------------------------
// Memory access
// ---------------------------------------------------------------------------

TEST_CASE("Emulator writeMemory and readMemory round-trip", "[emulator][memory]") {
    Emulator emu;
    emu.init();

    emu.writeMemory(0x0400, 0x42);
    REQUIRE(emu.readMemory(0x0400) == 0x42);
}

TEST_CASE("Emulator peekMemory reads same as readMemory for normal RAM", "[emulator][memory]") {
    Emulator emu;
    emu.init();

    emu.writeMemory(0x0400, 0x55);
    REQUIRE(emu.peekMemory(0x0400) == emu.readMemory(0x0400));
}

// ---------------------------------------------------------------------------
// Video / Framebuffer
// ---------------------------------------------------------------------------

TEST_CASE("Emulator getFramebuffer returns non-null after init", "[emulator][video]") {
    Emulator emu;
    emu.init();

    const uint8_t* fb = emu.getFramebuffer();
    REQUIRE(fb != nullptr);
}

TEST_CASE("Emulator getFramebufferSize equals expected RGBA buffer size", "[emulator][video]") {
    Emulator emu;
    emu.init();

    // 560 * 384 * 4 (RGBA) = 860160
    REQUIRE(emu.getFramebufferSize() == 860160);
}

// ---------------------------------------------------------------------------
// Beam position
// ---------------------------------------------------------------------------

TEST_CASE("Emulator beam position returns valid scanline and hPos", "[emulator][beam]") {
    Emulator emu;
    emu.init();

    // Run a few cycles so beam position is somewhere meaningful
    emu.runCycles(200);

    int scanline = emu.getBeamScanline();
    int hPos = emu.getBeamHPos();

    REQUIRE(scanline >= 0);
    REQUIRE(scanline < 262);  // 262 scanlines per NTSC frame
    REQUIRE(hPos >= 0);
    REQUIRE(hPos < 65);       // 65 cycles per scanline
}

// ---------------------------------------------------------------------------
// Soft switches
// ---------------------------------------------------------------------------

TEST_CASE("Emulator getSoftSwitchState returns packed state", "[emulator][softswitch]") {
    Emulator emu;
    emu.init();

    // getSoftSwitchState returns a 64-bit packed value; just verify it is callable
    // and produces a reasonable value (all zeros except TEXT mode which is set by default)
    uint64_t state = emu.getSoftSwitchState();
    // TEXT mode bit (bit 0) should be set on a fresh init
    REQUIRE((state & 0x01) == 0x01);
}

// ---------------------------------------------------------------------------
// Slot management
// ---------------------------------------------------------------------------

TEST_CASE("Emulator slot management: isSlotEmpty and getSlotCardName", "[emulator][slots]") {
    Emulator emu;
    emu.init();

    SECTION("Slot 3 is never empty (built-in 80-column)") {
        REQUIRE_FALSE(emu.isSlotEmpty(3));
        REQUIRE(std::string(emu.getSlotCardName(3)) == "80col");
    }

    SECTION("Slot 6 has Disk II by default") {
        REQUIRE_FALSE(emu.isSlotEmpty(6));
        REQUIRE(std::string(emu.getSlotCardName(6)) == "disk2");
    }

    SECTION("Slot 4 has Mockingboard by default") {
        REQUIRE_FALSE(emu.isSlotEmpty(4));
        REQUIRE(std::string(emu.getSlotCardName(4)) == "mockingboard");
    }

    SECTION("Slot 1 is empty by default") {
        REQUIRE(emu.isSlotEmpty(1));
    }
}

// ---------------------------------------------------------------------------
// Screen text
// ---------------------------------------------------------------------------

TEST_CASE("Emulator readScreenText returns a string", "[emulator][screen]") {
    Emulator emu;
    emu.init();

    // Read full screen (24 rows x 40 cols)
    const char* text = emu.readScreenText(0, 0, 23, 39);
    REQUIRE(text != nullptr);
}

// ---------------------------------------------------------------------------
// Disassembly
// ---------------------------------------------------------------------------

TEST_CASE("Emulator disassembleAt returns non-empty string", "[emulator][disasm]") {
    Emulator emu;
    emu.init();

    // Disassemble at current PC (should be in ROM after reset)
    const char* disasm = emu.disassembleAt(emu.getPC());
    REQUIRE(disasm != nullptr);
    REQUIRE(std::string(disasm).length() > 0);
}

// ---------------------------------------------------------------------------
// Speed control
// ---------------------------------------------------------------------------

TEST_CASE("Emulator speed multiplier set and get", "[emulator][speed]") {
    Emulator emu;
    emu.init();

    REQUIRE(emu.getSpeedMultiplier() == 1);

    emu.setSpeedMultiplier(2);
    REQUIRE(emu.getSpeedMultiplier() == 2);

    emu.setSpeedMultiplier(8);
    REQUIRE(emu.getSpeedMultiplier() == 8);

    // Values are clamped to 1-8
    emu.setSpeedMultiplier(0);
    REQUIRE(emu.getSpeedMultiplier() == 1);

    emu.setSpeedMultiplier(100);
    REQUIRE(emu.getSpeedMultiplier() == 8);
}

TEST_CASE("Emulator speed multiplier survives reset", "[emulator][speed]") {
    // The multiplier is a host preference (the user's chosen clock speed, or a
    // paste boost in flight), not machine state, so a reboot must not silently
    // put the machine back to 1 MHz.
    Emulator emu;
    emu.init();

    emu.setSpeedMultiplier(4);
    emu.reset();
    REQUIRE(emu.getSpeedMultiplier() == 4);
}

// ---------------------------------------------------------------------------
// screenCodeToAscii
// ---------------------------------------------------------------------------

TEST_CASE("Emulator screenCodeToAscii converts known codes", "[emulator][screen]") {
    // Normal ASCII 'A' = 0xC1 in Apple II screen memory
    int result = Emulator::screenCodeToAscii(0xC1);
    REQUIRE(result == 'A');
}

// ---------------------------------------------------------------------------
// Paste / keyboard type-ahead buffer
// ---------------------------------------------------------------------------

namespace {

// Read a character the way a program does: $C000 for the code, $C010 to clear
// the strobe. Returns the key code without the strobe bit.
uint8_t readKeyLikeSoftware(Emulator &emu) {
    uint8_t key = emu.readMemory(0xC000);
    emu.readMemory(0xC010);
    return key & 0x7F;
}

// Let the machine run until the next pasted character reaches the keyboard.
// Pasted keys arrive spaced out in emulated time, so a test that reads them
// back to back has to let time pass in between, exactly as software does.
bool runUntilKeyWaiting(Emulator &emu, int maxCycles = 500000) {
    int spent = 0;
    while (spent < maxCycles) {
        if (!emu.isKeyboardReady()) return true;  // strobe set: a key is waiting
        emu.runCycles(1000);
        spent += 1000;
    }
    return !emu.isKeyboardReady();
}

// Read the next pasted character, waiting for it to arrive.
uint8_t nextPastedKey(Emulator &emu) {
    REQUIRE(runUntilKeyWaiting(emu));
    return readKeyLikeSoftware(emu);
}

} // namespace

TEST_CASE("Emulator paste buffer feeds the keyboard latch one key at a time",
          "[emulator][keyboard][paste]") {
    Emulator emu;
    emu.init();

    REQUIRE(emu.pasteText("HI") == 2);

    // The first character is in the latch immediately; the rest wait.
    REQUIRE(emu.pastePending() == 1);
    REQUIRE((emu.readMemory(0xC000) & 0x80) != 0);
    REQUIRE(readKeyLikeSoftware(emu) == 'H');

    // The next one follows once the machine has run for a moment.
    REQUIRE(emu.pastePending() == 1);
    REQUIRE(nextPastedKey(emu) == 'I');

    // Drained: the strobe stays clear. Bits 0-6 keep the last code, as real
    // hardware does, so it is bit 7 that says whether a key is waiting.
    REQUIRE(emu.isKeyboardReady());
    REQUIRE((emu.readMemory(0xC000) & 0x80) == 0);
}

TEST_CASE("Emulator paste buffer never overwrites an unread key",
          "[emulator][keyboard][paste]") {
    // The buffer exists so a fast producer cannot lose characters: nothing may
    // enter the latch while the strobe is still set.
    Emulator emu;
    emu.init();

    emu.pasteText("AB");
    REQUIRE((emu.readMemory(0xC000) & 0x7F) == 'A');

    emu.pasteText("CD"); // more text arriving mid-paste
    REQUIRE((emu.readMemory(0xC000) & 0x7F) == 'A');
    REQUIRE(emu.pastePending() == 3);

    REQUIRE(readKeyLikeSoftware(emu) == 'A');
    REQUIRE(nextPastedKey(emu) == 'B');
    REQUIRE(nextPastedKey(emu) == 'C');
    REQUIRE(nextPastedKey(emu) == 'D');
    REQUIRE(emu.pastePending() == 0);
}

TEST_CASE("Emulator paste releases any-key-down when it drains",
          "[emulator][keyboard][paste]") {
    // AKD ($C010 bit 7) must not stay latched after the last pasted character
    // is read, or every read of $C010 reports a key held for the rest of the
    // session.
    Emulator emu;
    emu.init();

    emu.pasteText("A");
    emu.readMemory(0xC000);
    REQUIRE((emu.readMemory(0xC010) & 0x80) == 0); // released with the buffer

    // A cancelled paste releases it too.
    emu.pasteText("XY");
    emu.readMemory(0xC000);
    emu.clearPasteBuffer();
    REQUIRE(emu.pastePending() == 0);
    REQUIRE((emu.readMemory(0xC010) & 0x80) == 0);
}

TEST_CASE("Emulator paste accepts resolved key codes and skips unmappable text",
          "[emulator][keyboard][paste]") {
    Emulator emu;
    emu.init();

    // Ctrl-C as a raw code, the way a {ctrl-c} token arrives.
    emu.pasteKey(0x03);
    REQUIRE(readKeyLikeSoftware(emu) == 0x03);

    // Characters with no Apple II equivalent are dropped, not queued as junk.
    // The pound sign is multi-byte UTF-8, so a byte-wise reader would also
    // queue two stray continuation bytes here.
    REQUIRE(emu.pasteText("A\xC2\xA3" "B") == 2);
    REQUIRE(nextPastedKey(emu) == 'A');
    REQUIRE(nextPastedKey(emu) == 'B');
    REQUIRE(emu.pastePending() == 0);
}

TEST_CASE("Emulator reset discards a paste in flight",
          "[emulator][keyboard][paste]") {
    Emulator emu;
    emu.init();

    emu.pasteText("LONG TEXT");
    emu.reset();
    REQUIRE(emu.pastePending() == 0);
    REQUIRE(emu.isKeyboardReady());
}

TEST_CASE("Emulator reports any-key-down for real typing and clears it",
          "[emulator][keyboard][akd]") {
    // AKD is $C010 bit 7. It used to be set by the first keystroke of the
    // session and never cleared, because handleRawKeyUp only ever touched the
    // Apple buttons.
    Emulator emu;
    emu.init();
    REQUIRE((emu.readMemory(0xC010) & 0x80) == 0);

    emu.handleRawKeyDown(65, false, false, false, false, false, 0); // 'A' down
    REQUIRE((emu.peekMemory(0xC010) & 0x80) != 0);

    // The key code is still waiting to be read; only AKD follows the release.
    REQUIRE((emu.readMemory(0xC000) & 0x7F) == 'a');

    emu.handleRawKeyUp(65, false, false, false, false, 0);
    REQUIRE((emu.peekMemory(0xC010) & 0x80) == 0);
}

TEST_CASE("Emulator holds any-key-down while a second key is still pressed",
          "[emulator][keyboard][akd]") {
    Emulator emu;
    emu.init();

    emu.handleRawKeyDown(65, false, false, false, false, false, 0);
    emu.handleRawKeyDown(66, false, false, false, false, false, 0);
    emu.handleRawKeyUp(65, false, false, false, false, 0);
    REQUIRE((emu.peekMemory(0xC010) & 0x80) != 0);

    emu.handleRawKeyUp(66, false, false, false, false, 0);
    REQUIRE((emu.peekMemory(0xC010) & 0x80) == 0);
}

TEST_CASE("Emulator releases any-key-down when the host loses focus",
          "[emulator][keyboard][akd]") {
    Emulator emu;
    emu.init();

    emu.handleRawKeyDown(65, false, false, false, false, false, 0);
    emu.releaseModifiers(); // window blur
    REQUIRE((emu.peekMemory(0xC010) & 0x80) == 0);
}

TEST_CASE("Emulator any-key-down covers a held key and a paste at once",
          "[emulator][keyboard][akd][paste]") {
    // The two sources are independent: a paste character waiting in the latch
    // holds AKD, and so does a physically held key. Neither may clear it while
    // the other is still asserting it.
    Emulator emu;
    emu.init();

    emu.handleRawKeyDown(65, false, false, false, false, false, 0); // 'A' held
    emu.pasteText("Z");
    REQUIRE((emu.peekMemory(0xC010) & 0x80) != 0);

    // The typed key is in the latch, so the pasted one waits its turn rather
    // than overwriting it.
    REQUIRE((emu.readMemory(0xC000) & 0x7F) == 'a');
    emu.readMemory(0xC010);
    REQUIRE(runUntilKeyWaiting(emu));
    REQUIRE((emu.readMemory(0xC000) & 0x7F) == 'Z');

    // Releasing the held key does not clear AKD while the pasted character is
    // still sitting unread.
    emu.handleRawKeyUp(65, false, false, false, false, 0);
    REQUIRE((emu.peekMemory(0xC010) & 0x80) != 0);

    // ...and reading it clears AKD, because nothing is holding it any more.
    emu.readMemory(0xC010);
    REQUIRE((emu.peekMemory(0xC010) & 0x80) == 0);
}

TEST_CASE("Emulator paste survives a keyboard flush after each character",
          "[emulator][keyboard][paste]") {
    // `POKE -16368,0` / `STA $C010` — flushing the keyboard before waiting for
    // the next key — is everywhere in Apple II software. When a pasted key
    // appeared the instant the strobe cleared, the flush that followed the read
    // found a fresh character and threw it away, so a paste into any program
    // that flushes lost every second character. A person typing leaves nothing
    // for that flush to eat, and now neither does a paste.
    Emulator emu;
    emu.init();

    const std::string sent = "ABCDEFGH";
    REQUIRE(emu.pasteText(sent.c_str()) == sent.size());

    std::string got;
    for (size_t i = 0; i < sent.size(); ++i) {
        REQUIRE(runUntilKeyWaiting(emu));
        got.push_back(static_cast<char>(emu.readMemory(0xC000) & 0x7F));
        emu.readMemory(0xC010); // read: clear the strobe
        emu.writeMemory(0xC010, 0); // ...and flush, the way the program would
    }

    REQUIRE(got == sent);
    REQUIRE(emu.pastePending() == 0);
}

TEST_CASE("Emulator paste gives the machine longer after a carriage return",
          "[emulator][keyboard][paste]") {
    // A line takes work to digest — Applesoft tokenises it, DOS and
    // BASIC.SYSTEM run their command parsers — and those paths flush the
    // keyboard too, so the gap after a return is deliberately much longer than
    // the gap between two ordinary characters.
    Emulator emu;
    emu.init();

    auto cyclesUntilNextKey = [&](char first) {
        emu.clearPasteBuffer();
        emu.readMemory(0xC010); // drain anything left in the latch
        std::string text;
        text.push_back(first);
        text.push_back('Z');
        emu.pasteText(text.c_str());
        REQUIRE(runUntilKeyWaiting(emu));
        emu.readMemory(0xC000);
        emu.readMemory(0xC010);
        uint64_t start = emu.getTotalCycles();
        REQUIRE(runUntilKeyWaiting(emu));
        return emu.getTotalCycles() - start;
    };

    const uint64_t afterChar = cyclesUntilNextKey('A');
    const uint64_t afterReturn = cyclesUntilNextKey('\r');

    REQUIRE(afterChar >= Emulator::PASTE_KEY_GAP_CYCLES);
    REQUIRE(afterReturn >= Emulator::PASTE_LINE_GAP_CYCLES);
    REQUIRE(afterReturn > afterChar * 2);
}


// ============================================================================
// Game I/O connector: Apple joystick vs Sirius Joyport
// ============================================================================

TEST_CASE("Game port defaults to the Apple joystick", "[emulator][joyport]") {
    Emulator emu;
    emu.init();
    REQUIRE(emu.gamePortDevice() == GamePortDevice::AppleJoystick);

    // The Apple keys drive PB0/PB1 active high, as they always have.
    REQUIRE((emu.readMemory(0xC061) & 0x80) == 0x00);
    emu.setButton(0, true);
    REQUIRE((emu.readMemory(0xC061) & 0x80) == 0x80);
}

TEST_CASE("Joyport drives the pushbuttons through the annunciators",
          "[emulator][joyport]") {
    // This is the manual's test program in miniature: select a stick with AN0,
    // an axis pair with AN1, and read $C061-$C063 — active low.
    Emulator emu;
    emu.init();
    emu.setGamePortDevice(GamePortDevice::SiriusJoyport);

    emu.readMemory(0xC058); // AN0 off - stick 1
    emu.readMemory(0xC05A); // AN1 off - horizontal

    // Nothing held: every line reads high, the opposite of an Apple button.
    REQUIRE((emu.readMemory(0xC061) & 0x80) == 0x80);
    REQUIRE((emu.readMemory(0xC062) & 0x80) == 0x80);
    REQUIRE((emu.readMemory(0xC063) & 0x80) == 0x80);

    emu.setJoyportStick(0, Joyport::LEFT | Joyport::FIRE);
    REQUIRE((emu.readMemory(0xC061) & 0x80) == 0x00); // fire
    REQUIRE((emu.readMemory(0xC062) & 0x80) == 0x00); // left
    REQUIRE((emu.readMemory(0xC063) & 0x80) == 0x80); // not right

    emu.readMemory(0xC05B); // AN1 on - vertical
    REQUIRE((emu.readMemory(0xC062) & 0x80) == 0x80); // left is not up
    emu.setJoyportStick(0, Joyport::DOWN);
    REQUIRE((emu.readMemory(0xC063) & 0x80) == 0x00); // down

    // Stick 2 lives behind AN0 and must not answer for stick 1.
    emu.setJoyportStick(1, Joyport::UP);
    REQUIRE((emu.readMemory(0xC062) & 0x80) == 0x80);
    emu.readMemory(0xC059); // AN0 on - stick 2
    REQUIRE((emu.readMemory(0xC062) & 0x80) == 0x00);
    REQUIRE((emu.readMemory(0xC063) & 0x80) == 0x80);
}

TEST_CASE("Joyport ignores the Apple keys, and switching back restores them",
          "[emulator][joyport]") {
    Emulator emu;
    emu.init();
    emu.readMemory(0xC058);
    emu.readMemory(0xC05A);

    emu.setButton(0, true);
    emu.setGamePortDevice(GamePortDevice::SiriusJoyport);
    // The Joyport drives PB0 itself, and switching released the held button.
    REQUIRE((emu.readMemory(0xC061) & 0x80) == 0x80);

    emu.setJoyportStick(0, Joyport::FIRE);
    emu.setGamePortDevice(GamePortDevice::AppleJoystick);
    REQUIRE(emu.getJoyportStick(0) == 0);
    REQUIRE((emu.readMemory(0xC061) & 0x80) == 0x00);
}

TEST_CASE("Reset releases the sticks but keeps the chosen device",
          "[emulator][joyport]") {
    // The device is a host preference like the speed multiplier: a reboot must
    // not silently unplug the Joyport a game is being played with.
    Emulator emu;
    emu.init();
    emu.setGamePortDevice(GamePortDevice::SiriusJoyport);
    emu.setJoyportStick(1, Joyport::RIGHT);

    emu.reset();

    REQUIRE(emu.gamePortDevice() == GamePortDevice::SiriusJoyport);
    REQUIRE(emu.getJoyportStick(1) == 0);
}
