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
    void setGetA(RegGetCallback8 cb) { getA_ = cb; }
    void setSetA(RegSetCallback8 cb) { setA_ = cb; }
    void setGetP(RegGetCallback8 cb) { getP_ = cb; }
    void setSetP(RegSetCallback8 cb) { setP_ = cb; }
    void setGetSP(RegGetCallback8 cb) { getSP_ = cb; }
    void setSetSP(RegSetCallback8 cb) { setSP_ = cb; }
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
    RegGetCallback8 getA_;
    RegSetCallback8 setA_;
    RegGetCallback8 getP_;
    RegSetCallback8 setP_;
    RegGetCallback8 getSP_;
    RegSetCallback8 setSP_;
    ExecutingAtCallback executingAt_;
    RegGetCallback16 getPC_;
    RegSetCallback16 setPC_;
    RegSetCallback8 setX_;
    RegSetCallback8 setY_;
};

} // namespace a2e
