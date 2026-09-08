/*
 * expansion_card.hpp - Base class for expansion cards
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../machine/machine_profile.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>

namespace a2e {

/**
 * ExpansionCard - Abstract interface for Apple IIe expansion slot cards
 *
 * The Apple IIe has 7 expansion slots (1-7), each with:
 * - I/O space: $C0n0-$C0nF (16 bytes, where n = slot + 8)
 * - ROM space: $Cn00-$CnFF (256 bytes)
 * - Expansion ROM: $C800-$CFFF (shared 2KB, active when card's ROM is accessed)
 *
 * Slot assignments by convention:
 * - Slot 1: Printer cards
 * - Slot 2: Serial/modem cards
 * - Slot 3: 80-column card (built-in on //e, controlled by SLOTC3ROM)
 * - Slot 4: Mockingboard / other sound cards
 * - Slot 5: Hard drive / accelerator cards
 * - Slot 6: Disk II controller
 * - Slot 7: ProDOS RAM disk / other cards
 */
class ExpansionCard {
public:
    using IRQCallback = std::function<void()>;
    using CycleCallback = std::function<uint64_t()>;

    virtual ~ExpansionCard() = default;

    // ===== I/O Space Access ($C0n0-$C0nF) =====

    /**
     * Read from the card's I/O space
     * @param offset Offset within slot I/O (0-15)
     * @return Byte value
     */
    virtual uint8_t readIO(uint8_t offset) = 0;

    /**
     * Write to the card's I/O space
     * @param offset Offset within slot I/O (0-15)
     * @param value Byte value
     */
    virtual void writeIO(uint8_t offset, uint8_t value) = 0;

    /**
     * Peek at I/O without side effects (for debugger)
     * @param offset Offset within slot I/O (0-15)
     * @return Byte value
     */
    virtual uint8_t peekIO(uint8_t offset) const { return 0xFF; }

    // ===== ROM Space Access ($Cn00-$CnFF) =====

    /**
     * Read from the card's ROM space
     * @param offset Offset within slot ROM (0-255)
     * @return Byte value
     */
    virtual uint8_t readROM(uint8_t offset) = 0;

    /**
     * Check if this card has ROM (most cards do)
     * @return true if card provides ROM at $Cn00-$CnFF
     */
    virtual bool hasROM() const { return true; }

    /**
     * Write to the card's ROM space (unusual - used by cards like Mockingboard
     * that put I/O registers in ROM space instead of I/O space)
     * @param offset Offset within slot ROM (0-255)
     * @param value Byte value
     */
    virtual void writeROM(uint8_t offset, uint8_t value) {}

    // ===== Expansion ROM Space ($C800-$CFFF) =====

    /**
     * Check if this card has expansion ROM
     * @return true if card provides expansion ROM at $C800-$CFFF
     */
    virtual bool hasExpansionROM() const { return false; }

    /**
     * Read from the card's expansion ROM space
     * @param offset Offset within expansion ROM (0-2047)
     * @return Byte value
     */
    virtual uint8_t readExpansionROM(uint16_t offset) { return 0xFF; }

    // ===== Lifecycle =====

    /**
     * Reset the card to power-on state
     */
    virtual void reset() = 0;

    /**
     * Tell the card which machine it has been plugged into.
     *
     * Called by the MMU when the card is inserted. Most cards ignore it: a
     * Disk II does not care what is at the other end of the bus. It exists for
     * the cards that time themselves against the machine rather than against
     * their own oscillator — the mouse card raises its interrupt at the start
     * of vertical blank, and where vertical blank falls is a property of the
     * machine's video timing, not of the card.
     *
     * @param machine Profile of the host machine, valid for the card's lifetime
     */
    virtual void setMachine(const MachineProfile &machine) { (void)machine; }

    /**
     * Update the card's internal state (call each CPU cycle)
     * @param cycles Number of CPU cycles elapsed
     */
    virtual void update(int cycles) {}

    // ===== Callbacks =====

    /**
     * Set callback for generating IRQ
     * @param callback Function to call when IRQ is asserted
     */
    virtual void setIRQCallback(IRQCallback callback) {}

    /**
     * Set callback for getting current CPU cycle count
     * @param callback Function that returns current cycle count
     */
    virtual void setCycleCallback(CycleCallback callback) {}

    /**
     * Check if IRQ is currently active
     * @return true if IRQ line is asserted
     */
    virtual bool isIRQActive() const { return false; }

    // ===== State Serialization =====

    /**
     * Get the size of the serialized state
     * @return Size in bytes
     */
    virtual size_t getStateSize() const { return 0; }

    /**
     * Serialize card state for save
     * @param buffer Output buffer
     * @param maxSize Maximum bytes to write
     * @return Bytes written
     */
    virtual size_t serialize(uint8_t* buffer, size_t maxSize) const { return 0; }

    /**
     * Deserialize card state from save
     * @param buffer Input buffer
     * @param size Size of input data
     * @return Bytes read
     */
    virtual size_t deserialize(const uint8_t* buffer, size_t size) { return 0; }

    // ===== Card Information =====

    /**
     * Get the card's name for display
     * @return Human-readable name
     */
    virtual const char* getName() const = 0;

    /**
     * Get the card's preferred slot (0 = any)
     * @return Preferred slot number (1-7) or 0 for no preference
     */
    virtual uint8_t getPreferredSlot() const { return 0; }

    /**
     * Check if card is currently enabled
     * @return true if card is active
     */
    virtual bool isEnabled() const { return true; }

    /**
     * Enable or disable the card
     * @param enabled true to enable
     */
    virtual void setEnabled(bool enabled) {}
};

} // namespace a2e
