/*
 * test_joyport.cpp - Unit tests for the Sirius Joyport
 *
 * The table these pin is the one the Joyport manual's own BASIC test program
 * walks: with AN1 off, PB1 is left and PB2 is right; POKE -16293 (AN1 on) and
 * the same two inputs become up and down. AN0 chooses between the two sticks.
 * Every switch is active low.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "joyport.hpp"

using namespace a2e;

static constexpr uint8_t OPEN = 0x80;   // switch released
static constexpr uint8_t CLOSED = 0x00; // switch pressed

TEST_CASE("Idle sticks read high on every input", "[joyport]") {
    Joyport jp;
    for (int pb = 0; pb < 3; ++pb) {
        for (int an = 0; an < 4; ++an) {
            REQUIRE(jp.readPushButton(pb, an & 1, an & 2) == OPEN);
        }
    }
}

TEST_CASE("AN1 off reports the horizontal pair", "[joyport]") {
    Joyport jp;
    jp.setStickState(0, Joyport::LEFT);

    REQUIRE(jp.readPushButton(1, false, false) == CLOSED); // left
    REQUIRE(jp.readPushButton(2, false, false) == OPEN);   // right
    // The same switch must not show up on the vertical pair.
    REQUIRE(jp.readPushButton(1, false, true) == OPEN);
    REQUIRE(jp.readPushButton(2, false, true) == OPEN);

    jp.setStickState(0, Joyport::RIGHT);
    REQUIRE(jp.readPushButton(2, false, false) == CLOSED);
    REQUIRE(jp.readPushButton(1, false, false) == OPEN);
}

TEST_CASE("AN1 on reports the vertical pair", "[joyport]") {
    Joyport jp;
    jp.setStickState(0, Joyport::UP);
    REQUIRE(jp.readPushButton(1, false, true) == CLOSED); // up
    REQUIRE(jp.readPushButton(2, false, true) == OPEN);   // down

    jp.setStickState(0, Joyport::DOWN);
    REQUIRE(jp.readPushButton(2, false, true) == CLOSED);
    REQUIRE(jp.readPushButton(1, false, true) == OPEN);
}

TEST_CASE("Fire is on PB0 regardless of AN1", "[joyport]") {
    Joyport jp;
    jp.setStickState(0, Joyport::FIRE);
    REQUIRE(jp.readPushButton(0, false, false) == CLOSED);
    REQUIRE(jp.readPushButton(0, false, true) == CLOSED);
    // ...and belongs to the selected stick only.
    REQUIRE(jp.readPushButton(0, true, false) == OPEN);
}

TEST_CASE("AN0 selects the second stick", "[joyport]") {
    Joyport jp;
    jp.setStickState(0, Joyport::LEFT);
    jp.setStickState(1, Joyport::RIGHT | Joyport::FIRE);

    REQUIRE(jp.readPushButton(1, false, false) == CLOSED); // stick 1 left
    REQUIRE(jp.readPushButton(2, false, false) == OPEN);

    REQUIRE(jp.readPushButton(1, true, false) == OPEN);
    REQUIRE(jp.readPushButton(2, true, false) == CLOSED); // stick 2 right
    REQUIRE(jp.readPushButton(0, true, false) == CLOSED); // stick 2 fire
}

TEST_CASE("Diagonals close both pairs at once", "[joyport]") {
    Joyport jp;
    jp.setStickState(0, Joyport::UP | Joyport::RIGHT);
    REQUIRE(jp.readPushButton(2, false, false) == CLOSED); // right
    REQUIRE(jp.readPushButton(1, false, true) == CLOSED);  // up
    REQUIRE(jp.readPushButton(1, false, false) == OPEN);   // not left
    REQUIRE(jp.readPushButton(2, false, true) == OPEN);    // not down
}

TEST_CASE("reset releases both sticks", "[joyport]") {
    Joyport jp;
    jp.setStickState(0, Joyport::SWITCH_MASK);
    jp.setStickState(1, Joyport::SWITCH_MASK);
    jp.reset();
    REQUIRE(jp.stickState(0) == 0);
    REQUIRE(jp.stickState(1) == 0);
    REQUIRE(jp.readPushButton(0, false, false) == OPEN);
}

TEST_CASE("Out of range sticks and inputs are ignored", "[joyport]") {
    Joyport jp;
    jp.setStickState(-1, Joyport::FIRE);
    jp.setStickState(2, Joyport::FIRE);
    REQUIRE(jp.stickState(-1) == 0);
    REQUIRE(jp.stickState(2) == 0);
    REQUIRE(jp.readPushButton(3, false, false) == OPEN);
}

TEST_CASE("Bits outside the switch mask are discarded", "[joyport]") {
    Joyport jp;
    jp.setStickState(0, 0xFF);
    REQUIRE(jp.stickState(0) == Joyport::SWITCH_MASK);
}
