# Expansion Slots

The Apple IIe has seven expansion slots (1-7), each providing I/O space and ROM space for peripheral cards. The emulator supports a configurable set of expansion cards that can be installed in their compatible slots.

**Slots belong to the machine.** An Apple II Plus has eight slots numbered from 0, a different set of fixed cards and different defaults, and it remembers its own layout separately from the //e's. Everything below describes the //e unless it says otherwise; see [Apple II Plus Slots](#apple-ii-plus-slots) and [[Machines]].

**Two machines have no slot window at all**, and the menu item is hidden rather than greyed out on both:

- An **Apple //c** has no expansion sockets. It still decodes all seven slot addresses, because the firmware and everything written for a //e depend on them, but each one answers to a part soldered to the board: two serial ports in slots 1 and 2, the 80-column firmware in slot 3, the mouse in slot 4, and the disk port in slot 6. Every slot is therefore *fixed*, and there is nowhere to put a card.
- An **Apple IIgs** has real slots on the board, but the choice each one offers — the machine's own firmware, or a card you fitted — is not modelled here, and its core does not answer the host's request to change a slot. Its SmartPort lives in slot 5 as part of the machine rather than as a card you insert.

## Table of Contents

- [Slot Map](#slot-map)
- [Apple II Plus Slots](#apple-ii-plus-slots)
- [Configuring Slots](#configuring-slots)
- [Available Cards](#available-cards)
- [Memory Map](#memory-map)
- [Card Interface](#card-interface)

## Slot Map

The default slot configuration matches a typical Apple IIe setup:

| Slot | Default Card | Typical Use |
|------|-------------|-------------|
| 1 | Empty | Printer |
| 2 | Empty | Modem / Serial |
| 3 | 80-Column (Built-in) | Fixed -- cannot be changed |
| 4 | Mockingboard | Mouse / Sound |
| 5 | Thunderclock Plus | 3.5" drives / Clock |
| 6 | Disk II Controller | 5.25" drives |
| 7 | SmartPort | Hard disk / Clock |

## Apple II Plus Slots

A II Plus motherboard has eight slots, numbered from 0, and its defaults are not the //e's:

| Slot | Default Card | Notes |
|------|-------------|-------|
| 0 | 16K Language Card | Fixed -- cannot be changed |
| 1 | Empty | Printer |
| 2 | Empty | Modem / Serial |
| 3 | Empty | Free -- there is no built-in 80-column card |
| 4 | Empty | |
| 5 | Empty | |
| 6 | Disk II Controller | 5.25" drives |
| 7 | Empty | |

Slot 0 holds the language card that turns a 48K machine into the 64K one nearly all II Plus software expects. It is fixed: the bank switching at `$C080-$C08F` is the same hardware a //e carries on its motherboard, and is not something you could pull out.

Slot layouts are stored **per machine**, because the machines do not agree about what a slot is -- one shared layout would put a II Plus's slot 3 card into a //e's built-in 80-column slot. A machine you have never configured falls back to its own defaults; a machine you deliberately stripped stays stripped.

## Configuring Slots

Open the **Expansion Slots** window from **View > Expansion Slots** to change which cards are installed. The window builds its slot list from the machine in use, so a //e shows slots 1-7 with slot 3 locked and a II Plus shows 0-7 with slot 0 locked.

### How It Works

Cards are **dragged** from the card tray onto a slot, and dragged off again to remove them. Pending changes are highlighted, and a warning indicates that they require a reset. Click **Apply & Reset** to commit everything and restart the emulator.

### Slot Restrictions

- **Slot 3** is fixed and always contains the built-in 80-column firmware. It cannot be changed.
- Each card type can only be installed in one slot at a time. If a card is already in use in another slot, its option will be grayed out in the dropdown.
- Not all cards are available in all slots. Each slot has a specific list of compatible cards based on Apple II conventions.

### Compatible Cards per Slot

| Slot | Available Cards |
|------|----------------|
| 1 | Parallel Card, Super Serial Card, Z-80 SoftCard |
| 2 | Parallel Card, Super Serial Card, SmartPort, Z-80 SoftCard |
| 3 | 80-Column (fixed) |
| 4 | Mockingboard, Mouse Card, SmartPort, Z-80 SoftCard |
| 5 | Thunderclock Plus, SmartPort, Z-80 SoftCard |
| 6 | Disk II Controller |
| 7 | Thunderclock Plus, SmartPort, Z-80 SoftCard |

Any slot can also be left empty.

### No-Slot Clock

The window also has a **No-Slot Clock (DS1215)** toggle. This is not a slot card: the real device was a chip carrier that sat underneath a ROM, piggybacking on the `$C300` address space, so it provides a ProDOS-compatible clock without consuming a slot. Its setting persists under `a2e-nsc-enabled`.

### Persistence

Slot configuration is saved to `localStorage` under `a2e-slot-config` and restored on load. With no saved configuration the defaults in the Slot Map above are used.

## Available Cards

### Disk II Controller

The Disk II controller card provides access to two 5.25-inch floppy disk drives. It uses 16 soft switches in the I/O space for drive control including phase stepping, motor control, drive selection, and read/write operations.

- **Default slot:** 6
- **I/O space:** `$C0E0`-`$C0EF` (16 soft switches)
- **ROM space:** `$C600`-`$C6FF` (256-byte bootstrap ROM, P5A 341-0027)
- **Supported formats:** DSK, DO, PO, NIB, WOZ

Soft switch layout:

| Offset | Even (Off) | Odd (On) |
|--------|-----------|----------|
| $00-$01 | Phase 0 off | Phase 0 on |
| $02-$03 | Phase 1 off | Phase 1 on |
| $04-$05 | Phase 2 off | Phase 2 on |
| $06-$07 | Phase 3 off | Phase 3 on |
| $08-$09 | Motor off | Motor on |
| $0A-$0B | Drive 1 select | Drive 2 select |
| $0C-$0D | Q6L (read) | Q6H (write protect / write load) |
| $0E-$0F | Q7L (read mode) | Q7H (write mode) |

See [[Disk-Drives]] for more details on disk operation.

### Mockingboard

The Mockingboard sound card provides stereo audio through two AY-3-8910 Programmable Sound Generator (PSG) chips, each controlled by a MOS 6522 Versatile Interface Adapter (VIA). It was the most popular sound card for the Apple II.

- **Default slot:** 4
- **ROM space:** `$C400`-`$C4FF` (VIA registers -- unusual; most cards use I/O space)
- **VIA 1:** `$C400`-`$C47F` (bit 7 = 0) -- controls left channel PSG
- **VIA 2:** `$C480`-`$C4FF` (bit 7 = 1) -- controls right channel PSG

The Mockingboard is unusual among Apple II cards in that it uses the slot ROM address space for its VIA registers instead of the I/O space, because it needs more than the 16 bytes available in the I/O range.

Each PSG provides 3 tone channels and 1 noise channel, for a total of 6 tone channels and 2 noise channels in stereo.

See [[Audio-System]] for more details on Mockingboard audio.

### Thunderclock Plus

The Thunderclock Plus is a ProDOS-compatible real-time clock card. It provides automatic date and time stamping for ProDOS applications, eliminating manual date entry prompts and enabling proper file timestamps.

- **Compatible slots:** 5, 7
- **Default slot:** 7
- **I/O space:** Control register at `$C0n0`
- **ROM space:** `$Cn00`-`$CnFF` (256-byte ProDOS clock driver)
- **Expansion ROM:** `$C800`-`$CFFF` (utility routines)

The Thunderclock uses the host system's real date and time, so ProDOS file timestamps will reflect the actual current time.

**ProDOS detection:** ProDOS scans expansion slot ROM looking for specific signature bytes (`$08`, `$28`, `$58`, `$70` at offsets 0, 2, 4, 6). When found, ProDOS patches its clock driver to use the Thunderclock for all date/time operations.

**Hardware interface:** The card uses a serial interface based on the NEC uPD1990C clock chip. Time data is transmitted as 40 bits (10 BCD nibbles) encoding seconds, minutes, hours, day, day-of-week, and month.

### Apple Mouse Card

The Apple Mouse Interface Card provides mouse input for Apple II software. It emulates the MC6821 PIA-based command protocol used by the original card's firmware.

- **Compatible slots:** 4 (shares with Mockingboard)
- **I/O space:** `$C0n0`-`$C0n3` (MC6821 PIA registers)
- **ROM space:** `$Cn00`-`$CnFF` (firmware)
- **Expansion ROM:** Full 2 KB ROM with page selection via Port B bits 1-3

The mouse card firmware runs as native 6502 code on the CPU. The emulator provides the hardware-side emulation of the MC6821 PIA, receiving commands from the firmware and providing mouse position and button data.

**VBL interrupt support:** When the mouse mode has bit 3 set, an IRQ is generated at the start of each vertical blanking period, allowing software to poll the mouse at a consistent 60 Hz rate.

### SmartPort Hard Drive Controller

A block-device controller providing up to two hard drive volumes, the usual way to give ProDOS a large disk.

- **Compatible slots:** 2, 4, 5, 7 (default 7)
- **Devices:** 2 block devices
- **Supported images:** `.hdv`, `.po`, `.2mg`
- **ROM:** built at runtime rather than loaded from a dump

The card implements the SmartPort call interface (`STATUS`, `READBLOCK`, `WRITEBLOCK`, `FORMAT`, `CONTROL`, `INIT`) plus the older ProDOS block-device entry point, so it works with both calling conventions.

See [[SmartPort-Hard-Drives]] for using it.

### Super Serial Card

An emulation of the Apple Super Serial Card, built around the ACIA 6551.

- **Compatible slots:** 1, 2
- **I/O space:** ACIA registers (data, status, command, control)
- **Drives:** ImageWriter I and ImageWriter II virtual printers

Output can be routed to a virtual printer or a serial connection. See [[Printers]].

### Parallel Card

A Centronics-style parallel interface, matching the Apple Parallel Interface Card (341-0057, whose upper ROM half is the 341-0005 "Parallel Printer" firmware).

- **Compatible slots:** 1, 2
- **Drives:** Epson FX-80 and Apple DMP virtual printers

See [[Printers]].

### Microsoft Z-80 SoftCard

A full Z80 CPU emulation on a card, as the original SoftCard used to run CP/M on an Apple II.

- **Compatible slots:** 1, 2, 4, 5, 7
- **Implementation:** `src/core/cards/softcard/`, with the Z80 core under `softcard/z80/`

The Z80 and the 6502 share the machine's memory. Accessing the card's soft switch hands control to the Z80, which runs until control is handed back -- the two processors never execute simultaneously.

## Memory Map

Each expansion slot is assigned dedicated address ranges in the Apple IIe memory map:

### I/O Space ($C080-$C0FF)

Each slot gets 16 bytes of I/O space. Software reads and writes to these addresses to communicate with the card's hardware.

| Slot | I/O Range |
|------|-----------|
| 1 | `$C090`-`$C09F` |
| 2 | `$C0A0`-`$C0AF` |
| 3 | `$C0B0`-`$C0BF` |
| 4 | `$C0C0`-`$C0CF` |
| 5 | `$C0D0`-`$C0DF` |
| 6 | `$C0E0`-`$C0EF` |
| 7 | `$C0F0`-`$C0FF` |

### Slot ROM Space ($C100-$C7FF)

Each slot gets 256 bytes of ROM space. When the CPU reads from this range, the card in that slot provides the data. This is typically used for identification bytes, bootstrap code, or (in the Mockingboard's case) hardware register access.

| Slot | ROM Range |
|------|-----------|
| 1 | `$C100`-`$C1FF` |
| 2 | `$C200`-`$C2FF` |
| 3 | `$C300`-`$C3FF` |
| 4 | `$C400`-`$C4FF` |
| 5 | `$C500`-`$C5FF` |
| 6 | `$C600`-`$C6FF` |
| 7 | `$C700`-`$C7FF` |

### Expansion ROM Space ($C800-$CFFF)

A shared 2 KB region that can be mapped to any card's expansion ROM. When the CPU accesses a card's slot ROM, that card's expansion ROM (if it has one) becomes active in this shared range. Cards like the Thunderclock and Mouse Card use this for additional firmware.

### ROM Switching Soft Switches

Two soft switches control how the slot ROM area is accessed:

- **INTCXROM** (`$C006`/`$C007`) -- When set, the internal ROM is used for the entire `$C100`-`$CFFF` range instead of slot ROMs.
- **SLOTC3ROM** (`$C00A`/`$C00B`) -- When set, slot 3 uses the card's ROM instead of the built-in 80-column firmware.

## Card Interface

All expansion cards implement the `ExpansionCard` interface, which provides the following methods:

| Method | Description |
|--------|-------------|
| `readIO(offset)` | Read from the card's I/O space (offset 0-15) |
| `writeIO(offset, value)` | Write to the card's I/O space |
| `readROM(offset)` | Read from the card's ROM space (offset 0-255) |
| `writeROM(offset, value)` | Write to the card's ROM space (unusual, used by Mockingboard) |
| `readExpansionROM(offset)` | Read from expansion ROM (offset 0-2047) |
| `reset()` | Reset the card to power-on state |
| `update(cycles)` | Update card state each CPU cycle |
| `serialize() / deserialize()` | Save and restore card state |

Cards can also generate IRQ interrupts via a callback mechanism, used by the Mockingboard's VIA timers and the Mouse Card's VBL interrupt.

See also: [[Architecture-Overview]], [[Audio-System]], [[Disk-Drives]], [[SmartPort-Hard-Drives]], [[Printers]]
