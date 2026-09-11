/*
 * disk2_card.cpp - Disk II controller card implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "disk2_card.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

Disk2Card::Disk2Card() {
    rom_.fill(0xFF);
}

Disk2Card::Disk2Card(const uint8_t* rom, size_t romSize) {
    rom_.fill(0xFF);
    if (rom && romSize > 0) {
        loadROM(rom, romSize);
    }
}

void Disk2Card::loadROM(const uint8_t* rom, size_t size) {
    if (rom && size > 0) {
        std::memcpy(rom_.data(), rom, std::min(size, rom_.size()));
    }
}

uint8_t Disk2Card::readROM(uint8_t offset) {
    return rom_[offset];
}

} // namespace a2e
