# Memory System

This page describes the MMU (Memory Management Unit) implementation, covering the 128KB memory layout, bank switching, Language Card, soft switch map, expansion slot ROM routing, floating bus behavior, and memory visibility rules.

---

## Table of Contents

- [Overview](#overview)
- [Memory Layout](#memory-layout)
- [Bank Switching](#bank-switching)
- [80STORE Mode](#80store-mode)
- [Language Card](#language-card)
- [Soft Switch Map](#soft-switch-map)
- [Slot ROM Space](#slot-rom-space)
- [Floating Bus](#floating-bus)
- [Keyboard and Input](#keyboard-and-input)
- [Paddle Timers](#paddle-timers)
- [Memory Access Tracking](#memory-access-tracking)

---

## Overview

The MMU is implemented in `src/core/mmu/mmu.cpp` and `mmu.hpp`. It manages the full 128KB address space of the Apple IIe Enhanced (64KB main + 64KB auxiliary RAM), plus 16KB of system ROM, 8KB of character ROM, and the Language Card RAM banks. All CPU memory accesses route through the MMU, which decodes addresses and dispatches to the appropriate memory bank, soft switch handler, or expansion card.

The MMU provides two read interfaces:
- `read()` -- Normal read with full side effects (soft switch triggers, watchpoint callbacks, access tracking)
- `peek()` -- Non-side-effecting read for the debugger and memory viewer

---

## Memory Layout

### Physical Memory

| Memory | Size | Storage |
|--------|------|---------|
| Main RAM | 64 KB | `mainRAM_[65536]` |
| Auxiliary RAM | 64 KB | `auxRAM_[65536]` |
| Language Card Bank 1 (main) | 4 KB | `lcBank1_[4096]` |
| Language Card Bank 2 (main) | 4 KB | `lcBank2_[4096]` |
| Language Card High (main) | 8 KB | `lcHighRAM_[8192]` |
| Language Card Bank 1 (aux) | 4 KB | `auxLcBank1_[4096]` |
| Language Card Bank 2 (aux) | 4 KB | `auxLcBank2_[4096]` |
| Language Card High (aux) | 8 KB | `auxLcHighRAM_[8192]` |
| System ROM | 16 KB | `systemROM_[16384]` |
| Character ROM | 8 KB | `charROM_[8192]` |

### Address Map

| Address Range | Default Mapping | Notes |
|---------------|----------------|-------|
| `$0000`-`$01FF` | Main zero page and stack | ALTZP switches to aux |
| `$0200`-`$03FF` | Main RAM | RAMRD/RAMWRT for aux |
| `$0400`-`$07FF` | Text page 1 | 80STORE overrides RAMRD/RAMWRT |
| `$0800`-`$0BFF` | Text page 2 | RAMRD/RAMWRT for aux |
| `$0C00`-`$1FFF` | Main RAM | RAMRD/RAMWRT for aux |
| `$2000`-`$3FFF` | HiRes page 1 | 80STORE+HIRES overrides RAMRD/RAMWRT |
| `$4000`-`$5FFF` | HiRes page 2 | RAMRD/RAMWRT for aux |
| `$6000`-`$BFFF` | Main RAM | RAMRD/RAMWRT for aux |
| `$C000`-`$C00F` | Soft switches (write) / Keyboard (read) | Write-only switches; reads return keyboard latch |
| `$C010`-`$C01F` | Status reads / Keyboard strobe | Bit 7 = status, bits 0-6 = floating bus |
| `$C020` | Cassette output toggle | |
| `$C030` | Speaker toggle | |
| `$C050`-`$C05F` | Display and annunciator switches | Both read and write trigger |
| `$C060`-`$C063` | Cassette input, buttons | |
| `$C064`-`$C067` | Paddle inputs | Timer-based |
| `$C070` | Paddle trigger | |
| `$C07F` | DHIRES status | |
| `$C080`-`$C08F` | Language Card switches | See Language Card section |
| `$C090`-`$C0FF` | Slot I/O (16 bytes per slot) | Routes to expansion cards |
| `$C100`-`$C7FF` | Slot ROM (256 bytes per slot) | INTCXROM can override |
| `$C800`-`$CFFF` | Expansion ROM / Internal ROM | Shared 2KB expansion space |
| `$D000`-`$DFFF` | ROM or Language Card bank 1/2 | |
| `$E000`-`$FFFF` | ROM or Language Card high RAM | Contains reset/IRQ vectors |

---

## Bank Switching

The Apple IIe has several soft switches that control which physical memory bank is visible at a given address range:

### RAMRD / RAMWRT (`$C002`-`$C005`)

| Switch | Address | Effect |
|--------|---------|--------|
| RAMRD off | `$C002` (write) | Reads from `$0200`-`$BFFF` go to main RAM |
| RAMRD on | `$C003` (write) | Reads from `$0200`-`$BFFF` go to aux RAM |
| RAMWRT off | `$C004` (write) | Writes to `$0200`-`$BFFF` go to main RAM |
| RAMWRT on | `$C005` (write) | Writes to `$0200`-`$BFFF` go to aux RAM |

### ALTZP (`$C008`-`$C009`)

| Switch | Address | Effect |
|--------|---------|--------|
| ALTZP off | `$C008` (write) | Zero page (`$00`-`$FF`) and stack (`$100`-`$1FF`) use main RAM; Language Card uses main banks |
| ALTZP on | `$C009` (write) | Zero page and stack use aux RAM; Language Card uses aux banks |

### INTCXROM (`$C006`-`$C007`)

| Switch | Address | Effect |
|--------|---------|--------|
| INTCXROM off | `$C006` (write) | `$C100`-`$CFFF` uses slot card ROMs |
| INTCXROM on | `$C007` (write) | `$C100`-`$CFFF` uses internal system ROM |

### SLOTC3ROM (`$C00A`-`$C00B`)

| Switch | Address | Effect |
|--------|---------|--------|
| SLOTC3ROM off | `$C00A` (write) | `$C300`-`$C3FF` uses internal 80-column ROM; activates `$C800` internal ROM |
| SLOTC3ROM on | `$C00B` (write) | `$C300`-`$C3FF` uses slot 3 card ROM (if present) |

---

## 80STORE Mode

When 80STORE is enabled (`$C001`), the PAGE2 switch (`$C054`/`$C055`) controls main/aux bank selection for display memory, overriding RAMRD and RAMWRT:

- **Text page 1** (`$0400`-`$07FF`): PAGE2 off = main RAM, PAGE2 on = aux RAM
- **HiRes page 1** (`$2000`-`$3FFF`): Only when both 80STORE and HIRES are on. PAGE2 off = main RAM, PAGE2 on = aux RAM

This is the mechanism used by 80-column text and Double Hi-Res modes to access auxiliary display memory. When 80STORE is off, RAMRD/RAMWRT control bank selection for these regions normally.

---

## Language Card

The Language Card provides 16KB of RAM that overlays the ROM at `$D000`-`$FFFF`. The `$D000`-`$DFFF` region has two switchable 4KB banks.

### Switch Registers (`$C080`-`$C08F`)

The Language Card is controlled by soft switches at `$C080`-`$C08F`. The switch behavior depends on read vs. write access and uses a "double read" mechanism for write-enable.

| Bits | Meaning |
|------|---------|
| Bit 3 | Bank select: 0 = bank 2, 1 = bank 1 |
| Bits 0-1 | Mode (see table below) |

| Bits 0-1 | Read Source | Write Enable |
|----------|------------|--------------|
| 00 | RAM | Disabled |
| 01 | ROM | Enabled (after 2 consecutive reads) |
| 10 | ROM | Disabled |
| 11 | RAM | Enabled (after 2 consecutive reads) |

### Common Switch Combinations

| Address | Read | Bank | Read Source | Write |
|---------|------|------|------------|-------|
| `$C080` | R | 2 | RAM | Off |
| `$C081` | RR | 2 | ROM | On (double read) |
| `$C082` | R | 2 | ROM | Off |
| `$C083` | RR | 2 | RAM | On (double read) |
| `$C088` | R | 1 | RAM | Off |
| `$C089` | RR | 1 | ROM | On (double read) |
| `$C08A` | R | 1 | ROM | Off |
| `$C08B` | RR | 1 | RAM | On (double read) |

### Double-Read Write-Enable

Writes to Language Card switches reset the "prewrite" counter without counting toward the double-read requirement. This means:

- `LDA $C083` + `LDA $C083` enables writes (two reads)
- `LDA $C083` + `STA $C083` + `LDA $C083` does NOT enable writes (the STA resets the counter)
- `INC $C083` DOES enable writes (INC performs two reads before its write)

### Auxiliary Language Card

When ALTZP is on, the Language Card reads/writes go to the auxiliary banks (`auxLcBank1_`, `auxLcBank2_`, `auxLcHighRAM_`) instead of the main banks.

---

## Soft Switch Map

### Write-Only Switches (`$C000`-`$C00F`)

These switches are triggered by writes. Reads to this range return the keyboard latch value.

| Address | Switch | Description |
|---------|--------|-------------|
| `$C000` | 80STORE off | Disable 80STORE |
| `$C001` | 80STORE on | Enable 80STORE |
| `$C002` | RAMRD off | Main RAM read |
| `$C003` | RAMRD on | Aux RAM read |
| `$C004` | RAMWRT off | Main RAM write |
| `$C005` | RAMWRT on | Aux RAM write |
| `$C006` | INTCXROM off | Slot card ROMs |
| `$C007` | INTCXROM on | Internal ROM |
| `$C008` | ALTZP off | Main zero page |
| `$C009` | ALTZP on | Aux zero page |
| `$C00A` | SLOTC3ROM off | Internal slot 3 ROM |
| `$C00B` | SLOTC3ROM on | Slot 3 card ROM |
| `$C00C` | 80COL off | 40-column mode |
| `$C00D` | 80COL on | 80-column mode |
| `$C00E` | ALTCHAR off | Primary character set |
| `$C00F` | ALTCHAR on | Alternate character set (MouseText) |

### Status Reads (`$C010`-`$C01F`)

Status switches return the switch state in bit 7 and floating bus data in bits 0-6.

| Address | Read | Description |
|---------|------|-------------|
| `$C010` | KBDSTRB | Clear keyboard strobe; bit 7 = any key down |
| `$C011` | RDLCBNK2 | Language Card bank 2 selected |
| `$C012` | RDLCRAM | Language Card RAM read enabled |
| `$C013` | RDRAMRD | Aux RAM read enabled |
| `$C014` | RDRAMWRT | Aux RAM write enabled |
| `$C015` | RDCXROM | Internal CX ROM active |
| `$C016` | RDALTZP | Aux zero page active |
| `$C017` | RDC3ROM | Slot 3 ROM active |
| `$C018` | RD80STORE | 80STORE active |
| `$C019` | RDVBLBAR | Bit 7: 0 during VBL, 1 during active display |
| `$C01A` | RDTEXT | Text mode active |
| `$C01B` | RDMIXED | Mixed mode active |
| `$C01C` | RDPAGE2 | Page 2 active |
| `$C01D` | RDHIRES | HiRes mode active |
| `$C01E` | RDALTCHAR | Alternate character set active |
| `$C01F` | RD80COL | 80-column mode active |

### Display and Annunciator Switches (`$C050`-`$C05F`)

These switches are activated by both reads and writes.

| Address | Switch | Description |
|---------|--------|-------------|
| `$C050` | TXTCLR | Graphics mode |
| `$C051` | TXTSET | Text mode |
| `$C052` | MIXCLR | Full-screen mode |
| `$C053` | MIXSET | Mixed mode (4 lines text) |
| `$C054` | LOWSCR | Page 1 |
| `$C055` | HISCR | Page 2 |
| `$C056` | LORES | Low-resolution mode |
| `$C057` | HIRES | High-resolution mode |
| `$C058`/`$C059` | AN0 off/on | Annunciator 0 |
| `$C05A`/`$C05B` | AN1 off/on | Annunciator 1 |
| `$C05C`/`$C05D` | AN2 off/on | Annunciator 2 |
| `$C05E`/`$C05F` | AN3 off/on | Annunciator 3 (controls DHIRES) |

### I/O Devices

| Address | Device | Description |
|---------|--------|-------------|
| `$C000` | Keyboard | Read: key latch (bit 7 = key available) |
| `$C010` | Keyboard Strobe | Read: clear strobe, return any-key-down in bit 7 |
| `$C020` | Cassette Out | Toggle cassette output |
| `$C030` | Speaker | Toggle speaker state |
| `$C060` | Cassette In | Cassette input (always low, no cassette) |
| `$C061` | Button 0 | Open Apple key (bit 7 = pressed) |
| `$C062` | Button 1 | Closed Apple key (bit 7 = pressed) |
| `$C063` | Button 2 | Shift key / button 2 |
| `$C064`-`$C067` | Paddles 0-3 | Timer countdown (bit 7 = counting) |
| `$C070` | PTRIG | Reset all paddle timers |
| `$C07F` | IOUDIS/DHIRES | Bit 7 = DHIRES status (AN3 inverted) |

---

## Slot ROM Space

### Per-Slot I/O (`$C080`-`$C0FF`)

Each expansion slot has 16 bytes of I/O space:

| Address Range | Slot |
|---------------|------|
| `$C080`-`$C08F` | Language Card (special) |
| `$C090`-`$C09F` | Slot 1 |
| `$C0A0`-`$C0AF` | Slot 2 |
| `$C0B0`-`$C0BF` | Slot 3 (80-column, built-in) |
| `$C0C0`-`$C0CF` | Slot 4 |
| `$C0D0`-`$C0DF` | Slot 5 |
| `$C0E0`-`$C0EF` | Slot 6 |
| `$C0F0`-`$C0FF` | Slot 7 |

Reads and writes to slot I/O are dispatched to the installed card's `readIO()` / `writeIO()` methods with a 4-bit offset (0-15).

### Per-Slot ROM (`$C100`-`$C7FF`)

Each slot has 256 bytes of ROM space at `$CN00`-`$CNFF`. Accessing a slot's ROM page activates that card's expansion ROM at `$C800`-`$CFFF`. The card's `readROM()` method is called with an 8-bit offset.

### Expansion ROM (`$C800`-`$CFFF`)

This 2KB region is shared among all expansion cards. Only one card's expansion ROM can be active at a time, determined by the last slot ROM page accessed. The `activeExpansionSlot_` variable tracks which card currently owns this region.

A read from `$CFFF` deactivates the current expansion ROM after returning its data.

### Slot 3 Special Handling

Slot 3 is normally the built-in 80-column firmware. When SLOTC3ROM is off (default), reads to `$C300`-`$C3FF` return internal system ROM and also activate the internal ROM for `$C800`-`$CFFF` (via `intc8rom`). When SLOTC3ROM is on, the region uses the slot 3 card ROM if one is installed.

---

## Floating Bus

When the CPU reads an unoccupied or unmapped I/O address, it gets the "floating bus" value -- whatever byte the video circuitry is currently fetching from memory. The MMU computes this from the current CPU cycle count:

1. Calculate the frame cycle position: `cycles % CYCLES_PER_FRAME`
2. Derive the scanline and horizontal position
3. During horizontal blanking (cycles 0-24 of each scanline), return `$00`
4. During vertical blanking, wrap the scanline into the visible range
5. Compute the address the video hardware would be reading based on the current video mode (text/lores or hires) and display page
6. Return the byte at that address from the appropriate memory bank

This behavior is important for some copy protection schemes and timing-sensitive software that reads floating bus values to detect the beam position.

---

## Keyboard and Input

### Keyboard Latch (`$C000`)

Reading `$C000` returns the keyboard latch. Bit 7 is set when a key has been pressed and not yet read. Bits 0-6 contain the ASCII code of the key (or Apple II-specific code). The latch retains its value until explicitly cleared.

### Keyboard Strobe (`$C010`)

Reading `$C010` clears the keyboard strobe (bit 7 of the latch) and returns the any-key-down status in bit 7 with the key code in bits 0-6. Writing to `$C010` also clears the strobe.

### Buttons (`$C061`-`$C063`)

Button state is returned in bit 7 (pressed = `$80`, released = `$00`). Bits 0-6 contain floating bus data. The buttons map to:

| Address | Button | Physical Key |
|---------|--------|-------------|
| `$C061` | Button 0 | Open Apple (left Alt/Option) |
| `$C062` | Button 1 | Closed Apple (right Alt/Option) |
| `$C063` | Button 2 | Shift key state |

---

## Paddle Timers

The Apple IIe uses a timer-based paddle reading mechanism:

1. Software writes to `$C070` (PTRIG) to start all four paddle timers
2. The MMU records the current CPU cycle count as `paddleTriggerCycle_`
3. Reading `$C064`-`$C067` returns bit 7 = 1 while the timer is running, 0 when expired
4. Timer duration is `paddleValue * 11 cycles` (approximately 11 cycles per unit)
5. Paddle values range from 0-255, with 128 as center

This means a centered joystick axis produces a timer of about 1,408 cycles (128 * 11).

---

## Memory Access Tracking

The MMU includes optional access tracking for the debugger heat map visualization:

- `enableTracking(bool)` -- Enable/disable tracking
- `readCounts_[65536]` -- Per-address read access counters (saturate at 255)
- `writeCounts_[65536]` -- Per-address write access counters
- `decayTracking(amount)` -- Reduce all counters by a fixed amount for real-time decay
- `clearTracking()` -- Reset all counters to zero

When tracking is enabled, every `read()` and `write()` call increments the corresponding counter for that address. The JavaScript heat map window periodically decays and reads these counters to produce a visual representation of memory access patterns.

---

## The IIgs's Memory

A IIgs has a 24-bit address space and a memory controller of its own, `IIgsMemory` in `core/iigs/`. Four things about it are worth knowing.

**The Mega II side of it is an `MMU`** — the same class a //e is built from, constructed with the IIgs profile. Banks `$E0`/`$E1` are its main and auxiliary RAM, `$C000-$CFFF` in the banks that see it are its soft switches, and `$D000-$FFFF` is its language card. That is not a convenience: a IIgs really does contain a //e, and the video reads that MMU exactly as `Video` does on any other machine.

**Shadowing is a copy, not a redirection.** A write to a display region of bank `$00` or `$01` lands in fast RAM *and* is copied to `$E0`/`$E1`, because the video only ever looks at the Mega II's side. Which regions those are is the `$C035` register, and its bits read backwards — a set bit turns a region's shadowing **off**. Bit 6 changes what an address *is* rather than where a write also goes: with I/O and language card shadowing inhibited, banks `$00`/`$01` are plain RAM from `$C000` up, which is how a program gets a contiguous 128KB.

**Bank `$00` still obeys the //e's memory switches, and they send it into bank `$01`.** A IIgs is a //e whose main RAM is bank `$00` and whose auxiliary RAM is bank `$01`, so RAMRD and RAMWRT move `$0200-$BFFF`, ALTZP the zero page, stack and language card, and 80STORE with PAGE2 the text and first hi-res pages. Bank `$01` is never redirected. The 80-column firmware depends on it.

**`$C068` is eight of the //e's soft switches in one byte**, and writing it drives those switches through their own addresses so everything watching them sees the change the usual way. It has no bit for the language card's *write* latch, so that is read off the machine and preserved: changing the memory map must not quietly write-protect the card.

Banks `$00` and `$01` are 64K of fast RAM each, language card included — their `$D000-$FFFF` is the bank's own memory in the shape of a //e's card. The Mega II's card belongs to `$E0`/`$E1` alone. Vectors are pulled from ROM whatever the map shows, because the FPI answers the 65816's VPB line; GS/OS copies its kernel over `$D000-$FFFF` with interrupts enabled.

How much fast RAM is fitted is a user choice, 256K to 8M, in the Machine menu. Banks above what is fitted must not answer, because the firmware sizes memory by writing to one and reading it back.

## See Also

- [[Architecture-Overview]] -- How the MMU fits into the emulator
- [[CPU-Emulation]] -- CPU that reads/writes through MMU callbacks
- [[Video-Rendering]] -- Video modes controlled by soft switches
- [[Expansion-Slots]] -- Card architecture using MMU slot system
