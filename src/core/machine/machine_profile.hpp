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
  AppleIIPlus = 1,
};

inline constexpr int MACHINE_COUNT = 2;

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
  uint16_t romBaseAddress;   // Where the system ROM starts in the address space
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
  bool hasLanguageCard;       // Built into the machine, rather than a card
  bool hasAltCharSet;
  bool hasLowercase;          // An unmodified II+ cannot display lower case
  bool hasOpenAppleKeys;      // Open/Closed Apple on $C061/$C062
  bool hasIOUDisable;         // $C07E/$C07F
  bool hasInternalSlotRom;    // $C100-$CFFF ROM on the motherboard (INTCXROM)
  bool inhibitsBurstInText;   // Video generator kills burst on text lines
};

// ----------------------------------------------------------------------------
// Slots
//
// What the machine's expansion slots will accept. `fixedCard` names a slot the
// user cannot change (the //e's built-in 80-column card in slot 3);
// `defaultCard` is what a fresh machine ships with.
// ----------------------------------------------------------------------------
inline constexpr int MACHINE_SLOT_COUNT = 8; // Indexed by slot number

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

  // Which slot numbers physically exist. A //e has 1-7 with the language card
  // on the motherboard; a II+ has 0-7, and slot 0 is where the language card
  // goes.
  int firstSlot;
  int lastSlot;
  std::array<MachineSlot, MACHINE_SLOT_COUNT> slots;

  constexpr bool hasSlot(int slot) const {
    return slot >= firstSlot && slot <= lastSlot;
  }
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
        16 * 1024, // romSize — $C000-$FFFF, including the internal slot ROM
        0xC000,    // romBaseAddress
        8 * 1024,  // charRomSize — US and UK sets
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
        true,  // hasLanguageCard — on the motherboard, not a card
        true,  // hasAltCharSet
        true,  // hasLowercase
        true,  // hasOpenAppleKeys
        true,  // hasIOUDisable
        true,  // hasInternalSlotRom
        true,  // inhibitsBurstInText
    },
    1, // firstSlot
    7, // lastSlot
    // slots (slot 3 is the built-in 80-column card)
    {{
        {nullptr, nullptr},      // 0: a //e has no slot 0
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
// The Apple II Plus
//
// The machine the //e replaced, and the cheapest possible second profile: its
// video timing is the same circuit, so every number in MachineTiming is
// identical and the differences fall entirely in what the machine *has*.
//
// Four of those differences are the reason this profile is worth having, since
// each one exercises a different part of the seam:
//
//   - An NMOS 6502 rather than a 65C02. The CPU core already models both; the
//     profile is what selects one.
//   - No auxiliary bank, no 80-column mode, no double hi-res. Half the //e's
//     soft switches do not exist.
//   - 12KB of ROM at $D000 rather than 16KB at $C000, because a II+ has no
//     internal slot ROM.
//   - It never inhibits colour burst. This is the interesting one: a //e kills
//     the burst on text lines and so shows crisp white text, while a II+ sends
//     a reference on every line and its text fringes green and violet in every
//     mode. Video::burstForScanline() reads the flag, so this behaviour follows
//     from the profile alone.
// ============================================================================
inline constexpr MachineProfile APPLE_II_PLUS_PROFILE = {
    MachineId::AppleIIPlus,
    "apple2plus",
    "Apple II Plus",
    "II+",
    CPUVariant::NMOS_6502,
    // timing — the same video circuit, so the same numbers as a //e
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
        48 * 1024, // mainRamSize — the most the motherboard takes
        0,         // auxRamSize — there is no auxiliary bank
        12 * 1024, // romSize — $D000-$FFFF: Applesoft plus the monitor
        0xD000,    // romBaseAddress
        2 * 1024,  // charRomSize — upper case and the flashing/inverse sets
        4 * 1024,  // lcBankSize — for a language card fitted in slot 0
        8 * 1024,  // lcHighSize
    },
    // display — the same 40 columns of 14 dots, the same doubled 192 lines
    {
        560, // dotsPerLine
        560, // pixelWidth
        384, // pixelHeight
        2,   // lineDoubling
    },
    // caps
    {
        false, // hasAuxRam
        false, // has80Column
        false, // hasDoubleHires
        false, // hasLanguageCard — it is a card in slot 0, not built in
        false, // hasAltCharSet
        false, // hasLowercase — unmodified, the II+ cannot display it
        false, // hasOpenAppleKeys — $C061/$C062 are the paddle buttons
        false, // hasIOUDisable
        false, // hasInternalSlotRom — nothing answers at $C100-$CFFF
        false, // inhibitsBurstInText — burst on every line, so text fringes
    },
    0, // firstSlot — slot 0 exists, and is where a language card goes
    7, // lastSlot
    // slots: nothing is fixed on a II+, and nothing is fitted by default. Even
    // slot 3, which holds the //e's built-in 80-column card, is an ordinary
    // slot here.
    {{
        {nullptr, nullptr}, // 0: language card
        {nullptr, nullptr}, // 1
        {nullptr, nullptr}, // 2
        {nullptr, nullptr}, // 3
        {nullptr, nullptr}, // 4
        {nullptr, nullptr}, // 5
        {nullptr, "disk2"}, // 6: a II+ without a Disk II is not much use
        {nullptr, nullptr}, // 7
    }},
};

// ============================================================================
// Registry
// ============================================================================
// Ordered by MachineId, which machineProfile() relies on and a test pins.
inline constexpr std::array<const MachineProfile *, MACHINE_COUNT>
    MACHINE_PROFILES = {{&APPLE_IIE_PROFILE, &APPLE_II_PLUS_PROFILE}};

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
// Validation
//
// Two jobs. The first is stopping the //e's profile and the legacy constants in
// types.hpp from drifting apart, since the latter size arrays at compile time
// and so cannot simply become profile lookups: change one and the build fails.
//
// The second matters more now there is a second machine. Those same arrays are
// sized for the //e, which makes them the ceiling for *every* profile — a
// machine claiming more RAM or a bigger picture than the compiled storage would
// run off the end of it. So every registered profile is checked against the
// ceiling, and against its own internal consistency, at compile time.
// ============================================================================

// Every profile must fit the storage the build actually allocates.
constexpr bool profileFitsCompiledStorage(const MachineProfile &m) {
  return m.memory.mainRamSize <= MAIN_RAM_SIZE &&
         m.memory.auxRamSize <= AUX_RAM_SIZE &&
         m.memory.romSize <= ROM_SIZE &&
         m.memory.charRomSize <= CHAR_ROM_SIZE &&
         m.display.framebufferSize() <= FRAMEBUFFER_SIZE;
}

// ...and must describe a machine that could exist.
constexpr bool profileIsSelfConsistent(const MachineProfile &m) {
  // A scanline is its blanking plus one cycle per visible column.
  if (m.timing.hblankCycles + m.timing.visibleColumns !=
      m.timing.cyclesPerScanline)
    return false;
  // Each visible column clocks out 14 dots of the colour subcarrier stream.
  if (m.timing.visibleColumns * 14 != m.display.dotsPerLine) return false;
  // The framebuffer holds every visible scanline, doubled.
  if (m.timing.visibleScanlines * m.display.lineDoubling !=
      m.display.pixelHeight)
    return false;
  // Mixed mode's text band is the tail of the visible area.
  if (m.timing.mixedModeTextScanline >= m.timing.visibleScanlines) return false;
  // A machine with no auxiliary bank must not claim auxiliary RAM, and one
  // that has the bank must have some.
  if (m.caps.hasAuxRam != (m.memory.auxRamSize > 0)) return false;
  // Double hi-res is an 80-column mode; it cannot exist without one.
  if (m.caps.hasDoubleHires && !m.caps.has80Column) return false;
  // The ROM has to end at the top of the 16-bit address space.
  if (m.memory.romBaseAddress + m.memory.romSize != 0x10000) return false;
  // Slot numbers must be real and in order.
  if (m.firstSlot < 0 || m.lastSlot >= MACHINE_SLOT_COUNT) return false;
  if (m.firstSlot > m.lastSlot) return false;
  // Nothing may be fitted to a slot the machine does not have.
  for (int slot = 0; slot < MACHINE_SLOT_COUNT; slot++) {
    if (m.hasSlot(slot)) continue;
    if (m.slots[slot].fixedCard || m.slots[slot].defaultCard) return false;
  }
  return true;
}

constexpr bool allProfilesValid() {
  for (int i = 0; i < MACHINE_COUNT; i++) {
    const auto &m = *MACHINE_PROFILES[i];
    // The registry is ordered by id, which machineProfile() indexes directly.
    if (static_cast<int>(m.id) != i) return false;
    if (!profileFitsCompiledStorage(m)) return false;
    if (!profileIsSelfConsistent(m)) return false;
  }
  return true;
}

static_assert(allProfilesValid(),
              "A machine profile is inconsistent or does not fit the compiled "
              "storage — see profileIsSelfConsistent/profileFitsCompiledStorage");

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
// The relationships between these numbers are checked for every machine by
// profileIsSelfConsistent() above, not just for the //e.

} // namespace a2e
