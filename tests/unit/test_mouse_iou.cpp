/*
 * test_mouse_iou.cpp - Unit tests for a //c's IOU mouse
 *
 * The mouse a //c has is not a card and has no protocol: it is a dozen soft
 * switches and an interrupt per unit of travel. What is pinned here is that
 * each switch does what the //c Technical Reference says, that travel arrives
 * one step at a time rather than all at once, and that the two addresses the
 * firmware shares with a //e read the way a //c's firmware expects.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "mouse_iou.hpp"

using namespace a2e;

namespace {
constexpr uint8_t RD_X_INT = 0x15;
constexpr uint8_t RD_Y_INT = 0x17;
constexpr uint8_t RD_XY_MASK = 0x40;
constexpr uint8_t RD_VBL_MASK = 0x41;
constexpr uint8_t RD_X_EDGE = 0x42;
constexpr uint8_t RD_Y_EDGE = 0x43;
constexpr uint8_t RST_XY = 0x48;
constexpr uint8_t DIS_XY = 0x58;
constexpr uint8_t ENB_XY = 0x59;
constexpr uint8_t DIS_VBL = 0x5A;
constexpr uint8_t ENB_VBL = 0x5B;
constexpr uint8_t X_EDGE_RISING = 0x5C;
constexpr uint8_t X_EDGE_FALLING = 0x5D;
constexpr uint8_t Y_EDGE_RISING = 0x5E;
constexpr uint8_t Y_EDGE_FALLING = 0x5F;
constexpr uint8_t RD_BUTTON = 0x63;
constexpr uint8_t RD_X1 = 0x66;
constexpr uint8_t RD_Y1 = 0x67;

constexpr bool IOU_ENABLED = false; // IOUDIS off: the mouse owns $C058-$C05F
constexpr bool IOU_DISABLED = true;

// Run the mouse forward far enough to release every step it is holding, the
// way a machine servicing interrupts would: clear the flag, let the next go.
int drain(MouseIOU &mouse, int limit = 200) {
    int steps = 0;
    uint64_t cycle = 0;
    for (int i = 0; i < limit; i++) {
        cycle += 512;
        mouse.update(cycle, false);
        if (mouse.xInterruptPending() || mouse.yInterruptPending()) {
            steps++;
            mouse.read(RST_XY);
        }
    }
    return steps;
}
} // namespace

TEST_CASE("The mouse owns its switches, and shares one group", "[mouse]") {
    MouseIOU mouse;

    // Its own, whatever else is happening.
    for (uint8_t reg : {RD_X_INT, RD_Y_INT, RD_XY_MASK, RD_VBL_MASK, RD_X_EDGE,
                        RD_Y_EDGE, RST_XY, RD_BUTTON, RD_X1, RD_Y1}) {
        INFO("$C0" << std::hex << static_cast<int>(reg));
        REQUIRE(mouse.handles(reg, IOU_ENABLED));
        REQUIRE(mouse.handles(reg, IOU_DISABLED));
    }

    SECTION("$C058-$C05F are the mouse's only while IOU access is on") {
        // With IOUDIS on they are the annunciators and the double hi-res
        // switch, and a //c's firmware turns IOU access on for the moment it
        // needs a mouse switch and straight back off again. Taking them
        // permanently would cost the machine double hi-res.
        for (uint8_t reg = DIS_XY; reg <= Y_EDGE_FALLING; reg++) {
            INFO("$C0" << std::hex << static_cast<int>(reg));
            REQUIRE(mouse.handles(reg, IOU_ENABLED));
            REQUIRE_FALSE(mouse.handles(reg, IOU_DISABLED));
        }
    }

    SECTION("and it does not answer for anything else") {
        for (uint8_t reg : {0x00, 0x10, 0x16, 0x19, 0x30, 0x50, 0x61, 0x62,
                            0x64, 0x65, 0x70, 0x7E, 0x80}) {
            INFO("$C0" << std::hex << static_cast<int>(reg));
            REQUIRE_FALSE(mouse.handles(static_cast<uint8_t>(reg), IOU_ENABLED));
        }
    }
}

TEST_CASE("The masks and edge selects read back", "[mouse]") {
    MouseIOU mouse;

    // A machine comes up with both interrupts masked, and a mask reads as 1.
    REQUIRE((mouse.read(RD_XY_MASK) & 0x80) != 0);
    REQUIRE((mouse.read(RD_VBL_MASK) & 0x80) != 0);

    mouse.write(ENB_XY);
    mouse.write(ENB_VBL);
    REQUIRE(mouse.movementInterruptsEnabled());
    REQUIRE(mouse.vblInterruptsEnabled());
    REQUIRE((mouse.read(RD_XY_MASK) & 0x80) == 0);
    REQUIRE((mouse.read(RD_VBL_MASK) & 0x80) == 0);

    mouse.write(DIS_XY);
    mouse.write(DIS_VBL);
    REQUIRE((mouse.read(RD_XY_MASK) & 0x80) != 0);

    SECTION("an edge selection is remembered and reported") {
        mouse.write(X_EDGE_FALLING);
        mouse.write(Y_EDGE_RISING);
        REQUIRE((mouse.read(RD_X_EDGE) & 0x80) != 0); // 1 means falling
        REQUIRE((mouse.read(RD_Y_EDGE) & 0x80) == 0);

        mouse.write(X_EDGE_RISING);
        mouse.write(Y_EDGE_FALLING);
        REQUIRE((mouse.read(RD_X_EDGE) & 0x80) == 0);
        REQUIRE((mouse.read(RD_Y_EDGE) & 0x80) != 0);
    }
}

TEST_CASE("Travel arrives one interrupt at a time", "[mouse]") {
    // The IOU counts nothing. Each unit of movement is a transition on X0 and
    // an interrupt, and the handler adds one to a counter of its own, so ten
    // units is ten interrupts. Releasing them any faster than the handler
    // clears the flag would throw away the ones in between — which looks like
    // a mouse that moves a fraction of the distance and then sticks.
    MouseIOU mouse;
    mouse.write(ENB_XY);
    mouse.addDelta(10, 0);

    uint64_t cycle = 0;
    mouse.update(cycle, false);
    REQUIRE(mouse.xInterruptPending());
    REQUIRE(mouse.pendingX() == 9);

    SECTION("and the next one waits for the handler") {
        // Time passing is not enough: the flag is still set, so the step that
        // would overwrite it is held back.
        for (int i = 0; i < 10; i++) mouse.update(cycle += 1000, false);
        REQUIRE(mouse.pendingX() == 9);

        mouse.read(RST_XY);
        REQUIRE_FALSE(mouse.xInterruptPending());
        mouse.update(cycle += 1000, false);
        REQUIRE(mouse.pendingX() == 8);
    }

    SECTION("all ten arrive in the end") {
        REQUIRE(drain(mouse) == 10);
        REQUIRE(mouse.pendingX() == 0);
    }
}

TEST_CASE("Which way the mouse moved is on the direction lines", "[mouse]") {
    MouseIOU mouse;
    mouse.write(ENB_XY);

    mouse.addDelta(1, 0);
    mouse.update(1000, false);
    REQUIRE((mouse.read(RD_X1) & 0x80) != 0); // right is X1 high
    mouse.read(RST_XY);

    mouse.addDelta(-1, 0);
    mouse.update(2000, false);
    REQUIRE((mouse.read(RD_X1) & 0x80) == 0);
    mouse.read(RST_XY);

    SECTION("and Y reads the other way round, as the firmware expects") {
        // The handler inverts Y1 before counting, because a screen's Y grows
        // downward and the quadrature line does not.
        mouse.addDelta(0, 1);
        mouse.update(3000, false);
        REQUIRE((mouse.read(RD_Y1) & 0x80) == 0); // down is Y1 low
        mouse.read(RST_XY);

        mouse.addDelta(0, -1);
        mouse.update(4000, false);
        REQUIRE((mouse.read(RD_Y1) & 0x80) != 0);
    }
}

TEST_CASE("A masked mouse interrupts nobody", "[mouse]") {
    // Which is the state a machine boots in, and the state "transparent mode"
    // leaves behind: the position simply stops following the mouse.
    MouseIOU mouse;
    mouse.addDelta(50, 50);

    for (int i = 0; i < 20; i++) mouse.update(i * 1000, false);
    REQUIRE_FALSE(mouse.xInterruptPending());
    REQUIRE_FALSE(mouse.isIRQActive());

    SECTION("and does not bank a minute of movement to replay later") {
        for (int i = 0; i < 100; i++) mouse.addDelta(100, 100);
        REQUIRE(mouse.pendingX() <= 1024);
    }
}

TEST_CASE("The flags are cleared by $C048 and not by reading them", "[mouse]") {
    // Table 9-2 calls $C015 and $C017 RstXInt and RstYInt and says a read
    // resets them. The //c's own interrupt handler says otherwise: it reads
    // $C015 and ORs $C017 to see whether either fired, BITs each again to see
    // which, and writes $C048 when it is done. A read that cleared would leave
    // the second test looking at nothing and every X movement handled as a Y.
    MouseIOU mouse;
    mouse.write(ENB_XY);
    mouse.addDelta(1, 1);
    mouse.update(1000, false);

    REQUIRE((mouse.read(RD_X_INT) & 0x80) != 0);
    REQUIRE((mouse.read(RD_X_INT) & 0x80) != 0); // still there, twice over
    REQUIRE((mouse.read(RD_Y_INT) & 0x80) != 0);

    mouse.read(RST_XY);
    REQUIRE((mouse.read(RD_X_INT) & 0x80) == 0);
    REQUIRE((mouse.read(RD_Y_INT) & 0x80) == 0);

    SECTION("and every address from $C048 to $C04F does it") {
        for (uint8_t reg = 0x48; reg <= 0x4F; reg++) {
            mouse.addDelta(1, 0);
            mouse.update(9000 + reg * 1000, false);
            REQUIRE(mouse.xInterruptPending());
            mouse.write(reg);
            REQUIRE_FALSE(mouse.xInterruptPending());
        }
    }
}

TEST_CASE("The interrupt line follows the flags and the mask", "[mouse]") {
    MouseIOU mouse;
    int raised = 0;
    mouse.setIRQCallback([&raised]() { raised++; });

    mouse.write(ENB_XY);
    mouse.addDelta(3, 0);
    mouse.update(1000, false);
    REQUIRE(raised == 1);
    REQUIRE(mouse.isIRQActive());

    mouse.read(RST_XY);
    REQUIRE_FALSE(mouse.isIRQActive());

    SECTION("masking it drops the line without losing the flag's meaning") {
        mouse.update(2000, false);
        REQUIRE(mouse.isIRQActive());
        mouse.write(DIS_XY);
        REQUIRE_FALSE(mouse.isIRQActive());
    }

    SECTION("vertical blank raises it too, once per blanking") {
        // On its own, with nothing left to deliver: a machine in VBL with a
        // mouse still moving is being interrupted by both, and this is about
        // the one.
        MouseIOU vbl;
        int vblRaised = 0;
        vbl.setIRQCallback([&vblRaised]() { vblRaised++; });
        vbl.write(ENB_VBL);

        vbl.update(3000, true);
        vbl.update(3100, true);
        vbl.update(3200, true);
        REQUIRE(vblRaised == 1); // one blanking, one interrupt
        REQUIRE(vbl.vblInterruptPending());
        REQUIRE(vbl.isIRQActive());

        vbl.clearVblInterrupt();
        vbl.update(4000, false); // out of blanking
        vbl.update(5000, true);  // and into the next one
        REQUIRE(vblRaised == 2);
    }
}

TEST_CASE("The button reads low when it is pressed", "[mouse]") {
    // $C063 is the //e's shift-key modifier input, which idles the other way
    // round. A //c reading it while nothing is pressed must see bit 7 set, or
    // its firmware decides the button is down and stays down.
    MouseIOU mouse;
    REQUIRE((mouse.read(RD_BUTTON) & 0x80) != 0);

    mouse.setButton(true);
    REQUIRE(mouse.isButtonPressed());
    REQUIRE((mouse.read(RD_BUTTON) & 0x80) == 0);

    mouse.setButton(false);
    REQUIRE((mouse.read(RD_BUTTON) & 0x80) != 0);
}

TEST_CASE("A peek changes nothing", "[mouse]") {
    // Nearly every address here is a switch, so a debugger reading them the
    // way the CPU does would enable interrupts and clear flags by watching.
    MouseIOU mouse;
    mouse.write(ENB_XY);
    mouse.addDelta(1, 0);
    mouse.update(1000, false);

    REQUIRE(mouse.peek(RD_X_INT) == 0x80);
    REQUIRE(mouse.xInterruptPending());
    mouse.peek(RST_XY);
    REQUIRE(mouse.xInterruptPending());
    mouse.peek(DIS_XY);
    REQUIRE(mouse.movementInterruptsEnabled());
}

TEST_CASE("A reset leaves the mouse masked and still", "[mouse]") {
    MouseIOU mouse;
    mouse.write(ENB_XY);
    mouse.write(ENB_VBL);
    mouse.addDelta(20, 20);
    mouse.setButton(true);
    mouse.update(1000, true);

    mouse.reset();

    REQUIRE_FALSE(mouse.movementInterruptsEnabled());
    REQUIRE_FALSE(mouse.vblInterruptsEnabled());
    REQUIRE_FALSE(mouse.xInterruptPending());
    REQUIRE_FALSE(mouse.isButtonPressed());
    REQUIRE(mouse.pendingX() == 0);
    REQUIRE_FALSE(mouse.isIRQActive());
}

TEST_CASE("Shift is on the button's line, and both read low", "[mouse][shift]") {
    // A //c has the //e's shift-key modification built in: the Shift key is
    // wired to PB2, the same line as the mouse button, and the Technical
    // Reference gives both as "0 if it is pressed". With nothing pressed the
    // line idles high — there is no pull-down on it.
    MouseIOU mouse;
    REQUIRE((mouse.read(RD_BUTTON) & 0x80) != 0);

    mouse.setShiftKey(true);
    REQUIRE((mouse.read(RD_BUTTON) & 0x80) == 0);
    mouse.setShiftKey(false);
    REQUIRE((mouse.read(RD_BUTTON) & 0x80) != 0);

    mouse.setButton(true);
    REQUIRE((mouse.read(RD_BUTTON) & 0x80) == 0);
    mouse.setShiftKey(true);
    mouse.setButton(false);
    REQUIRE((mouse.read(RD_BUTTON) & 0x80) == 0);
}
