/*
 * smartport_card.hpp - SmartPort expansion card for ProDOS block devices
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../expansion_card.hpp"
#include "block_device.hpp"
#include <cstdint>
#include <functional>
#include <array>

namespace a2e {

class SmartPortCard : public ExpansionCard {
public:
    static constexpr int MAX_DEVICES = 2;

    using MemReadCallback = std::function<uint8_t(uint16_t)>;
    using MemWriteCallback = std::function<void(uint16_t, uint8_t)>;
    // The whole address space, for a machine that has more than one bank. A
    // machine that does not leave these unset and the card stays in bank zero.
    using MemRead24Callback = std::function<uint8_t(uint32_t)>;
    using MemWrite24Callback = std::function<void(uint32_t, uint8_t)>;
    using RegGetCallback8 = std::function<uint8_t()>;
    using RegSetCallback8 = std::function<void(uint8_t)>;
    using RegGetCallback16 = std::function<uint16_t()>;
    using RegSetCallback16 = std::function<void(uint16_t)>;

    SmartPortCard();
    ~SmartPortCard() override = default;

    // ExpansionCard interface
    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override { return 0xFF; }

    uint8_t readROM(uint8_t offset) override;
    uint8_t peekROM(uint8_t offset) override;
    bool hasROM() const override { return hasAnyDevice(); }

    void reset() override;
    const char* getName() const override { return "SmartPort"; }
    uint8_t getPreferredSlot() const override { return 7; }

    // State serialization
    size_t getStateSize() const override;
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    // Slot configuration
    void setSlotNumber(uint8_t slot);
    uint8_t getSlotNumber() const { return slotNum_; }

    // Device management
    bool insertImage(int device, const uint8_t* data, size_t size, const std::string& filename);
    void ejectImage(int device);
    bool isImageInserted(int device) const;
    const std::string& getImageFilename(int device) const;
    bool isImageModified(int device) const;
    const uint8_t* exportImageData(int device, size_t* size) const;
    const uint8_t* getBlockData(int device, size_t* size) const;
    BlockDevice* getDevice(int device);
    const BlockDevice* getDevice(int device) const;

    // Callbacks for memory and CPU access
    void setMemReadCallback(MemReadCallback cb) { memRead_ = cb; }
    void setMemWriteCallback(MemWriteCallback cb) { memWrite_ = cb; }
    void setMemRead24Callback(MemRead24Callback cb) { memRead24_ = std::move(cb); }
    void setMemWrite24Callback(MemWrite24Callback cb) { memWrite24_ = std::move(cb); }
    void setGetA(RegGetCallback8 cb) { getA_ = cb; }
    void setSetA(RegSetCallback8 cb) { setA_ = cb; }
    void setGetP(RegGetCallback8 cb) { getP_ = cb; }
    void setSetP(RegSetCallback8 cb) { setP_ = cb; }
    /**
     * The stack pointer, as the address in bank zero it points at.
     *
     * A 6502's stack is page one and its pointer is one byte, so a 6502 hands
     * over `$0100 | S` and takes back the low byte. A 65816's pointer is the
     * whole address, and in emulation mode it happens to be `$01xx` — which is
     * why one shape serves both: the card reads a return address at `SP+1`
     * and never has to know which processor pushed it.
     */
    void setGetSP(RegGetCallback16 cb) { getSP_ = cb; }
    void setSetSP(RegSetCallback16 cb) { setSP_ = cb; }
    /**
     * Whether the CPU is *executing* this ROM address, rather than reading it
     * as data. The card's entry points are traps: a read of $Cn10 during a
     * fetch is a driver call to service, and a read of the same byte by a
     * ProDOS scan is just a byte.
     *
     * This is a predicate rather than a comparison the card makes itself
     * because only the machine knows what its processor has done to the
     * program counter by the time the read arrives. A 6502 here fetches with
     * `read(pc_++)`, so the counter has already moved past the opcode; a 65816
     * reads and then increments, so it has not. The card guessing at that is
     * how a IIgs came to boot a SmartPort volume and then fail every driver
     * call after it, on an off-by-one that matched the boot by luck.
     */
    using ExecutingAtCallback = std::function<bool(uint16_t)>;
    void setExecutingAt(ExecutingAtCallback cb) { executingAt_ = std::move(cb); }

    void setGetPC(RegGetCallback16 cb) { getPC_ = cb; }
    void setSetPC(RegSetCallback16 cb) { setPC_ = cb; }
    void setSetX(RegSetCallback8 cb) { setX_ = cb; }
    void setSetY(RegSetCallback8 cb) { setY_ = cb; }

    // Activity tracking for UI
    bool hasActivity() const { return activity_; }
    bool isActivityWrite() const { return activityWrite_; }
    void clearActivity() { activity_ = false; }

private:
    void buildROM();
    bool hasAnyDevice() const;
    bool handleBoot();
    void handleProDOSBlock();
    void handleSmartPort();
    void setErrorResult(uint8_t errorCode);

    // Memory anywhere, falling back to bank zero on a machine with only that.
    uint8_t read24(uint32_t address) const;
    void write24(uint32_t address, uint8_t value);

    // What a device says about itself: the status byte every STATUS call
    // starts with, and the ProDOS byte at $CnFE that summarises the slot.
    uint8_t deviceStatusByte(int device) const;
    uint8_t prodosStatusByte() const;
    int deviceCount() const;

    uint8_t slotNum_ = 7;
    std::array<uint8_t, 256> rom_;
    std::array<BlockDevice, MAX_DEVICES> devices_;

    // Boot state: false until first boot completes, then ProDOS calls are handled
    bool booted_ = false;

    // Activity LED state
    bool activity_ = false;
    bool activityWrite_ = false;

    // Static empty string for when no device is loaded
    static const std::string emptyString_;

    // Callbacks
    MemReadCallback memRead_;
    MemWriteCallback memWrite_;
    MemRead24Callback memRead24_;
    MemWrite24Callback memWrite24_;
    RegGetCallback8 getA_;
    RegSetCallback8 setA_;
    RegGetCallback8 getP_;
    RegSetCallback8 setP_;
    RegGetCallback16 getSP_;
    RegSetCallback16 setSP_;
    ExecutingAtCallback executingAt_;
    RegGetCallback16 getPC_;
    RegSetCallback16 setPC_;
    RegSetCallback8 setX_;
    RegSetCallback8 setY_;
};

} // namespace a2e
