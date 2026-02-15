/*
 * ssc_card.hpp - Apple Super Serial Card expansion card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../expansion_card.hpp"
#include "acia6551.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>

namespace a2e {

/**
 * SSCCard - Apple Super Serial Card
 *
 * The SSC uses a MOS 6551 ACIA UART chip and a 2KB ROM.
 * In the browser, the serial port connects via WebSocket to a remote host.
 *
 * I/O Space ($C0n0-$C0nF):
 *   Offset 0: Communication mode (not directly used)
 *   Offset 1: Read SW1 DIP switches
 *   Offset 2: Read SW2 DIP switches
 *   Offsets 8-B: 6551 ACIA registers (offset & 0x03)
 *
 * ROM Space:
 *   $Cn00-$CnFF: Slot ROM (first 256 bytes of 2KB ROM)
 *   $C800-$CFFF: Expansion ROM (full 2KB)
 *
 * DIP Switch Defaults (SW1 = $16, SW2 = $00):
 *   9600 baud, 8 data bits, 1 stop bit, no parity
 *
 * Typically installed in Slot 1 or Slot 2.
 */
class SSCCard : public ExpansionCard {
public:
    using SerialTxCallback = std::function<void(uint8_t)>;

    SSCCard();
    ~SSCCard() override = default;

    // Delete copy
    SSCCard(const SSCCard&) = delete;
    SSCCard& operator=(const SSCCard&) = delete;

    // Allow move
    SSCCard(SSCCard&&) = default;
    SSCCard& operator=(SSCCard&&) = default;

    // ===== ExpansionCard Interface =====

    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    uint8_t readROM(uint8_t offset) override;
    bool hasROM() const override { return true; }

    bool hasExpansionROM() const override { return true; }
    uint8_t readExpansionROM(uint16_t offset) override;

    void reset() override;
    void update(int cycles) override;

    void setIRQCallback(IRQCallback callback) override { irqCallback_ = std::move(callback); }
    bool isIRQActive() const override { return acia_.isIRQActive(); }

    const char* getName() const override { return "Super Serial Card"; }
    uint8_t getPreferredSlot() const override { return 2; }

    // State serialization
    static constexpr size_t STATE_SIZE = 4 + ACIA6551::STATE_SIZE; // SW1/SW2 + ACIA
    size_t getStateSize() const override { return STATE_SIZE; }
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    // ===== Serial interface =====

    void setSerialTxCallback(SerialTxCallback cb);
    void serialReceive(uint8_t byte);

    // DIP switch configuration
    void setSW1(uint8_t value) { sw1_ = value; }
    void setSW2(uint8_t value) { sw2_ = value; }
    uint8_t getSW1() const { return sw1_; }
    uint8_t getSW2() const { return sw2_; }

    // Slot number (needed for ROM address calculation)
    void setSlotNumber(uint8_t slot) { slotNumber_ = slot; }

private:
    ACIA6551 acia_;

    // ROM data
    const uint8_t* rom_ = nullptr;
    size_t romSize_ = 0;

    // DIP switch settings
    // SW1 default: $16 = 9600 baud, 8N1
    // SW2 default: $00 = no interrupts
    uint8_t sw1_ = 0x16;
    uint8_t sw2_ = 0x00;

    uint8_t slotNumber_ = 2;

    // IRQ callback
    IRQCallback irqCallback_;
};

} // namespace a2e
