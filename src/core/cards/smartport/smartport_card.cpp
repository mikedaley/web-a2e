/*
 * smartport_card.cpp - SmartPort expansion card for ProDOS block devices
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "smartport_card.hpp"
#include <cstring>

namespace a2e {

const std::string SmartPortCard::emptyString_;

// A card's own slot ROM puts the ProDOS entry at $Cn10 and the SmartPort
// entry three past it (prodosEntry_'s default). See setProDOSEntry for the
// IIgs's layout.
// Boot trigger offset in I/O space
static constexpr uint8_t BOOT_IO_OFFSET = 0x00;

// SmartPort error codes
static constexpr uint8_t SP_OK = 0x00;
static constexpr uint8_t SP_IO_ERROR = 0x27;
static constexpr uint8_t SP_NO_DEVICE = 0x28;
static constexpr uint8_t SP_WRITE_PROTECTED = 0x2B;
static constexpr uint8_t SP_BAD_CODE = 0x21;   // No such status or control code
static constexpr uint8_t SP_BAD_UNIT = 0x11;   // Unit number out of range

SmartPortCard::SmartPortCard() {
    rom_.fill(0);
    buildROM();
}

void SmartPortCard::setSlotNumber(uint8_t slot) {
    if (slot < 1 || slot > 7) return;
    slotNum_ = slot;
    buildROM();
}

void SmartPortCard::setProDOSEntry(uint8_t offset) {
    prodosEntry_ = offset;
    buildROM();
}

void SmartPortCard::buildROM() {
    rom_.fill(0);

    // Signature bytes for ProDOS device discovery.
    // ProDOS checks odd bytes at $Cn01, $Cn03, $Cn05, $Cn07.
    // We embed them as LDA# operands so they occupy the right offsets.

    // $00: LDA #$20  -> $Cn01 = $20 (ProDOS signature)
    rom_[0x00] = 0xA9; rom_[0x01] = 0x20;
    // $02: LDA #$00  -> $Cn03 = $00 (block device)
    rom_[0x02] = 0xA9; rom_[0x03] = 0x00;
    // $04: LDA #$03  -> $Cn05 = $03 (identifies as SmartPort-capable)
    rom_[0x04] = 0xA9; rom_[0x05] = 0x03;
    // $06: LDA #$00  -> $Cn07 = $00 (SmartPort present)
    rom_[0x06] = 0xA9; rom_[0x07] = 0x00;

    // Boot code: set X to slot*16, trigger the I/O boot, and RTS to $0801.
    // This path is used by PR#n from BASIC (execution falls through from
    // $Cn00) and by the autostart ROM.
    const uint8_t slotOffset = slotNum_ << 4; // slot * 16
    const uint8_t ioAddr = 0x80 + slotOffset; // $C0n0 base
    const uint8_t prodos = prodosEntry_;
    const uint8_t smartPort = smartPortEntry();

    // Where the six-byte boot stub goes depends on where the entries are. A
    // card's own layout has room for it at $08, below entries at $10 and $13.
    // The IIgs's layout has the entries at $0A and $0D, so the fall-through
    // from $07 branches over them to a stub at $10 — BRA is a 65C02
    // instruction, and every machine with this layout has one.
    uint8_t stub = 0x08;
    if (prodos < 0x10) {
        stub = 0x10;
        rom_[0x08] = 0x80;                                      // BRA
        rom_[0x09] = static_cast<uint8_t>(stub - 0x0A);
    }
    // LDX #$n0 (set X to slot*16, needed by ProDOS boot block)
    rom_[stub + 0] = 0xA2;
    rom_[stub + 1] = slotOffset;
    // STX $C0n0 (trigger I/O trap for boot block load)
    rom_[stub + 2] = 0x8E;
    rom_[stub + 3] = ioAddr;
    rom_[stub + 4] = 0xC0;
    // RTS - if boot succeeded, writeIO pushed $0800 on stack so RTS goes to $0801.
    // If no disk loaded, RTS returns to the autostart ROM caller which scans the next slot.
    rom_[stub + 5] = 0x60;

    // ProDOS block device entry: SEC + RTS (trapped by readROM)
    rom_[prodos] = 0x38;     // SEC
    rom_[prodos + 1] = 0x60; // RTS
    rom_[prodos + 2] = 0xEA; // NOP
    // SmartPort entry: SEC + RTS (trapped by readROM)
    rom_[smartPort] = 0x38;     // SEC
    rom_[smartPort + 1] = 0x60; // RTS

    // $FB: the SmartPort ID type byte. Bit 7 says the card takes extended
    // calls — the ones with a four-byte pointer, which is how anything on a
    // 65816 reads into a bank other than zero. GS/OS looks here before it will
    // build a driver for the slot, and will not touch a card that says no.
    rom_[0xFB] = 0x80;

    // $FE, the ProDOS status byte, depends on how many devices are fitted and
    // is answered by readROM rather than baked in here.

    // $FF: ProDOS entry point offset (used by both autostart ROM boot and ProDOS driver)
    // The readROM trap at the entry distinguishes boot vs ProDOS calls via the booted_ flag.
    rom_[0xFF] = prodos;
}

int SmartPortCard::deviceCount() const {
    int count = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices_[i].isLoaded()) count = i + 1;
    }
    return count;
}

uint8_t SmartPortCard::prodosStatusByte() const {
    // Bits 5-4 are one less than the number of volumes; the low four say the
    // slot can be asked for status, read, written and formatted.
    //
    // A IIgs's own slot 5 firmware answers $BF whatever is plugged in: four
    // volumes, removable, interrupting — the port, not the drives on it. And
    // ProDOS 8 1.x depends on the two drives that implies: its device-table
    // builder pushes a byte for every device that is not the boot device and
    // pops one for every other device in the boot slot, which only balances
    // when the boot slot has a drive 2. A card that reported the one image
    // it held sent ProDOS 8 1.4 into a BRK after its splash screen, on every
    // demo disk that boots it.
    if (prodosEntry_ < 0x10) return 0xBF;
    const int volumes = deviceCount() > 0 ? deviceCount() : 1;
    return static_cast<uint8_t>(((volumes - 1) << 4) | 0x0F);
}

uint8_t SmartPortCard::deviceStatusByte(int device) const {
    // Block device, writable, readable, online, formattable — and write
    // protected if the image is.
    uint8_t status = 0xF8;
    if (devices_[device].isWriteProtected()) status |= 0x04;
    return status;
}

uint8_t SmartPortCard::read24(uint32_t address) const {
    if (memRead24_) return memRead24_(address);
    return memRead_ ? memRead_(static_cast<uint16_t>(address)) : 0;
}

void SmartPortCard::write24(uint32_t address, uint8_t value) {
    if (memWrite24_) { memWrite24_(address, value); return; }
    if (memWrite_) memWrite_(static_cast<uint16_t>(address), value);
}

bool SmartPortCard::hasAnyDevice() const {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices_[i].isLoaded()) return true;
    }
    return false;
}

void SmartPortCard::reset() {
    booted_ = false;
    activity_ = false;
    activityWrite_ = false;
}

uint8_t SmartPortCard::readIO(uint8_t offset) {
    (void)offset;
    return 0xFF;
}

void SmartPortCard::writeIO(uint8_t offset, uint8_t value) {
    // The boot trap answers the ROM stub's `STX $C0n0` once. It must not
    // answer a running program that happens to write there — and it must
    // never answer a stack that has wandered into the I/O page, where every
    // push is a write to this card and the boot's own pushes would come
    // straight back here, without end.
    if (offset == BOOT_IO_OFFSET && !booted_) {
        // Boot trap: load block 0 of device 0 into $0800
        if (!devices_[0].isLoaded() || !memWrite_ || !getSP_ || !setSP_) {
            // Mark as booted so subsequent ProDOS calls to $Cn10 are handled
            // as block device driver calls rather than triggering another boot.
            booted_ = true;
            // No disk loaded. Scan lower slots for the next bootable device,
            // mimicking the autostart ROM's 7→1 scan that was interrupted.
            if (setPC_ && memRead_) {
                for (int slot = slotNum_ - 1; slot >= 1; slot--) {
                    uint16_t base = 0xC000 | (slot << 8);
                    uint8_t sig = memRead_(base + 1); // $Cn01
                    if (sig == 0x20) {
                        // ProDOS-compatible device found (Disk II, SmartPort, etc.)
                        uint8_t entry = memRead_(base + 0xFF);
                        setPC_(base + (entry ? entry : 0));
                        return;
                    }
                }
                // No bootable slot found — fall through to Applesoft prompt
                setPC_(0xE003);
            }
            return;
        }

        uint8_t blockBuf[BlockDevice::BLOCK_SIZE];
        if (devices_[0].readBlock(0, blockBuf)) {
            for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) {
                memWrite_(static_cast<uint16_t>(0x0800 + i), blockBuf[i]);
            }
            activity_ = true;
            activityWrite_ = false;

            // Set X to slot*16 (ProDOS boot block expects this)
            if (setX_) setX_(slotNum_ << 4);

            // Push $0800 onto the stack so the RTS in boot ROM goes to $0801
            uint16_t sp = getSP_();
            memWrite_(sp, 0x08); // high byte
            sp--;
            memWrite_(sp, 0x00); // low byte
            sp--;
            setSP_(sp);
        }
    }
}

uint8_t SmartPortCard::peekROM(uint8_t offset) {
    // What the CPU would fetch, minus the trap: a debugger looking at the
    // entry point sees the SEC/RTS that is really there.
    if (!hasAnyDevice()) return 0;
    if (offset == 0xFE) return prodosStatusByte();
    return rom_[offset];
}

uint8_t SmartPortCard::readROM(uint8_t offset) {
    // When no devices are loaded, hide the ROM so ProDOS doesn't detect this slot
    if (!hasAnyDevice()) return 0;

    // The entry points are traps, so what matters is whether the CPU is
    // *executing* this byte rather than reading it as data — a ProDOS scan
    // reads the same addresses and must be given the ROM. The machine answers
    // that, because only it knows what its processor has done to the program
    // counter by the time this read arrives. See setExecutingAt.
    const uint16_t here =
        (0xC000 | (static_cast<uint16_t>(slotNum_) << 8)) + offset;

    if (offset == 0xFE) return prodosStatusByte();

    if (executingAt_ && executingAt_(here)) {
        if (offset == prodosEntry_) {
            if (!booted_) {
                // First call to entry point = boot (from autostart ROM or PR#n fallthrough)
                if (!handleBoot()) {
                    // No disk loaded. Scan lower slots for the next bootable
                    // device, continuing the autostart ROM's 7→1 scan.
                    if (setPC_ && memRead_) {
                        for (int slot = slotNum_ - 1; slot >= 1; slot--) {
                            uint16_t base = 0xC000 | (slot << 8);
                            uint8_t sig = memRead_(base + 1);
                            if (sig == 0x20) {
                                uint8_t entry = memRead_(base + 0xFF);
                                setPC_(base + (entry ? entry : 0));
                                return 0xEA; // NOP
                            }
                        }
                        setPC_(0xE003); // No bootable slot — Applesoft prompt
                    }
                    return 0xEA; // NOP (harmless; CPU continues from new PC)
                }
            } else {
                // Subsequent calls = ProDOS block driver calls
                handleProDOSBlock();
            }
            return 0x60; // RTS
        }
        if (offset == smartPortEntry()) {
                    handleSmartPort();
            return 0x60; // RTS
        }
    }

    return rom_[offset];
}

bool SmartPortCard::handleBoot() {
    // Called when the autostart ROM or PR#n reaches the entry point for the first time.
    // Load block 0 of device 0 into $0800, set X to slot*16, and arrange
    // for the CPU to jump to $0801 (ProDOS boot block entry) via RTS.
    booted_ = true;

    if (!devices_[0].isLoaded() || !memWrite_ || !getSP_ || !setSP_) return false;

    // Load block 0 into $0800
    uint8_t blockBuf[BlockDevice::BLOCK_SIZE];
    if (!devices_[0].readBlock(0, blockBuf)) return false;

    for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) {
        memWrite_(static_cast<uint16_t>(0x0800 + i), blockBuf[i]);
    }

    activity_ = true;
    activityWrite_ = false;

    // Set X to slot*16 (ProDOS boot block expects this)
    if (setX_) setX_(slotNum_ << 4);

    // Push $0800 onto the stack so RTS goes to $0801 (RTS adds 1 to popped address)
    uint16_t sp = getSP_();
    memWrite_(sp, 0x08); // high byte
    sp--;
    memWrite_(sp, 0x00); // low byte
    sp--;
    setSP_(sp);
    return true;
}

void SmartPortCard::setErrorResult(uint8_t errorCode) {
    if (!setA_ || !getP_ || !setP_) return;

    setA_(errorCode);

    uint8_t p = getP_();
    if (errorCode != SP_OK) {
        p |= 0x01;  // Set carry (error)
    } else {
        p &= ~0x01; // Clear carry (success)
    }
    setP_(p);
}

void SmartPortCard::handleProDOSBlock() {
    // ProDOS block call convention:
    // $42 = command (0=STATUS, 1=READ, 2=WRITE, 3=FORMAT)
    // $43 = unit number (bit 7: 0=device 0, 1=device 1; bits 4-6: slot)
    // $44-$45 = buffer pointer (lo/hi)
    // $46-$47 = block number (lo/hi)
    if (!memRead_ || !memWrite_ || !setA_ || !setP_) return;

    uint8_t command = memRead_(0x42);
    uint8_t unitNum = memRead_(0x43);
    uint16_t bufPtr = memRead_(0x44) | (memRead_(0x45) << 8);
    uint16_t blockNum = memRead_(0x46) | (memRead_(0x47) << 8);

    // Device index from unit number bit 7
    int device = (unitNum & 0x80) ? 1 : 0;

    activity_ = true;
    activityWrite_ = (command == 2);

    switch (command) {
        case 0: { // STATUS
            if (!devices_[device].isLoaded()) {
                setErrorResult(SP_NO_DEVICE);
                return;
            }
            // ProDOS expects block count in X (lo) and Y (hi) registers
            uint16_t blocks = devices_[device].getTotalBlocks();
            if (setX_) setX_(blocks & 0xFF);
            if (setY_) setY_((blocks >> 8) & 0xFF);
            setErrorResult(SP_OK);
            return;
        }

        case 1: { // READ
            if (!devices_[device].isLoaded()) {
                setErrorResult(SP_NO_DEVICE);
                return;
            }
            if (onTransfer_) onTransfer_(1, device, blockNum, memRead_(0x44) | (memRead_(0x45) << 8));
            uint8_t blockBuf[BlockDevice::BLOCK_SIZE];
            if (!devices_[device].readBlock(blockNum, blockBuf)) {
                setErrorResult(SP_IO_ERROR);
                return;
            }
            for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) {
                memWrite_(static_cast<uint16_t>(bufPtr + i), blockBuf[i]);
            }
            setErrorResult(SP_OK);
            return;
        }

        case 2: { // WRITE
            if (!devices_[device].isLoaded()) {
                setErrorResult(SP_NO_DEVICE);
                return;
            }
            if (devices_[device].isWriteProtected()) {
                setErrorResult(SP_WRITE_PROTECTED);
                return;
            }
            uint8_t blockBuf[BlockDevice::BLOCK_SIZE];
            for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) {
                blockBuf[i] = memRead_(static_cast<uint16_t>(bufPtr + i));
            }
            if (!devices_[device].writeBlock(blockNum, blockBuf)) {
                setErrorResult(SP_IO_ERROR);
                return;
            }
            setErrorResult(SP_OK);
            return;
        }

        case 3: { // FORMAT
            // We don't actually format - just return OK if device is present
            if (!devices_[device].isLoaded()) {
                setErrorResult(SP_NO_DEVICE);
                return;
            }
            setErrorResult(SP_OK);
            return;
        }

        default:
            setErrorResult(SP_IO_ERROR);
            return;
    }
}

void SmartPortCard::handleSmartPort() {
    // A SmartPort call is `JSR $Cn13` followed by three inline bytes — the
    // command and a pointer to its parameter list — that the caller expects to
    // be stepped over on return. So the return address on the stack is read,
    // used, and put back three further on.
    //
    // There are two dialects. The standard call has a two-byte pointer and a
    // three-byte block number; the extended call, command bit 6, has four of
    // each, which is what a 65816 needs to read into a bank other than zero —
    // and what GS/OS uses for everything once $CnFB has told it the card can.
    if (!memRead_ || !memWrite_ || !setA_ || !setP_ || !getSP_ || !setSP_) return;

    const uint16_t sp = getSP_();
    const uint16_t retAddr =
        static_cast<uint16_t>(memRead_(sp + 1) | (memRead_(sp + 2) << 8));
    const uint16_t inlineAddr = retAddr + 1;
    const uint8_t command = memRead_(inlineAddr);
    const bool extended = (command & 0x40) != 0;
    const uint8_t op = command & 0x3F;

    // The inline pointer to the parameter list is two bytes in a standard
    // call and four in an extended one — so the return address is stepped
    // over three bytes or five, and getting that wrong returns into the middle
    // of the pointer and executes it.
    uint32_t paramPtr = memRead_(inlineAddr + 1) | (memRead_(inlineAddr + 2) << 8);
    if (extended) paramPtr |= static_cast<uint32_t>(memRead_(inlineAddr + 3)) << 16;

    const uint16_t newRet = static_cast<uint16_t>(retAddr + (extended ? 5 : 3));
    memWrite_(sp + 1, static_cast<uint8_t>(newRet));
    memWrite_(sp + 2, static_cast<uint8_t>(newRet >> 8));

    // Parameter list: count, unit, then a pointer whose width is the dialect's.
    // The list itself can be in any bank an extended caller cares to name.
    const uint8_t unitNum = read24(paramPtr + 1);
    uint32_t pointer = read24(paramPtr + 2) | (read24(paramPtr + 3) << 8);
    if (extended) pointer |= static_cast<uint32_t>(read24(paramPtr + 4)) << 16;
    const uint32_t afterPointer = paramPtr + (extended ? 6 : 4);

    activity_ = true;
    activityWrite_ = (op == 0x02);

    auto writeCount = [this](uint16_t count) {
        // STATUS reports how many bytes it wrote, low byte in X and high in Y.
        if (setX_) setX_(static_cast<uint8_t>(count));
        if (setY_) setY_(static_cast<uint8_t>(count >> 8));
    };

    switch (op) {
        case 0x00: { // STATUS
            const uint8_t statusCode = read24(afterPointer);

            if (unitNum == 0) {
                // The card itself. Code 0 is eight bytes: how many devices,
                // whether any is interrupting, and a vendor and version nobody
                // checks.
                if (statusCode != 0x00) { setErrorResult(SP_BAD_CODE); return; }
                write24(pointer + 0, static_cast<uint8_t>(deviceCount()));
                write24(pointer + 1, 0x00); // no interrupts pending
                write24(pointer + 2, 0x00); write24(pointer + 3, 0x00); // vendor
                write24(pointer + 4, 0x00); write24(pointer + 5, 0x01); // version
                write24(pointer + 6, 0x00); write24(pointer + 7, 0x00);
                writeCount(8);
                setErrorResult(SP_OK);
                return;
            }

            const int device = unitNum - 1;
            if (device < 0 || device >= MAX_DEVICES) { setErrorResult(SP_BAD_UNIT); return; }
            if (!devices_[device].isLoaded()) { setErrorResult(SP_NO_DEVICE); return; }

            const uint32_t blocks = devices_[device].getTotalBlocks();
            switch (statusCode) {
            case 0x00: // General status: the status byte and a 3-byte block count
                write24(pointer + 0, deviceStatusByte(device));
                write24(pointer + 1, static_cast<uint8_t>(blocks));
                write24(pointer + 2, static_cast<uint8_t>(blocks >> 8));
                write24(pointer + 3, static_cast<uint8_t>(blocks >> 16));
                writeCount(4);
                setErrorResult(SP_OK);
                return;
            case 0x03: {
                // The Device Information Block: the general status, then a
                // name, a type and a version. This is what an operating system
                // reads to decide what it has found, and GS/OS will not build
                // a driver for a device it cannot ask.
                write24(pointer + 0, deviceStatusByte(device));
                write24(pointer + 1, static_cast<uint8_t>(blocks));
                write24(pointer + 2, static_cast<uint8_t>(blocks >> 8));
                write24(pointer + 3, static_cast<uint8_t>(blocks >> 16));
                static const char NAME[16] = {'A','P','P','L','E','M',' ','H','A','R','D',' ','D','I','S','K'};
                write24(pointer + 4, 16); // name length
                for (int i = 0; i < 16; i++) write24(pointer + 5 + i, static_cast<uint8_t>(NAME[i]));
                write24(pointer + 21, 0x02); // device type: hard disk
                write24(pointer + 22, 0xA0); // subtype: extended calls, not removable
                write24(pointer + 23, 0x00); // firmware version
                write24(pointer + 24, 0x01);
                writeCount(25);
                setErrorResult(SP_OK);
                return;
            }
            default:
                setErrorResult(SP_BAD_CODE);
                return;
            }
        }

        case 0x01:   // READ BLOCK
        case 0x02: { // WRITE BLOCK
            // A standard block number is three bytes and an extended one four.
            uint32_t blockNum = read24(afterPointer) |
                                (read24(afterPointer + 1) << 8) |
                                (static_cast<uint32_t>(read24(afterPointer + 2)) << 16);
            if (extended) blockNum |= static_cast<uint32_t>(read24(afterPointer + 3)) << 24;

            const int device = unitNum - 1;
            if (device < 0 || device >= MAX_DEVICES) { setErrorResult(SP_BAD_UNIT); return; }
            if (!devices_[device].isLoaded()) { setErrorResult(SP_NO_DEVICE); return; }

            if (onTransfer_) onTransfer_(op, device, blockNum, pointer);
            uint8_t blockBuf[BlockDevice::BLOCK_SIZE];
            if (op == 0x01) {
                if (!devices_[device].readBlock(blockNum, blockBuf)) { setErrorResult(SP_IO_ERROR); return; }
                for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) write24(pointer + i, blockBuf[i]);
            } else {
                if (devices_[device].isWriteProtected()) { setErrorResult(SP_WRITE_PROTECTED); return; }
                for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) blockBuf[i] = read24(pointer + i);
                if (!devices_[device].writeBlock(blockNum, blockBuf)) { setErrorResult(SP_IO_ERROR); return; }
            }
            setErrorResult(SP_OK);
            return;
        }

        case 0x03: { // FORMAT: the image is already a formatted volume
            const int device = unitNum - 1;
            if (device < 0 || device >= MAX_DEVICES) { setErrorResult(SP_BAD_UNIT); return; }
            if (!devices_[device].isLoaded()) { setErrorResult(SP_NO_DEVICE); return; }
            setErrorResult(SP_OK);
            return;
        }

        case 0x04: // CONTROL
        case 0x05: // INIT
            setErrorResult(SP_OK);
            return;

        default:
            setErrorResult(SP_IO_ERROR);
            return;
    }
}

// Device management

bool SmartPortCard::insertImage(int device, const uint8_t* data, size_t size, const std::string& filename) {
    if (device < 0 || device >= MAX_DEVICES) return false;
    return devices_[device].load(data, size, filename);
}

void SmartPortCard::ejectImage(int device) {
    if (device >= 0 && device < MAX_DEVICES) {
        devices_[device].eject();
    }
}

bool SmartPortCard::isImageInserted(int device) const {
    if (device < 0 || device >= MAX_DEVICES) return false;
    return devices_[device].isLoaded();
}

const std::string& SmartPortCard::getImageFilename(int device) const {
    if (device < 0 || device >= MAX_DEVICES) return emptyString_;
    return devices_[device].getFilename();
}

bool SmartPortCard::isImageModified(int device) const {
    if (device < 0 || device >= MAX_DEVICES) return false;
    return devices_[device].isModified();
}

const uint8_t* SmartPortCard::exportImageData(int device, size_t* size) const {
    if (device < 0 || device >= MAX_DEVICES) {
        if (size) *size = 0;
        return nullptr;
    }
    return devices_[device].exportData(size);
}

const uint8_t* SmartPortCard::getBlockData(int device, size_t* size) const {
    if (device < 0 || device >= MAX_DEVICES) {
        if (size) *size = 0;
        return nullptr;
    }
    return devices_[device].getBlockData(size);
}

BlockDevice* SmartPortCard::getDevice(int device) {
    if (device < 0 || device >= MAX_DEVICES) return nullptr;
    return &devices_[device];
}

const BlockDevice* SmartPortCard::getDevice(int device) const {
    if (device < 0 || device >= MAX_DEVICES) return nullptr;
    return &devices_[device];
}

// State serialization

size_t SmartPortCard::getStateSize() const {
    // slotNum(1) + per-device state
    size_t total = 1;
    for (int i = 0; i < MAX_DEVICES; i++) {
        total += 4; // device state size prefix (LE32)
        if (devices_[i].isLoaded()) {
            total += devices_[i].getStateSize();
        }
    }
    return total;
}

size_t SmartPortCard::serialize(uint8_t* buffer, size_t maxSize) const {
    if (maxSize < 1) return 0;

    size_t offset = 0;
    buffer[offset++] = slotNum_;

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices_[i].isLoaded()) {
            size_t devStateSize = devices_[i].getStateSize();
            if (offset + 4 + devStateSize > maxSize) return 0;

            // Write device state size
            uint32_t sz = static_cast<uint32_t>(devStateSize);
            buffer[offset++] = sz & 0xFF;
            buffer[offset++] = (sz >> 8) & 0xFF;
            buffer[offset++] = (sz >> 16) & 0xFF;
            buffer[offset++] = (sz >> 24) & 0xFF;

            size_t written = devices_[i].serialize(buffer + offset, maxSize - offset);
            if (written == 0) return 0;
            offset += written;
        } else {
            if (offset + 4 > maxSize) return 0;
            buffer[offset++] = 0;
            buffer[offset++] = 0;
            buffer[offset++] = 0;
            buffer[offset++] = 0;
        }
    }

    return offset;
}

size_t SmartPortCard::deserialize(const uint8_t* buffer, size_t size) {
    if (size < 1) return 0;

    size_t offset = 0;
    slotNum_ = buffer[offset++];
    buildROM();

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (offset + 4 > size) return 0;

        uint32_t devStateSize = buffer[offset] | (buffer[offset + 1] << 8) |
                                (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
        offset += 4;

        if (devStateSize > 0) {
            if (offset + devStateSize > size) return 0;
            size_t read = devices_[i].deserialize(buffer + offset, devStateSize);
            if (read == 0) return 0;
            offset += read;
        } else {
            devices_[i].eject();
        }
    }

    return offset;
}

} // namespace a2e
