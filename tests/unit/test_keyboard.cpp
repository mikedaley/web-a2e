/*
 * test_keyboard.cpp - Unit tests for Apple IIe keyboard input handling
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "keyboard.hpp"

using namespace a2e;

// ============================================================================
// Basic key translation (handleKeyDown)
// ============================================================================

TEST_CASE("handleKeyDown returns translated Apple II keycode", "[keyboard][basic]") {
    Keyboard kb;
    // 'A' key (browser keycode 65) with no modifiers
    // translateKeycode maps 65 -> 0x61 ('a' lowercase)
    int result = kb.handleKeyDown(65, false, false, false, false, false);
    // Without caps lock or shift, should return lowercase 'a' = 0x61
    CHECK(result == 0x61);
}

TEST_CASE("Letter keys A-Z map to lowercase ASCII without modifiers", "[keyboard][letters]") {
    Keyboard kb;
    // Browser keycodes 65-90 for A-Z
    for (int browserKey = 65; browserKey <= 90; browserKey++) {
        int result = kb.handleKeyDown(browserKey, false, false, false, false, false);
        int expected = browserKey + 32; // lowercase ASCII
        INFO("Browser keycode " << browserKey << " expected 0x" << std::hex << expected);
        CHECK(result == expected);
    }
}

// ============================================================================
// Shift modifier
// ============================================================================

TEST_CASE("Shift+letter gives uppercase ASCII", "[keyboard][shift]") {
    Keyboard kb;
    // Browser keycode 65 ('A') with shift
    int result = kb.handleKeyDown(65, true, false, false, false, false);
    CHECK(result == 0x41); // 'A' uppercase
}

TEST_CASE("Shift modifier uppercase for all letters", "[keyboard][shift]") {
    Keyboard kb;
    for (int browserKey = 65; browserKey <= 90; browserKey++) {
        int result = kb.handleKeyDown(browserKey, true, false, false, false, false);
        int expected = browserKey; // uppercase ASCII = same as browser keycode
        INFO("Browser keycode " << browserKey);
        CHECK(result == expected);
    }
}

TEST_CASE("Caps lock gives uppercase without shift", "[keyboard][capslock]") {
    Keyboard kb;
    int result = kb.handleKeyDown(65, false, false, false, false, true);
    CHECK(result == 0x41); // 'A' uppercase
}

// ============================================================================
// Control modifier
// ============================================================================

TEST_CASE("Ctrl+A gives control character 0x01", "[keyboard][ctrl]") {
    Keyboard kb;
    // Browser keycode 65 ('A'), ctrl pressed
    int result = kb.handleKeyDown(65, false, true, false, false, false);
    CHECK(result == 0x01);
}

TEST_CASE("Ctrl+letters produce control characters 0x01-0x1A", "[keyboard][ctrl]") {
    Keyboard kb;
    for (int browserKey = 65; browserKey <= 90; browserKey++) {
        int result = kb.handleKeyDown(browserKey, false, true, false, false, false);
        int expected = browserKey - 64; // Ctrl+A=1, Ctrl+B=2, ...
        INFO("Ctrl+" << (char)browserKey << " expected 0x" << std::hex << expected);
        CHECK(result == expected);
    }
}

// ============================================================================
// Open Apple / Closed Apple (button state)
// ============================================================================

TEST_CASE("Alt key sets Open Apple pressed state", "[keyboard][apple]") {
    Keyboard kb;
    CHECK(kb.isOpenApplePressed() == false);

    // Alt key down (browser keycode 18)
    kb.handleKeyDown(18, false, false, true, false, false);
    CHECK(kb.isOpenApplePressed() == true);
}

TEST_CASE("The Meta keys press no Apple key", "[keyboard][apple]") {
    // Which host key is an Apple key is the host's decision, and it sends the
    // answer as an Alt key. A Meta key reaching the core is one the host left
    // to the browser, so it must not press Closed Apple on the way past.
    Keyboard kb;
    CHECK(kb.handleKeyDown(91, false, false, false, true, false) == -1);
    CHECK(kb.handleKeyDown(93, false, false, false, true, false) == -1);
    CHECK(kb.isClosedApplePressed() == false);
    CHECK(kb.isOpenApplePressed() == false);
    CHECK_FALSE(kb.isAnyKeyDown());
}

// ============================================================================
// handleKeyUp clears button state
// ============================================================================

TEST_CASE("handleKeyUp clears Open Apple", "[keyboard][keyup]") {
    Keyboard kb;

    kb.handleKeyDown(18, false, false, true, false, false);
    CHECK(kb.isOpenApplePressed() == true);

    kb.handleKeyUp(18, false, false, true, false);
    CHECK(kb.isOpenApplePressed() == false);
}

TEST_CASE("handleKeyUp clears Closed Apple", "[keyboard][keyup]") {
    Keyboard kb;

    kb.handleKeyDown(18, false, false, true, false, false, Keyboard::LOCATION_RIGHT);
    CHECK(kb.isClosedApplePressed() == true);

    kb.handleKeyUp(18, false, false, false, false, Keyboard::LOCATION_RIGHT);
    CHECK(kb.isClosedApplePressed() == false);
}

// ============================================================================
// reset
// ============================================================================

TEST_CASE("reset clears modifier states", "[keyboard][reset]") {
    Keyboard kb;

    // Set both apple buttons
    kb.handleKeyDown(18, false, false, true, false, false, Keyboard::LOCATION_LEFT);
    kb.handleKeyDown(18, false, false, true, false, false, Keyboard::LOCATION_RIGHT);
    CHECK(kb.isOpenApplePressed() == true);
    CHECK(kb.isClosedApplePressed() == true);

    kb.reset();

    CHECK(kb.isOpenApplePressed() == false);
    CHECK(kb.isClosedApplePressed() == false);
}

// ============================================================================
// Special keys
// ============================================================================

TEST_CASE("Enter key maps to CR (0x0D)", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(13, false, false, false, false, false);
    CHECK(result == 0x0D);
}

TEST_CASE("Escape key maps to 0x1B", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(27, false, false, false, false, false);
    CHECK(result == 0x1B);
}

TEST_CASE("Space key maps to 0x20", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(32, false, false, false, false, false);
    CHECK(result == 0x20);
}

TEST_CASE("Left arrow maps to 0x08", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(37, false, false, false, false, false);
    CHECK(result == 0x08);
}

TEST_CASE("Right arrow maps to 0x15", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(39, false, false, false, false, false);
    CHECK(result == 0x15);
}

TEST_CASE("Up arrow maps to 0x0B", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(38, false, false, false, false, false);
    CHECK(result == 0x0B);
}

TEST_CASE("Down arrow maps to 0x0A", "[keyboard][special]") {
    Keyboard kb;
    int result = kb.handleKeyDown(40, false, false, false, false, false);
    CHECK(result == 0x0A);
}

// ============================================================================
// Unmapped keys return -1
// ============================================================================

TEST_CASE("Unmapped key returns -1", "[keyboard][unmapped]") {
    Keyboard kb;
    // F-keys and other non-mapped keys
    int result = kb.handleKeyDown(112, false, false, false, false, false); // F1
    CHECK(result == -1);
}

TEST_CASE("Pure modifier keys return -1", "[keyboard][unmapped]") {
    Keyboard kb;
    // Shift key (16) returns -1
    CHECK(kb.handleKeyDown(16, true, false, false, false, false) == -1);
    // Ctrl key (17) returns -1
    CHECK(kb.handleKeyDown(17, false, true, false, false, false) == -1);
    // Alt key (18) returns -1 (but sets Open Apple state)
    CHECK(kb.handleKeyDown(18, false, false, true, false, false) == -1);
}

// ============================================================================
// charToAppleKey
// ============================================================================

TEST_CASE("charToAppleKey converts printable ASCII", "[keyboard][charToAppleKey]") {
    CHECK(charToAppleKey(0x20) == 0x20); // space
    CHECK(charToAppleKey(0x41) == 0x41); // 'A'
    CHECK(charToAppleKey(0x61) == 0x61); // 'a'
    CHECK(charToAppleKey(0x7E) == 0x7E); // '~'
    CHECK(charToAppleKey(0x30) == 0x30); // '0'
}

TEST_CASE("charToAppleKey converts newline to CR", "[keyboard][charToAppleKey]") {
    CHECK(charToAppleKey(0x0A) == 0x0D); // LF -> CR
    CHECK(charToAppleKey(0x0D) == 0x0D); // CR -> CR
}

TEST_CASE("charToAppleKey converts tab", "[keyboard][charToAppleKey]") {
    CHECK(charToAppleKey(0x09) == 0x09);
}

TEST_CASE("charToAppleKey returns -1 for unmappable characters", "[keyboard][charToAppleKey]") {
    CHECK(charToAppleKey(0x00) == -1);
    CHECK(charToAppleKey(0x01) == -1);
    CHECK(charToAppleKey(0x7F) == -1);
    CHECK(charToAppleKey(0x100) == -1); // Beyond ASCII
}

// ============================================================================
// Shift+number row
// ============================================================================

TEST_CASE("Shift+number produces correct symbols", "[keyboard][shift_symbols]") {
    Keyboard kb;
    CHECK(kb.handleKeyDown(49, true, false, false, false, false) == 0x21); // 1 -> !
    CHECK(kb.handleKeyDown(50, true, false, false, false, false) == 0x40); // 2 -> @
    CHECK(kb.handleKeyDown(51, true, false, false, false, false) == 0x23); // 3 -> #
    CHECK(kb.handleKeyDown(48, true, false, false, false, false) == 0x29); // 0 -> )
    CHECK(kb.handleKeyDown(57, true, false, false, false, false) == 0x28); // 9 -> (
}

// ============================================================================
// Apple buttons from the Alt keys
//
// Both Alt keys report browser keycode 18, so the location is the only thing
// separating Open Apple from Closed Apple. The button states are derived from
// which modifiers are held rather than toggled directly, so a key-up naming
// the wrong side cannot leave a button stuck on.
// ============================================================================

namespace {

constexpr int ALT = 18;
constexpr int LEFT = Keyboard::LOCATION_LEFT;
constexpr int RIGHT = Keyboard::LOCATION_RIGHT;
constexpr int STANDARD = Keyboard::LOCATION_STANDARD;

// handleKeyDown(keycode, shift, ctrl, alt, meta, capsLock, location)
void altDown(Keyboard& kb, int location) {
    kb.handleKeyDown(ALT, false, false, true, false, false, location);
}

// handleKeyUp(keycode, shift, ctrl, alt, meta, location)
void altUp(Keyboard& kb, int location, bool anyAltStillHeld) {
    kb.handleKeyUp(ALT, false, false, anyAltStillHeld, false, location);
}

} // namespace

TEST_CASE("left Alt is Open Apple and right Alt is Closed Apple", "[keyboard][buttons]") {
    Keyboard kb;

    altDown(kb, LEFT);
    CHECK(kb.isOpenApplePressed());
    CHECK_FALSE(kb.isClosedApplePressed());

    altUp(kb, LEFT, false);
    kb.releaseModifiers();

    altDown(kb, RIGHT);
    CHECK_FALSE(kb.isOpenApplePressed());
    CHECK(kb.isClosedApplePressed());
}

TEST_CASE("an unspecified Alt location is treated as the left key", "[keyboard][buttons]") {
    Keyboard kb;

    // Falling back to left keeps Open Apple working rather than silently
    // producing Closed Apple on an event that did not report a side.
    altDown(kb, STANDARD);

    CHECK(kb.isOpenApplePressed());
    CHECK_FALSE(kb.isClosedApplePressed());
}

TEST_CASE("releasing one Alt leaves the other held", "[keyboard][buttons]") {
    Keyboard kb;

    altDown(kb, LEFT);
    altDown(kb, RIGHT);
    REQUIRE(kb.isOpenApplePressed());
    REQUIRE(kb.isClosedApplePressed());

    // Left goes up while right is still down.
    altUp(kb, LEFT, true);

    CHECK_FALSE(kb.isOpenApplePressed());
    CHECK(kb.isClosedApplePressed());
}

TEST_CASE("a key-up naming the wrong side still clears once Alt is released",
          "[keyboard][buttons]") {
    Keyboard kb;

    altDown(kb, LEFT);
    altDown(kb, RIGHT);

    // Both releases arrive claiming the same side, which hosts do when two
    // instances of a modifier are held. The first mis-attributes...
    altUp(kb, LEFT, true);
    // ...but the last one reports no Alt remaining, which is authoritative.
    altUp(kb, LEFT, false);

    CHECK_FALSE(kb.isOpenApplePressed());
    CHECK_FALSE(kb.isClosedApplePressed());
}

TEST_CASE("A Meta key-up leaves a held Alt alone", "[keyboard][buttons]") {
    Keyboard kb;

    altDown(kb, RIGHT);
    kb.handleKeyDown(91, false, false, true, true, false);
    kb.handleKeyUp(91, false, false, true, false);

    CHECK(kb.isClosedApplePressed());
}

TEST_CASE("releaseModifiers drops every held button", "[keyboard][buttons]") {
    Keyboard kb;

    altDown(kb, LEFT);
    altDown(kb, RIGHT);
    kb.handleKeyDown(91, false, false, true, true, false);

    // A key held while the host loses focus never delivers its key-up.
    kb.releaseModifiers();

    CHECK_FALSE(kb.isOpenApplePressed());
    CHECK_FALSE(kb.isClosedApplePressed());
}

TEST_CASE("Alt keys generate no character", "[keyboard][buttons]") {
    Keyboard kb;

    CHECK(kb.handleKeyDown(ALT, false, false, true, false, false, LEFT) == -1);
    CHECK(kb.handleKeyDown(ALT, false, false, true, false, false, RIGHT) == -1);
}

TEST_CASE("state converges when the last Alt is released, whatever the location",
          "[keyboard][buttons]") {
    Keyboard kb;

    altDown(kb, LEFT);
    altDown(kb, RIGHT);

    // macOS browsers deliver no event at all when one of two held Option keys
    // is released, then a single key-up naming an arbitrary side once both are
    // up. Only `alt` being false is dependable, and it must clear everything.
    altUp(kb, LEFT, false);

    CHECK_FALSE(kb.isOpenApplePressed());
    CHECK_FALSE(kb.isClosedApplePressed());
}

// ============================================================================
// AKD — any key down ($C010 bit 7)
// ============================================================================

TEST_CASE("AKD is asserted while a key is held and released on key-up",
          "[keyboard][akd]") {
    Keyboard kb;
    REQUIRE_FALSE(kb.isAnyKeyDown());

    kb.handleKeyDown(65, false, false, false, false, false); // 'A' down
    REQUIRE(kb.isAnyKeyDown());

    kb.handleKeyUp(65, false, false, false, false);
    REQUIRE_FALSE(kb.isAnyKeyDown());
}

TEST_CASE("AKD stays asserted until the last of several keys is released",
          "[keyboard][akd]") {
    Keyboard kb;
    kb.handleKeyDown(65, false, false, false, false, false); // 'A'
    kb.handleKeyDown(66, false, false, false, false, false); // 'B'
    REQUIRE(kb.isAnyKeyDown());

    kb.handleKeyUp(65, false, false, false, false);
    REQUIRE(kb.isAnyKeyDown()); // 'B' is still down

    kb.handleKeyUp(66, false, false, false, false);
    REQUIRE_FALSE(kb.isAnyKeyDown());
}

TEST_CASE("Auto-repeat does not double-count a held key", "[keyboard][akd]") {
    // A held key repeats key-down events; one key-up must still clear AKD.
    Keyboard kb;
    for (int i = 0; i < 5; ++i) {
        kb.handleKeyDown(65, false, false, false, false, false);
    }
    kb.handleKeyUp(65, false, false, false, false);
    REQUIRE_FALSE(kb.isAnyKeyDown());
}

TEST_CASE("Modifiers and unmapped keys do not assert AKD", "[keyboard][akd]") {
    // Shift, Control, Caps Lock and the Apple buttons are separate lines on
    // real hardware, not keys in the matrix.
    Keyboard kb;
    kb.handleKeyDown(16, true, false, false, false, false);  // Shift
    kb.handleKeyDown(17, false, true, false, false, false);  // Ctrl
    kb.handleKeyDown(18, false, false, true, false, false, Keyboard::LOCATION_LEFT);
    kb.handleKeyDown(91, false, false, false, true, false);  // Meta
    REQUIRE_FALSE(kb.isAnyKeyDown());
    REQUIRE(kb.isOpenApplePressed());
}

TEST_CASE("A key-up that never arrives is cleared by losing focus",
          "[keyboard][akd]") {
    // A key held while the host loses focus delivers no key-up, which would
    // otherwise leave AKD high for the rest of the session.
    Keyboard kb;
    kb.handleKeyDown(65, false, false, false, false, false);
    REQUIRE(kb.isAnyKeyDown());

    kb.releaseModifiers();
    REQUIRE_FALSE(kb.isAnyKeyDown());
}

TEST_CASE("Shift released before the key still clears AKD", "[keyboard][akd]") {
    // Tracking is by browser keycode, so the modifier state at release time
    // cannot strand a key: 'A' pressed shifted and released unshifted is the
    // same key going up.
    Keyboard kb;
    kb.handleKeyDown(65, true, false, false, false, false);
    kb.handleKeyUp(65, false, false, false, false);
    REQUIRE_FALSE(kb.isAnyKeyDown());
}

// ============================================================================
// The rest of the keyboard: Delete, the keypad, Control with punctuation
// ============================================================================

TEST_CASE("Forward Delete is the Apple's DELETE key", "[keyboard][special]") {
    // Backspace is left arrow ($08), which is what Applesoft's line editor
    // wants; the key marked DELETE on a //e, //c and IIgs sends $7F, and
    // ProDOS editors and GS/OS ask for it.
    Keyboard kb;
    CHECK(kb.handleKeyDown(46, false, false, false, false, false) == 0x7F);
    CHECK(kb.handleKeyDown(8, false, false, false, false, false) == 0x08);
}

TEST_CASE("The numeric keypad types what the number row types", "[keyboard][keypad]") {
    Keyboard kb;
    for (int i = 0; i <= 9; i++) {
        CHECK(kb.handleKeyDown(96 + i, false, false, false, false, false) == 0x30 + i);
    }
    CHECK(kb.handleKeyDown(106, false, false, false, false, false) == 0x2A); // *
    CHECK(kb.handleKeyDown(107, false, false, false, false, false) == 0x2B); // +
    CHECK(kb.handleKeyDown(109, false, false, false, false, false) == 0x2D); // -
    CHECK(kb.handleKeyDown(110, false, false, false, false, false) == 0x2E); // .
    CHECK(kb.handleKeyDown(111, false, false, false, false, false) == 0x2F); // /
    // Shift does nothing to a keypad key.
    CHECK(kb.handleKeyDown(97, true, false, false, false, false) == 0x31);
    // A keypad key is a key: it asserts AKD like any other.
    CHECK(kb.isAnyKeyDown());
}

TEST_CASE("Control with punctuation gives the codes below $20", "[keyboard][ctrl]") {
    // The keyboard encoder clears bit 6 of @ [ \ ] ^ _ as it does of a
    // letter: Ctrl-@ is NUL, Ctrl-[ is Escape, Ctrl-^ is $1E, Ctrl-_ is $1F.
    Keyboard kb;
    CHECK(kb.handleKeyDown(219, false, true, false, false, false) == 0x1B); // Ctrl-[
    CHECK(kb.handleKeyDown(220, false, true, false, false, false) == 0x1C); // Ctrl-backslash
    CHECK(kb.handleKeyDown(221, false, true, false, false, false) == 0x1D); // Ctrl-]
    // The 2, 6 and - keys read as @, ^ and _ under Control with or without
    // Shift: Ctrl-2 is NUL on a //e, and $C000 then reads $80.
    CHECK(kb.handleKeyDown(50, false, true, false, false, false) == 0x00);  // Ctrl-2 = Ctrl-@
    CHECK(kb.handleKeyDown(50, true, true, false, false, false) == 0x00);   // Ctrl-Shift-2 too
    CHECK(kb.handleKeyDown(54, false, true, false, false, false) == 0x1E);  // Ctrl-6 = Ctrl-^
    CHECK(kb.handleKeyDown(189, false, true, false, false, false) == 0x1F); // Ctrl-- = Ctrl-_
    // Anything else is left alone: Ctrl-1 is still '1'.
    CHECK(kb.handleKeyDown(49, false, true, false, false, false) == 0x31);
    CHECK(kb.handleKeyDown(188, false, true, false, false, false) == 0x2C);
}

TEST_CASE("Control-Shift-letter is the same control character", "[keyboard][ctrl]") {
    Keyboard kb;
    CHECK(kb.handleKeyDown(65, true, true, false, false, false) == 0x01);
    CHECK(kb.handleKeyDown(65, false, true, false, false, true) == 0x01);
}

// ============================================================================
// A keyboard with no lower case (the II+)
// ============================================================================

TEST_CASE("An upper-case-only keyboard types capitals whatever is held", "[keyboard][iiplus]") {
    Keyboard kb;
    kb.setUppercaseOnly(true);
    CHECK(kb.handleKeyDown(65, false, false, false, false, false) == 0x41);
    CHECK(kb.handleKeyDown(65, true, false, false, false, false) == 0x41);
    CHECK(kb.handleKeyDown(65, false, false, false, false, true) == 0x41);
    CHECK(kb.handleKeyDown(65, true, false, false, false, true) == 0x41);
    // Shifted symbols and control characters are unaffected.
    CHECK(kb.handleKeyDown(49, true, false, false, false, false) == 0x21);
    CHECK(kb.handleKeyDown(65, false, true, false, false, false) == 0x01);
    // The default keyboard still has its lower case.
    kb.setUppercaseOnly(false);
    CHECK(kb.handleKeyDown(65, false, false, false, false, false) == 0x61);
}
