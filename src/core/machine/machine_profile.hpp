/*
 * machine_profile.hpp - Description of an Apple II machine variant
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace a2e {

// ============================================================================
// A machine profile is DATA, not polymorphism.
//
// The emulator models one machine at a time, and the parts of it that differ
// between an Apple //e, a II+ and a IIgs are overwhelmingly *numbers* — a clock
// rate, a scanline count, how much RAM answers, which CPU is fitted, whether
// the video generator inhibits colour burst in text mode. Those belong in a
// struct that the subsystems read.
//
// They are deliberately NOT virtual methods. `MMU::read`, `Video::emit*` and
// the CPU dispatch loop are the hottest code in the emulator; putting an
// indirect call on a per-cycle or per-dot path to serve a machine count that is
// currently one would cost real speed to buy nothing. When a second machine
// arrives, anything it cannot express as data — a 65816's 24-bit bus, the IIgs
// shadowing map, Super Hi-Res — wants its own subsystem class selected once at
// construction, not a branch taken sixty million times a second.
//
// So the rule is: if it is a number or a flag, it goes in the profile; if it is
// a different *mechanism*, it goes in a different class that the profile names.
// ============================================================================

// Identifies a machine. Serialized into save states, so values are stable and
// must never be renumbered.
enum class MachineId : uint8_t {
  AppleIIe = 0,
};

inline constexpr int MACHINE_COUNT = 1;

// ----------------------------------------------------------------------------
// Timing
//
// Everything the machine measures in CPU cycles. A //e's video and CPU are the
// same clock divided differently, which is why the video numbers live here
// rather than in the display section: a scanline is a count of cycles, not of
// pixels.
// ----------------------------------------------------------------------------
struct MachineTiming {
  double cpuClockHz;         // 1.023 MHz on a //e
  int cyclesPerScanline;     // 65: 40 visible columns + 25 of horizontal blank
  int hblankCycles;          // 25, and the scanner's clock 0 is this far in
  int visibleColumns;        // 40 columns of one byte each
  int scanlinesPerFrame;     // 262 total, including vertical blank
  int visibleScanlines;      // 192 lines actually drawn
  int mixedModeTextScanline; // First of the four text lines in mixed mode

  constexpr int cyclesPerFrame() const {
    return cyclesPerScanline * scanlinesPerFrame;
  }

  // Cycles of emulated time per audio sample at the host's sample rate. This is
  // the number that paces the whole emulator: audio asks for samples, and the
  // machine runs this many cycles for each one.
  constexpr double cyclesPerSample(int sampleRate) const {
    return cpuClockHz / static_cast<double>(sampleRate);
  }
};

// ----------------------------------------------------------------------------
// Memory
//
// Sizes only. How the address space is *decoded* is the MMU's business and
// differs by mechanism, not by number.
// ----------------------------------------------------------------------------
struct MachineMemory {
  size_t mainRamSize;
  size_t auxRamSize;         // 0 when the machine has no auxiliary bank
  size_t romSize;
  size_t charRomSize;
  size_t lcBankSize;         // Language card $D000 bank, 4KB
  size_t lcHighSize;         // Language card $E000-$FFFF, 8KB

  constexpr size_t totalRamSize() const { return mainRamSize + auxRamSize; }
};

// ----------------------------------------------------------------------------
// Display
//
// The framebuffer the core renders into. A //e emits 560 dots per visible line
// and the framebuffer is 560 wide, so the signal maps 1:1 onto pixels; lines
// are doubled vertically to square them up.
// ----------------------------------------------------------------------------
struct MachineDisplay {
  int dotsPerLine;   // Visible dots in the 14.31818 MHz stream
  int pixelWidth;
  int pixelHeight;
  int lineDoubling;  // Framebuffer rows per emitted scanline

  constexpr size_t framebufferSize() const {
    return static_cast<size_t>(pixelWidth) * static_cast<size_t>(pixelHeight) * 4;
  }
};

// ----------------------------------------------------------------------------
// Capabilities
//
// Flags for hardware a machine either has or does not. Each one exists because
// some other machine answers differently: a II+ has no auxiliary bank, no
// 80-column mode and never inhibits colour burst, which is why its text fringes
// green and violet in every mode where a //e's does not.
// ----------------------------------------------------------------------------
struct MachineCapabilities {
  bool hasAuxRam;
  bool has80Column;
  bool hasDoubleHires;
  bool hasLanguageCard;
  bool hasAltCharSet;
  bool hasOpenAppleKeys;      // Open/Closed Apple on $C061/$C062
  bool hasIOUDisable;         // $C07E/$C07F
  bool inhibitsBurstInText;   // Video generator kills burst on text lines
};

// ----------------------------------------------------------------------------
// Slots
//
// What the machine's expansion slots will accept. `fixedCard` names a slot the
// user cannot change (the //e's built-in 80-column card in slot 3);
// `defaultCard` is what a fresh machine ships with.
// ----------------------------------------------------------------------------
inline constexpr int MACHINE_SLOT_COUNT = 8; // Index by slot number; 0 unused

struct MachineSlot {
  const char *fixedCard;   // nullptr when the slot is user-configurable
  const char *defaultCard; // nullptr when the slot ships empty
};

// ----------------------------------------------------------------------------
// The profile itself
// ----------------------------------------------------------------------------
struct MachineProfile {
  MachineId id;
  const char *key;       // Stable identifier: "apple2e"
  const char *name;      // "Apple //e Enhanced"
  const char *shortName; // "//e"

  CPUVariant cpu;
  MachineTiming timing;
  MachineMemory memory;
  MachineDisplay display;
  MachineCapabilities caps;
  std::array<MachineSlot, MACHINE_SLOT_COUNT> slots;
};

// ============================================================================
// The Apple //e Enhanced
//
// Every number here was already in the codebase; this is where they now live.
// ============================================================================
inline constexpr MachineProfile APPLE_IIE_PROFILE = {
    MachineId::AppleIIe,
    "apple2e",
    "Apple //e Enhanced",
    "//e",
    CPUVariant::CMOS_65C02,
    // timing
    {
        1023000.0, // cpuClockHz
        65,        // cyclesPerScanline
        25,        // hblankCycles
        40,        // visibleColumns
        262,       // scanlinesPerFrame
        192,       // visibleScanlines
        160,       // mixedModeTextScanline
    },
    // memory
    {
        64 * 1024, // mainRamSize
        64 * 1024, // auxRamSize
        16 * 1024, // romSize
        8 * 1024,  // charRomSize
        4 * 1024,  // lcBankSize
        8 * 1024,  // lcHighSize
    },
    // display
    {
        560, // dotsPerLine
        560, // pixelWidth
        384, // pixelHeight
        2,   // lineDoubling
    },
    // caps
    {
        true,  // hasAuxRam
        true,  // has80Column
        true,  // hasDoubleHires
        true,  // hasLanguageCard
        true,  // hasAltCharSet
        true,  // hasOpenAppleKeys
        true,  // hasIOUDisable
        true,  // inhibitsBurstInText
    },
    // slots (index 0 unused; slot 3 is the built-in 80-column card)
    {{
        {nullptr, nullptr},      // 0: not a slot
        {nullptr, nullptr},      // 1: empty (parallel / SSC available)
        {nullptr, nullptr},      // 2: empty (parallel / SSC available)
        {"80col", "80col"},      // 3: built-in 80-column, fixed
        {nullptr, "mockingboard"},
        {nullptr, "thunderclock"},
        {nullptr, "disk2"},
        {nullptr, "smartport"},
    }},
};

// ============================================================================
// Registry
// ============================================================================
inline constexpr std::array<const MachineProfile *, MACHINE_COUNT>
    MACHINE_PROFILES = {{&APPLE_IIE_PROFILE}};

constexpr const MachineProfile &defaultMachineProfile() {
  return APPLE_IIE_PROFILE;
}

constexpr const MachineProfile &machineProfile(MachineId id) {
  const auto index = static_cast<size_t>(id);
  return index < MACHINE_PROFILES.size() ? *MACHINE_PROFILES[index]
                                         : APPLE_IIE_PROFILE;
}

constexpr const MachineProfile &machineProfileAt(int index) {
  return (index >= 0 && index < MACHINE_COUNT) ? *MACHINE_PROFILES[index]
                                               : APPLE_IIE_PROFILE;
}

// Constexpr-friendly string compare: <cstring> is not usable in a constant
// expression, and this is only ever comparing short profile keys.
constexpr bool machineKeyEquals(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) return a == b;
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

// Returns nullptr when no machine has this key, so a caller can tell a bad
// identifier from a valid one rather than silently getting a //e.
constexpr const MachineProfile *findMachineProfile(const char *key) {
  if (key == nullptr) return nullptr;
  for (const auto *profile : MACHINE_PROFILES) {
    if (machineKeyEquals(profile->key, key)) return profile;
  }
  return nullptr;
}

// ============================================================================
// The legacy constants in types.hpp size arrays at compile time, so they cannot
// simply become profile lookups. These assertions are what stop the two
// descriptions of the //e from drifting apart: change one and the build fails.
// ============================================================================
static_assert(APPLE_IIE_PROFILE.memory.mainRamSize == MAIN_RAM_SIZE);
static_assert(APPLE_IIE_PROFILE.memory.auxRamSize == AUX_RAM_SIZE);
static_assert(APPLE_IIE_PROFILE.memory.romSize == ROM_SIZE);
static_assert(APPLE_IIE_PROFILE.memory.charRomSize == CHAR_ROM_SIZE);
static_assert(APPLE_IIE_PROFILE.display.pixelWidth == SCREEN_WIDTH);
static_assert(APPLE_IIE_PROFILE.display.pixelHeight == SCREEN_HEIGHT);
static_assert(APPLE_IIE_PROFILE.display.framebufferSize() == FRAMEBUFFER_SIZE);
static_assert(APPLE_IIE_PROFILE.timing.cpuClockHz == CPU_CLOCK_HZ);
static_assert(APPLE_IIE_PROFILE.timing.cyclesPerScanline == CYCLES_PER_SCANLINE);
static_assert(APPLE_IIE_PROFILE.timing.scanlinesPerFrame == SCANLINES_PER_FRAME);
static_assert(APPLE_IIE_PROFILE.timing.cyclesPerFrame() == CYCLES_PER_FRAME);
static_assert(APPLE_IIE_PROFILE.timing.cyclesPerSample(AUDIO_SAMPLE_RATE) ==
              CYCLES_PER_SAMPLE);
// A scanline is its blanking plus one cycle per visible column, by definition.
static_assert(APPLE_IIE_PROFILE.timing.hblankCycles +
                  APPLE_IIE_PROFILE.timing.visibleColumns ==
              APPLE_IIE_PROFILE.timing.cyclesPerScanline);
// Each visible column clocks out 14 dots of the 14.31818 MHz stream.
static_assert(APPLE_IIE_PROFILE.timing.visibleColumns * 14 ==
              APPLE_IIE_PROFILE.display.dotsPerLine);
// The framebuffer holds every visible scanline, doubled.
static_assert(APPLE_IIE_PROFILE.timing.visibleScanlines *
                  APPLE_IIE_PROFILE.display.lineDoubling ==
              APPLE_IIE_PROFILE.display.pixelHeight);
// Mixed mode's text band is the tail of the visible area.
static_assert(APPLE_IIE_PROFILE.timing.mixedModeTextScanline <
              APPLE_IIE_PROFILE.timing.visibleScanlines);

} // namespace a2e
