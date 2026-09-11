# Machines

The emulator models one machine at a time, and the badge in the header names it. Click the badge to choose a different one.

There are three: the **Apple //e**, the **Apple II Plus** and the **Apple //c**.

---

## Table of Contents

- [Choosing a Machine](#choosing-a-machine)
- [Apple //e](#apple-e)
- [Apple II Plus](#apple-ii-plus)
- [Apple //c](#apple-c)
- [What Survives a Switch](#what-survives-a-switch)
- [ROMs](#roms)
- [How It Works](#how-it-works)

---

## Choosing a Machine

The badge to the right of the Apple logo is a live control, not decoration. Click it and a menu lists every machine the build knows about, each drawn with its own picture, its CPU, memory and column count, a tick against the one in use, and a note against any whose ROMs are missing.

The badge wears the machine's own logotype rather than the name you would write in a sentence. A II Plus is badged `][`, because rendering "II+" in that heavy oblique face produces "//+", a designation Apple never used.

The window title follows the machine too, so a browser's window switcher tells you which machine a tab is running.

Your choice is remembered and restored at startup, before the renderer and windows are built — so they are made for the right machine rather than rebuilt for it a moment later.

## Apple //e

The 1983 Enhanced //e, and the default.

| | |
|---|---|
| CPU | 65C02 at 1.023 MHz |
| RAM | 128KB — 64KB main plus a 64KB auxiliary bank |
| Text | 40 and 80 columns |
| Graphics | Lo-Res, Double Lo-Res, Hi-Res, Double Hi-Res |
| Character sets | US and UK |
| Slots | 1–7, with a built-in 80-column card fixed in slot 3 |
| ROMs | Included in the build |

## Apple II Plus

The 1979 machine most of the software you remember was written on. Its differences from the //e are real rather than cosmetic, and each one shows.

| | |
|---|---|
| CPU | NMOS 6502 at 1.023 MHz — the 65C02-only instructions are not there |
| RAM | 48KB, plus a 16KB language card fixed in slot 0 |
| Text | 40 columns only |
| Graphics | Lo-Res and Hi-Res only |
| Character sets | One |
| Slots | 0–7, with the language card fixed in slot 0 and slot 3 free |
| ROMs | **You supply them** — see [ROMs](#roms) |

### No auxiliary bank

`$C000-$C00F` on a //e are the memory and display management switches — 80STORE, RAMRD/RAMWRT, INTCXROM, ALTZP, SLOTC3ROM, 80COL, ALTCHARSET. On a II Plus that range manages no memory at all, and the emulator ignores the whole group on a machine with no auxiliary bank.

That single fact is what makes every 80-column and double-resolution path unreachable, rather than merely hidden: the video generator selects those modes from the 80COL switch, and on a II Plus that switch can never be set.

### The colour burst is never switched off

A //e kills the colour burst on text lines, which is why its text is crisp white. A II Plus sends a reference on every line, so **its text fringes green and violet in every mode** — exactly as the real machine's did on a colour monitor or television.

### One character set, and its glyphs are stored the other way round

A //e's 8KB character ROM puts bit 0 at the left of a glyph row; a II Plus's 2KB ROM puts bit 6 there, and puts the blank scanline of each cell first rather than last. Neither is more correct — it is how the part was wired to the video shift register — so the ROM image is rewritten into one layout when it is loaded rather than being decoded differently per dot.

The UK character set is a second bank inside the //e's larger ROM. A II Plus has nothing there, so the alternate set is simply not offered.

### Slot 0 and the language card

A II Plus motherboard has eight slots, numbered from 0. Slot 0 holds the 16K language card, which is how a 48K machine becomes the 64K one nearly all II Plus software expects. It is fitted as a **fixed** card: the bank switching at `$C080-$C08F` is the same hardware a //e carries on its motherboard, and is not something you could pull out.

Slot 3 is free, since there is no built-in 80-column card to occupy it.

## Apple //c

The 1984 portable: a //e folded into a slab, with the drive in the case and nothing to plug a card into. **Not yet startable** — see [ROMs](#roms), and the note below on what is still missing.

| | |
|---|---|
| CPU | 65C02 at 1.023 MHz |
| RAM | 128KB — 64KB main plus a 64KB auxiliary bank, soldered down |
| Text | 40 and 80 columns |
| Graphics | Lo-Res, Double Lo-Res, Hi-Res, Double Hi-Res |
| Character sets | One |
| Slots | None. The slot addresses are decoded, but every one is soldered |
| ROMs | **You supply them** — see [ROMs](#roms) |

The machine modelled is the original //c, ROM 255: one internal 5.25" drive and an external port, no UniDisk 3.5 and no memory expansion — both arrived on later ROMs and put different things in slots 4 and 5.

### The same custom chips as a //e

A //c carries the //e's IOU and MMU, so every number in its timing, memory and display is the //e's, and so is nearly every capability. Its text is crisp white for the same reason a //e's is, it runs the same 65C02, and it answers the same soft switches. What differs is the back of the machine.

### No slots, only slot addresses

A //c has no expansion sockets at all. The firmware and every program written for a //e still expect to find peripherals at slot addresses, so the machine decodes all seven — but each one answers to a part soldered to the board:

| Slot | What is there |
|---|---|
| 1 | Serial port 1 — the printer port, a 6551 with no handshake lines |
| 2 | Serial port 2 — the modem port, a 6551 with the full set |
| 3 | 80-column firmware, where a //e has its card |
| 4 | The mouse, built in |
| 5 | Nothing |
| 6 | The disk port: the internal drive and the external connector |
| 7 | Nothing |

The slot window shows them all, and none of them can be changed. Slots 5 and 7 are listed as having no socket rather than being offered a card, which is the difference between a machine whose slots are empty and one that has no slots.

### What is not there yet

The profile describes the machine, and the //e subsystems underneath it are the right ones, but three of the built-in peripherals have no implementation behind them: the **IWM** that drives the disk, and the two **6551** serial ports. The mouse is the //e's mouse card, which is close but is not how a //c's is wired.

So a //c with its ROM in place would reach its firmware and not a disk. The IWM is the next piece of work.

## What Survives a Switch

Switching machines **rebuilds the emulator**. There is no way to convert a running machine into a different one — the RAM, the cards and the save state are all shaped to the machine that made them — so the old machine is destroyed and the new one constructed. The menu warns you before it does it.

| | Survives a switch? |
|---|---|
| Inserted disks and hard drives | No |
| Anything in memory | No |
| Display settings and monitor profile | Yes |
| Volume and mute | Yes |
| Character set | Yes |
| CPU speed | Yes |
| Game port device (Apple joystick or Joyport) | Yes |
| Expansion slot layout | Yes, but **per machine** |

Slot layouts are remembered separately for each machine, because the machines do not agree about what a slot is: one shared layout would put a II Plus's slot 3 card into a //e's built-in 80-column slot, and would follow a //e's SmartPort onto a machine whose defaults are a bare Disk II. A machine you have never configured falls back to its own defaults; a machine you deliberately stripped stays stripped.

## ROMs

The //e's ROMs are part of the build. **The Apple II Plus ROMs are not distributed with the emulator** and have to be supplied before building.

A II Plus motherboard carries six 2KB ROMs in sockets D0 to F8 covering `$D000-$FFFF`: five of Applesoft and the Autostart monitor at `$F800`. Supply either those six images or one pre-combined 12KB image, plus the 2KB character generator:

- `341-0011.bin`, `341-0012.bin`, `341-0013.bin`, `341-0014.bin`, `341-0015.bin` (Applesoft, `$D000-$F7FF`) and `341-0020.bin` (Autostart monitor, `$F800-$FFFF`)
- **or** `apple2plus.rom` (12KB, `$D000-$FFFF`)
- `341-0036.bin` (2KB character generator)

The //c's are not distributed either. It carries one 16KB ROM covering `$C000-$FFFF` and a 4KB character generator:

- `342-0033-A.bin` (16KB, `$C000-$FFFF`) **or** `apple2c.rom`
- `342-0265-A.bin` (4KB character generator)

Put them in `roms/` and rebuild. Without them the machine is still fully described and still listed in the menu, but is marked **unavailable** — a machine that could never reach a prompt is not offered rather than failing silently.

## How It Works

A machine is **data, not polymorphism**. What differs between a //e and a II Plus is overwhelmingly numbers — a clock rate, a scanline count, how much RAM answers, which CPU is fitted, whether the video generator inhibits colour burst in text mode — and those live in a `MachineProfile` struct that the subsystems read.

They are deliberately not virtual methods. `MMU::read`, the video emitters and the CPU dispatch loop are the hottest code in the emulator, and an indirect call on a per-cycle or per-dot path would cost real speed to serve a machine count of three. The rule is: **a number or a flag goes in the profile; a different mechanism goes in a different class that the profile names.**

Every profile is validated at compile time — that a scanline is its blanking plus one cycle per visible column, that a machine with no auxiliary bank does not claim auxiliary RAM, that double hi-res does not exist without 80 columns, that nothing is fitted to a slot the machine does not have, that a machine with no sockets ships nothing the user could then remove — so a broken profile does not compile.

Save states carry the machine id in their header, and a state saved on one machine is refused by the other rather than being read as garbage.

Adding a machine needs a profile entry, a subsystem class for anything that is a different mechanism rather than a different number, and its ROMs. Nothing in the browser layer needs to know: the //c arrived in the menu, the slot window and the window title without any of them being told about it, because all three read the profile.

See also: [[Expansion-Slots]], [[Input-Devices]], [[Architecture-Overview]], [[Video-Rendering]]
