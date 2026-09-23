/*
 * mouse_iou.hpp - A //c's mouse, which is wired to the IOU rather than a card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../emulator/state_stream.hpp"

#include <cstdint>
#include <functional>

namespace a2e {

/**
 * MouseIOU - the mouse input a //c has instead of a mouse card
 *
 * A //e's mouse is a card: an MC6821 PIA in a slot, a ROM on the card, and a
 * command protocol the firmware talks over the PIA's two ports. A //c has none
 * of that. Its mouse plugs into the back panel and its two quadrature lines go
 * straight into the IOU, so what the machine offers software is a handful of
 * soft switches scattered through $C0xx — which is why this is not an
 * ExpansionCard. It is a different mechanism, and the profile names it in slot
 * 4 only because that is where a //c's mouse *firmware* lives.
 *
 * The switches, from the //c Technical Reference (Table 9-2):
 *
 *   $C058 / $C059   DisXY / EnbXY — mask or allow X0/Y0 movement interrupts
 *   $C05A / $C05B   DisVBl / EnVBl — mask or allow VBL interrupts
 *   $C05C / $C05D   X0Edge — interrupt on the rising or falling edge of X0
 *   $C05E / $C05F   Y0Edge — the same for Y0
 *   $C040           bit 7: X0/Y0 interrupt mask (1 = masked)
 *   $C041           bit 7: VBL interrupt mask (1 = masked)
 *   $C042 / $C043   bit 7: which edge X0 / Y0 is set to (1 = falling)
 *   $C015           bit 7: an X0 interrupt happened — and reading resets it
 *   $C017           bit 7: the same for Y0
 *   $C048-$C04F     resets both movement interrupt flags
 *   $C066 / $C067   bit 7: the X1 and Y1 lines — which way the mouse moved
 *   $C063           bit 7: the button, 0 when pressed; Shift is on the same
 *                   line (the //c Technical Reference: "0 if it is pressed")
 *
 * The $C058-$C05F group is shared with the annunciators and with double
 * hi-res, and IOUDIS is what decides: with IOUDIS off those eight addresses
 * belong to the mouse, and with it on they are the //e's. The firmware turns
 * IOUDIS off, touches a switch and turns it back on, which is why `handles()`
 * is asked about it rather than deciding for itself.
 *
 * **Movement is interrupts, not a position.** The IOU does not count anything:
 * each unit of travel toggles X0, and the edge raises an interrupt whose
 * handler reads X1 to learn the direction and adds one to a counter in the
 * machine's own screen holes. A mouse that moved 20 units therefore has to
 * interrupt 20 times. So host movement is banked here and released one step at
 * a time, and only when the previous step's flag has been cleared: releasing
 * them faster than the handler runs would lose the steps in between, which
 * looks like a mouse that moves a fraction of the distance and sticks.
 *
 * Which edge is selected is recorded and read back but does not gate anything.
 * A real X0 alternates, and the firmware selects the edge matching the level
 * it is at so the next transition interrupts; since a step here *is* the
 * transition, honouring the selection as well would drop every other one.
 */
class MouseIOU {
public:
    using IRQCallback = std::function<void()>;

    MouseIOU() { reset(); }

    /**
     * How the IOU pulls the interrupt line.
     *
     * Called once per step it raises a flag for, which is the right shape even
     * though the real line is a level: the flag is only cleared by the handler
     * reading it, and a step is never released onto a flag that is still set,
     * so one edge is one interrupt and none can be missed.
     */
    void setIRQCallback(IRQCallback cb) { irqCallback_ = std::move(cb); }

    // ===== Host input =====

    /** Mouse travel, in the units the firmware counts. */
    void addDelta(int dx, int dy);
    void setButton(bool pressed) { button_ = pressed; }
    bool isButtonPressed() const { return button_; }
    /** The keyboard's Shift key, which a //c wires to the same PB2 line. */
    void setShiftKey(bool held) { shiftKey_ = held; }
    bool isShiftKeyHeld() const { return shiftKey_; }

    // ===== Soft switches =====

    /**
     * Whether this address belongs to the mouse right now.
     *
     * `ioudis` is the machine's IOUDIS switch, because the $C058-$C05F group
     * is only the mouse's while it is off.
     */
    bool handles(uint8_t reg, bool ioudis) const;

    /**
     * Acknowledge the VBL interrupt.
     *
     * Not one of this object's own addresses: the flag is reset by reading
     * $C019, which is the machine's vertical blanking status and belongs to
     * the video counters, and by $C070, which starts the paddle timers. The
     * MMU owns both and tells the IOU when one goes past.
     */
    void clearVblInterrupt() { vblInt_ = false; }

    uint8_t read(uint8_t reg);            // as the CPU sees it, side effects and all
    uint8_t peek(uint8_t reg) const;      // as the debugger sees it
    void write(uint8_t reg);

    // ===== Running =====

    /**
     * Release banked movement and notice vertical blank.
     *
     * @param cycle  the CPU's total cycle count
     * @param inVbl  whether the video is in vertical blanking now
     */
    void update(uint64_t cycle, bool inVbl);

    /** Level-triggered: an unserviced interrupt flag that is not masked. */
    bool isIRQActive() const;

    void reset();

    // ===== State, for tests and the debugger =====

    /**
     * The IOU's mouse in a save state: what is enabled, what is pending, and
     * the steps banked but not yet released — a mouse mid-move must finish
     * the move after a restore, or the pointer lands short.
     */
    void serialize(StateWriter &w) const;
    void deserialize(StateReader &r);

    bool movementInterruptsEnabled() const { return xyEnabled_; }
    bool vblInterruptsEnabled() const { return vblEnabled_; }
    bool xInterruptPending() const { return xInt_; }
    bool yInterruptPending() const { return yInt_; }
    bool vblInterruptPending() const { return vblInt_; }
    int pendingX() const { return pendingX_; }
    int pendingY() const { return pendingY_; }

private:
    // A step every this many cycles, so a fast swipe arrives as a stream of
    // interrupts a handler can keep up with rather than all at once. Roughly
    // 4kHz, which is a brisk real mouse and is still 200-odd instructions of
    // handler between steps.
    static constexpr uint64_t STEP_CYCLES = 256;

    // Travel banked but not yet delivered. Capped so that a machine ignoring
    // its mouse — interrupts masked, which is what "transparent mode" leaves
    // behind — does not save up a minute of movement to replay later.
    static constexpr int MAX_PENDING = 1024;

    bool xyEnabled_ = false;   // EnbXY / DisXY
    bool vblEnabled_ = false;  // EnVBl / DisVBl
    bool xEdgeFalling_ = false;
    bool yEdgeFalling_ = false;

    bool xInt_ = false;
    bool yInt_ = false;
    bool vblInt_ = false;

    bool x1_ = false; // Direction of the last step released
    bool y1_ = false;
    bool button_ = false;
    // Host state, like the button: deliberately not serialized.
    bool shiftKey_ = false;

    int pendingX_ = 0;
    int pendingY_ = 0;
    uint64_t nextStepCycle_ = 0;
    bool wasInVbl_ = false;

    IRQCallback irqCallback_;
};

} // namespace a2e
