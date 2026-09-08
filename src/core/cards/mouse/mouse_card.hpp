/*
 * mouse_card.hpp - Apple Mouse Interface card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../expansion_card.hpp"
#include <cstdint>

namespace a2e {

/**
 * MouseCard - Apple Mouse Interface Card emulation
 *
 * Emulates the Apple Mouse Interface Card (342-0270-C ROM) using the
 * AppleWin-style PIA command protocol. The real ROM firmware executes
 * as native 6502 code on the CPU while our C++ code emulates the
 * "MCU side" of the MC6821 PIA — receiving commands and providing
 * response data through Port A/B transitions.
 *
 * Protocol overview:
 *   6502 CPU <-> ROM firmware (runs natively) <-> MC6821 PIA <-> Our "MCU" code
 *
 * The firmware writes command bytes to PIA Port A and toggles Port B
 * control signals. Our code detects Port B transitions, buffers the
 * command/data, and places response data on Port A input pins for the
 * firmware to read back.
 *
 * Mouse state is communicated through "screen holes" by the firmware
 * itself — we only provide position/status data via the PIA when asked.
 *
 * The 6821 PIA provides 4 registers at I/O offsets 0-3 and the
 * real ROM is served for card identification and firmware execution.
 * Port B bits 1-3 select the 256-byte ROM page (2KB total).
 *
 * VBL (Vertical Blank) interrupt generation:
 * When the mouse mode has bit 3 set (VBL interrupt enable), an IRQ
 * is generated at the start of each vertical blanking period.
 *
 * Available in slots 2, 4, and 7.
 */
class MouseCard : public ExpansionCard {
public:
    MouseCard();
    ~MouseCard() override = default;

    // The card raises its interrupt at the start of vertical blank, so it needs
    // the machine's video timing to know where that is.
    void setMachine(const MachineProfile &machine) override {
        machine_ = &machine;
    }

    // Delete copy
    MouseCard(const MouseCard&) = delete;
    MouseCard& operator=(const MouseCard&) = delete;

    // Allow move
    MouseCard(MouseCard&&) = default;
    MouseCard& operator=(MouseCard&&) = default;

    // ===== ExpansionCard Interface =====

    // I/O space ($C0n0-$C0nF)
    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    // ROM space ($Cn00-$CnFF)
    uint8_t readROM(uint8_t offset) override;
    bool hasROM() const override { return true; }

    // No expansion ROM - the mouse card uses banked slot ROM instead
    bool hasExpansionROM() const override { return false; }
    uint8_t readExpansionROM(uint16_t offset) override;

    void reset() override;
    void update(int cycles) override;

    const char* getName() const override { return "Mouse"; }
    uint8_t getPreferredSlot() const override { return 4; }

    // IRQ support
    void setIRQCallback(IRQCallback callback) override { irqCallback_ = callback; }
    void setCycleCallback(CycleCallback callback) override { cycleCallback_ = callback; }
    bool isIRQActive() const override { return irqActive_; }

    // State serialization
    static constexpr size_t STATE_SIZE = 36;
    size_t getStateSize() const override { return STATE_SIZE; }
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    // Mouse input from browser
    void setSlotNumber(uint8_t slot) { slotNum_ = slot; }
    void addDelta(int dx, int dy);
    void setMouseButton(bool pressed);

    // Debug accessors
    uint8_t getSlotNumber() const { return slotNum_; }
    int16_t getMouseX() const { return mouseX_; }
    int16_t getMouseY() const { return mouseY_; }
    bool getMouseButton() const { return mouseButton_; }
    bool getMoved() const { return moved_; }
    bool getButtonChanged() const { return buttonChanged_; }
    int16_t getClampMinX() const { return clampMinX_; }
    int16_t getClampMaxX() const { return clampMaxX_; }
    int16_t getClampMinY() const { return clampMinY_; }
    int16_t getClampMaxY() const { return clampMaxY_; }
    uint8_t getDDRA() const { return ddra_; }
    uint8_t getDDRB() const { return ddrb_; }
    uint8_t getORA() const { return ora_; }
    uint8_t getORB() const { return orb_; }
    uint8_t getIRA() const { return ira_; }
    uint8_t getIRB() const { return irb_; }
    uint8_t getCRA() const { return cra_; }
    uint8_t getCRB() const { return crb_; }
    bool getVBLInterruptPending() const { return vblInterruptPending_; }
    bool getMoveInterruptPending() const { return moveInterruptPending_; }
    bool getButtonInterruptPending() const { return buttonInterruptPending_; }
    uint8_t getLastCommand() const { return lastCommand_; }
    uint8_t getResponseState() const { return byState_; }
    bool getWasInVBL() const { return wasInVBL_; }
    uint8_t getMode() const { return mode_; }

private:
    // Not owned: profiles are static constexpr objects with program lifetime.
    const MachineProfile *machine_ = &defaultMachineProfile();


    // ROM data
    const uint8_t* rom_;
    size_t romSize_;

    // Slot number (needed for screen hole addressing)
    uint8_t slotNum_ = 4;

    // 6821 PIA registers
    uint8_t ddra_ = 0;     // Data Direction Register A (1=output, 0=input)
    uint8_t ddrb_ = 0;     // Data Direction Register B
    uint8_t ora_ = 0;      // Output Register A
    uint8_t orb_ = 0;      // Output Register B
    uint8_t ira_ = 0;      // Input Register A (peripheral side)
    uint8_t irb_ = 0;      // Input Register B (peripheral side)
    uint8_t cra_ = 0;      // Control Register A
    uint8_t crb_ = 0;      // Control Register B

    // Mouse state
    int16_t mouseX_ = 0;
    int16_t mouseY_ = 0;
    bool mouseButton_ = false;
    bool lastButton_ = false;
    bool moved_ = false;
    bool buttonChanged_ = false;

    // Mouse mode (set by SetMouse command)
    uint8_t mode_ = 0;

    // Mouse clamping
    int16_t clampMinX_ = 0;
    int16_t clampMaxX_ = 1023;
    int16_t clampMinY_ = 0;
    int16_t clampMaxY_ = 1023;

    // Interrupt state
    bool irqActive_ = false;
    bool vblInterruptPending_ = false;
    bool moveInterruptPending_ = false;
    bool buttonInterruptPending_ = false;

    // VBL tracking
    bool wasInVBL_ = false;

    // IRQ/cycle callbacks
    IRQCallback irqCallback_;
    CycleCallback cycleCallback_;

    // PIA command protocol state machine (AppleWin-style)
    uint8_t byState_ = 0;       // Protocol state (0=idle, 1=writing, 2=reading)
    uint8_t byBuff_[8] = {};    // Command/response data buffer
    uint8_t nBuffPos_ = 0;      // Current buffer position
    uint8_t nDataLen_ = 0;      // Expected data length for current operation
    uint8_t by6821B_ = 0;       // Previous Port B value for transition detection
    uint8_t lastCommand_ = 0;   // Last command byte (for debug display)

    // Position snapshot (set on ReadMouse, served to firmware)
    int16_t snapX_ = 0;
    int16_t snapY_ = 0;

    void updateIRQState();

    // PIA command protocol handlers
    void on6821_B(uint8_t byData);  // Port B transition handler
    void onCommand();               // Command dispatch
    void onWrite();                 // Multi-byte write dispatch
};

} // namespace a2e
