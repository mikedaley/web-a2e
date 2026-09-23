/*
 * mouse_iou.cpp - A //c's mouse, which is wired to the IOU rather than a card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "mouse_iou.hpp"

#include <algorithm>

namespace a2e {

namespace {
// The switches this thing owns. Named as the //c Technical Reference names
// them, because half of them are addresses a //e uses for something else and
// the number on its own tells you nothing.
constexpr uint8_t RD_X_INT = 0x15;   // bit 7: X0 interrupt happened; read resets
constexpr uint8_t RD_Y_INT = 0x17;   // bit 7: Y0 interrupt happened; read resets
constexpr uint8_t RD_XY_MASK = 0x40; // bit 7: 1 when movement interrupts masked
constexpr uint8_t RD_VBL_MASK = 0x41;
constexpr uint8_t RD_X_EDGE = 0x42; // bit 7: 1 when set to the falling edge
constexpr uint8_t RD_Y_EDGE = 0x43;
constexpr uint8_t RST_XY_FIRST = 0x48; // $C048-$C04F all reset both flags
constexpr uint8_t RST_XY_LAST = 0x4F;
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

constexpr uint8_t BIT7 = 0x80;
} // namespace

void MouseIOU::reset() {
    // A machine comes up with both interrupts masked: the firmware turns on
    // what it wants when something asks for a mouse.
    xyEnabled_ = false;
    vblEnabled_ = false;
    xEdgeFalling_ = false;
    yEdgeFalling_ = false;
    xInt_ = yInt_ = vblInt_ = false;
    x1_ = y1_ = false;
    button_ = false;
    pendingX_ = pendingY_ = 0;
    nextStepCycle_ = 0;
    wasInVbl_ = false;
}

void MouseIOU::addDelta(int dx, int dy) {
    pendingX_ = std::clamp(pendingX_ + dx, -MAX_PENDING, MAX_PENDING);
    pendingY_ = std::clamp(pendingY_ + dy, -MAX_PENDING, MAX_PENDING);
}

bool MouseIOU::handles(uint8_t reg, bool ioudis) const {
    switch (reg) {
    case RD_X_INT:
    case RD_Y_INT:
    case RD_XY_MASK:
    case RD_VBL_MASK:
    case RD_X_EDGE:
    case RD_Y_EDGE:
    case RD_BUTTON:
    case RD_X1:
    case RD_Y1:
        return true;
    default:
        break;
    }

    if (reg >= RST_XY_FIRST && reg <= RST_XY_LAST) return true;

    // The $C058-$C05F group is the mouse's only while IOU access is enabled.
    // With IOUDIS on they are the annunciators and the double hi-res switch,
    // and the firmware relies on being able to put them back.
    if (reg >= DIS_XY && reg <= Y_EDGE_FALLING) return !ioudis;

    return false;
}

uint8_t MouseIOU::read(uint8_t reg) {
    // $C015 and $C017 report, and only $C048 clears.
    //
    // Table 9-2 calls them RstXInt and RstYInt and says a read resets the
    // flag, and the //c's own interrupt handler proves that cannot be what the
    // chip does: it reads $C015 and ORs $C017 to find out whether either fired
    // ($C528), then BITs each of them again to find out which ($C533, $C53D),
    // and clears both with a single write to $C048 when it is finished
    // ($C57B). A flag that cleared on the first read would leave the second
    // test looking at nothing, and every X movement would be handled as a Y.
    if (reg >= RST_XY_FIRST && reg <= RST_XY_LAST) {
        xInt_ = false;
        yInt_ = false;
        return 0x00;
    }

    // Everything else a read can reach is either a status bit or a switch that
    // does the same thing whether it is read or written.
    write(reg);
    return peek(reg);
}

uint8_t MouseIOU::peek(uint8_t reg) const {
    switch (reg) {
    case RD_X_INT:
        return xInt_ ? BIT7 : 0x00;
    case RD_Y_INT:
        return yInt_ ? BIT7 : 0x00;
    case RD_XY_MASK:
        return xyEnabled_ ? 0x00 : BIT7; // 1 means masked
    case RD_VBL_MASK:
        return vblEnabled_ ? 0x00 : BIT7;
    case RD_X_EDGE:
        return xEdgeFalling_ ? BIT7 : 0x00;
    case RD_Y_EDGE:
        return yEdgeFalling_ ? BIT7 : 0x00;
    case RD_BUTTON:
        // Pressed reads as zero, and so does a held Shift key: a //c has the
        // //e's shift-key modification built in, on this same line, and the
        // Technical Reference gives both as "0 if it is pressed". Neither is
        // a pull-down, which is why a //c with nothing pressed reads high.
        return (button_ || shiftKey_) ? 0x00 : BIT7;
    case RD_X1:
        return x1_ ? BIT7 : 0x00;
    case RD_Y1:
        return y1_ ? BIT7 : 0x00;
    default:
        return 0x00;
    }
}

void MouseIOU::write(uint8_t reg) {
    switch (reg) {
    case DIS_XY:
        xyEnabled_ = false;
        break;
    case ENB_XY:
        xyEnabled_ = true;
        break;
    case DIS_VBL:
        vblEnabled_ = false;
        vblInt_ = false;
        break;
    case ENB_VBL:
        vblEnabled_ = true;
        break;
    case X_EDGE_RISING:
        xEdgeFalling_ = false;
        break;
    case X_EDGE_FALLING:
        xEdgeFalling_ = true;
        break;
    case Y_EDGE_RISING:
        yEdgeFalling_ = false;
        break;
    case Y_EDGE_FALLING:
        yEdgeFalling_ = true;
        break;
    default:
        if (reg >= RST_XY_FIRST && reg <= RST_XY_LAST) {
            xInt_ = false;
            yInt_ = false;
        }
        break;
    }
}

void MouseIOU::update(uint64_t cycle, bool inVbl) {
    if (inVbl && !wasInVbl_ && vblEnabled_ && !vblInt_) {
        vblInt_ = true;
        if (irqCallback_) irqCallback_();
    }
    wasInVbl_ = inVbl;

    if (!xyEnabled_) return;
    if (cycle < nextStepCycle_) return;

    // One step of each axis at a time, and only onto a flag the handler has
    // already cleared. Both axes can move in the same step: a diagonal drag
    // interrupts twice and the handler is told which by reading $C015 and
    // $C017 in turn, exactly as the firmware does.
    bool stepped = false;

    if (!xInt_ && pendingX_ != 0) {
        x1_ = pendingX_ > 0;
        pendingX_ += pendingX_ > 0 ? -1 : 1;
        xInt_ = true;
        stepped = true;
    }

    if (!yInt_ && pendingY_ != 0) {
        // Y reads the other way round, and not by accident: the firmware's
        // handler inverts Y1 (EOR #$80 at $C539) before it decides which way
        // to count, because a mouse's Y axis increases downward and the
        // quadrature line does not. So down the screen is Y1 low.
        y1_ = pendingY_ < 0;
        pendingY_ += pendingY_ > 0 ? -1 : 1;
        yInt_ = true;
        stepped = true;
    }

    if (stepped) {
        nextStepCycle_ = cycle + STEP_CYCLES;
        if (irqCallback_) irqCallback_();
    }
}

bool MouseIOU::isIRQActive() const {
    if (xyEnabled_ && (xInt_ || yInt_)) return true;
    return vblEnabled_ && vblInt_;
}

void MouseIOU::serialize(StateWriter &w) const {
    w.boolean(xyEnabled_);
    w.boolean(vblEnabled_);
    w.boolean(xEdgeFalling_);
    w.boolean(yEdgeFalling_);
    w.boolean(xInt_);
    w.boolean(yInt_);
    w.boolean(vblInt_);
    w.boolean(x1_);
    w.boolean(y1_);
    w.boolean(button_);
    w.i32(pendingX_);
    w.i32(pendingY_);
    w.u64(nextStepCycle_);
    w.boolean(wasInVbl_);
}

void MouseIOU::deserialize(StateReader &r) {
    xyEnabled_ = r.boolean();
    vblEnabled_ = r.boolean();
    xEdgeFalling_ = r.boolean();
    yEdgeFalling_ = r.boolean();
    xInt_ = r.boolean();
    yInt_ = r.boolean();
    vblInt_ = r.boolean();
    x1_ = r.boolean();
    y1_ = r.boolean();
    button_ = r.boolean();
    pendingX_ = r.i32();
    pendingY_ = r.i32();
    nextStepCycle_ = r.u64();
    wasInVbl_ = r.boolean();
}

} // namespace a2e
