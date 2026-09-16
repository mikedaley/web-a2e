# Machines

The emulator models one machine at a time, and the badge in the header names it. Click the badge to choose a different one.

There are four: the **Apple //e**, the **Apple II Plus**, the **Apple //c** and the **Apple IIgs**.

---

## Table of Contents

- [Choosing a Machine](#choosing-a-machine)
- [Apple //e](#apple-e)
- [Apple II Plus](#apple-ii-plus)
- [Apple //c](#apple-c)
- [Apple IIgs](#apple-iigs)
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
| ROMs | 16KB firmware at `$C000-$FFFF`, plus a 4KB character generator |

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

The 1984 portable: a //e folded into a slab, with the drive in the case and nothing to plug a card into. It boots, reads disks, prints, takes a mouse and runs software.

| | |
|---|---|
| CPU | 65C02 at 1.023 MHz |
| RAM | 128KB — 64KB main plus a 64KB auxiliary bank, soldered down |
| Text | 40 and 80 columns |
| Graphics | Lo-Res, Double Lo-Res, Hi-Res, Double Hi-Res |
| Character sets | One |
| Slots | None. The slot addresses are decoded, but every one is soldered |
| ROMs | 16KB firmware at `$C000-$FFFF`, plus a 4KB character generator |

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
| 6 | The disk port: an IWM driving the internal drive and the external connector |
| 7 | Nothing |

The slot window shows them all, and none of them can be changed. Slots 5 and 7 are listed as having no socket rather than being offered a card, which is the difference between a machine whose slots are empty and one that has no slots.

Because there is no socket, there is nowhere for a card's ROM to live either: the firmware for the serial ports, the mouse and the drive is part of the 16KB system ROM. `$C100-$CFFF` therefore reads the internal ROM on a //c whatever INTCXROM and SLOTC3ROM say — those switches choose between the internal ROM and a slot that does not exist. A //e with an empty slot reads the floating bus at the same addresses.

### The disk is an IWM, not a card

Slot 6 decodes the sixteen addresses a Disk II card does, and the chip Apple soldered there answers them: the Integrated Woz Machine, the same controller in one package. The drives, the stepper, the motor and the sequencer are the same code the card uses; what the IWM adds is a register file — status, handshake and a mode register — and what it lacks is a `$C600` boot ROM, because a //c's disk firmware is part of its system ROM. A //c boots DOS 3.3 and ProDOS from the drive in its case exactly as a //e does from a card. See [[Disk-System-Internals]].

### The serial ports are the SSC's chip without the card

Slots 1 and 2 each hold a 6551 ACIA — the same chip a Super Serial Card carries, at the same four addresses within the slot (`$C098-$C09B` and `$C0A8-$C0AB`). So `PR#1` sends what the machine prints out of the printer port, and `IN#2` takes what arrives at the modem port, both through the //c's own firmware. An ImageWriter attached to the printer port prints exactly as one on an SSC does; the Printer window offers it without a card being installed, because on this machine there is nothing to install.

What a port does not have is the card's other half: no DIP switches to set its speed (the firmware keeps that), and no ROM, since a //c's serial firmware is part of the system ROM.

### The mouse is the IOU, not a card

A //e's mouse is a card in a slot: a PIA, a ROM, and a command protocol the firmware talks over the PIA's ports. A //c's mouse plugs into the back panel and its two quadrature lines go straight into the IOU, so what software gets is a handful of soft switches — `$C058-$C05F` to allow and shape the interrupts, `$C015` and `$C017` to say which axis moved, `$C066` and `$C067` for which way, `$C063` for the button, `$C048` to acknowledge.

There is no counter in the hardware. Every unit of travel is an interrupt, and the firmware in the system ROM reads the direction and adds one to a position it keeps in slot 4's screen holes; the button is sampled in the same handler's vertical-blanking path. Slot 4 names "mouse" in the slot window for that firmware's sake, but there is nothing in a socket and nothing to remove.

Mouse-driven software — MousePaint, AppleWorks' mouse support — therefore works on a //c with no card installed, and the mouse is captured in the browser exactly as it is on a //e (see [[Input-Devices]]).

## Apple IIgs

The 1986 machine that took the Apple II 16-bit: a 65C816, megabytes of RAM, 4096 colours and a synthesiser. It boots **GS/OS System 6.0.4 to the Finder**, with a working mouse and sound.

It is the one machine here that is not the same computer as the others. A //e, a II Plus and a //c differ by numbers, which is what a machine profile is for; a IIgs differs by *mechanism* — a 24-bit bus, a shadowing memory map, a second display system, an Ensoniq — and those get their own classes in their own directory rather than flags in everyone else's. Its profile says `MachineFamily::AppleIIgs`, and that is what selects the parts.

What it *does* share is real: a IIgs contains a Mega II, and a Mega II is a //e. Its video timing is the //e's to the cycle, which is why its profile carries those numbers, why the //e's `Video` class draws its text and hi-res, and why it runs //e software at all.

### Two clocks

The 65816 runs at 2.8 MHz until it reaches the Mega II, and that access is stretched to a 1.023 MHz cycle. So the slow clock ticks *inside* the memory, as each slow-side access happens, and the rest of the instruction is added afterwards at whatever the speed register says. Keeping it there is what lets it advance during an instruction: a disk read loop is a few cycles with one access in it, and a drive whose clock only moved between instructions would see that loop in lumps.

`$C036`'s bottom four bits are a veto on the fast clock rather than a speed setting — they are slot motor detect, and a drive turning in an enabled slot drops the whole machine to 1.023 MHz until it stops. That is what makes a Disk II readable at all.

### How much memory

A IIgs's fast RAM is a choice, from **256K to 8M**, in the Machine menu and remembered. Changing it rebuilds the machine, as switching machines does. Banks above what is fitted must not answer, because the firmware sizes memory by writing to one and reading it back.

### Super Hi-Res, and a border

`$C029` switches between the machine's two video systems. Super Hi-Res is 320 or 640 pixels wide with a palette per scanline, and the frame the emulator draws is the raster a monitor shows — border included, minus the part a bezel would hide. At 16 pixels a cycle that is **736x448** with the 640x400 picture at (48, 24); the //e's 560x384 goes in the same width, stretched to 640, and centred in the 200 lines. The IIgs's screen is shown at a monitor's 4:3 rather than at the //e's own ratio.

Bit 5 of `$C029` shows double hi-res in black and white, which is what the System 6 Finder and the 80-column desktop programs ask for.

### What else is in it

- **An Ensoniq** with thirty-two oscillators, clocked from the machine and interrupting, summed to one output pin the way a stock machine hears it
- **An ADB controller** for the keyboard and mouse, with `$C025` reporting the modifier keys so shift-click and ⌘-menu shortcuts work
- **A battery-backed clock** and the 256 bytes of settings beside it, kept by the host between sessions exactly as the firmware wrote them, checksum included — alter one byte and the firmware writes its defaults over the lot, which is what a machine with a dead battery does
- **A Z8530 SCC** behind two serial ports, with a loopback cable available as a tick box for the Apple IIgs Diagnostic's external test
- **A SmartPort in slot 5**, part of the machine rather than a card, serving hard drive images through GS/OS's extended calls
- **A speaker** as well, because `$C030` is a Mega II address; the volume nibble in `$C03C` is the speaker's amplifier and is applied as a taper rather than a ratio

See [[Apple-IIgs]] for the full account — the memory map, the interrupt sources, the video raster's numbers, and what is still missing.

## What Survives a Switch

Switching machines **rebuilds the emulator**. There is no way to convert a running machine into a different one — the RAM, the cards and the save state are all shaped to the machine that made them — so the old machine is destroyed and the new one constructed. The menu warns you before it does it.

| | Survives a switch? |
|---|---|
| Inserted disks and hard drives | No |
| Anything in memory | No |
| Display settings | No — **remembered per machine** |
| Saved monitor profiles | Yes — they are named snapshots any machine may pick |
| Volume and mute | Yes |
| Character set | Yes |
| CPU speed | Yes |
| Game port device (Apple joystick or Joyport) | Yes |
| Expansion slot layout | No — **remembered per machine** |
| Save states, including the autosave | No — **kept per machine** |
| ⌘ as Open Apple | No — **remembered per machine** |

Slot layouts are remembered separately for each machine, because the machines do not agree about what a slot is: one shared layout would put a II Plus's slot 3 card into a //e's built-in 80-column slot, and would follow a //e's SmartPort onto a machine whose defaults are a bare Disk II. A machine you have never configured falls back to its own defaults; a machine you deliberately stripped stays stripped.

Display settings are per machine for the same kind of reason — a //e's soft composite look has no business on a IIgs's RGB desktop — and each machine's default differs in one value, the screen border: 35% on the 8-bit machines, whose picture fills the frame, and 0 on a IIgs, which draws its own.

Save states are per machine because a state only restores into the machine that wrote it, so one shared autosave would come back to nothing for every machine but the last. See [[Save-States]].

## ROMs

The //e's and the //c's ROMs are part of the build. **The Apple II Plus and Apple IIgs ROMs are not distributed with the emulator** and have to be supplied before building.

A II Plus motherboard carries six 2KB ROMs in sockets D0 to F8 covering `$D000-$FFFF`: five of Applesoft and the Autostart monitor at `$F800`. Supply either those six images or one pre-combined 12KB image, plus the 2KB character generator:

- `341-0011.bin`, `341-0012.bin`, `341-0013.bin`, `341-0014.bin`, `341-0015.bin` (Applesoft, `$D000-$F7FF`) and `341-0020.bin` (Autostart monitor, `$F800-$FFFF`)
- **or** `apple2plus.rom` (12KB, `$D000-$FFFF`)
- `341-0036.bin` (2KB character generator)

The //c carries one 16KB ROM covering `$C000-$FFFF` and a 4KB character generator:

- `342-0272-A.bin` (16KB, `$C000-$FFFF`) **or** `apple2c.rom`
- `342-0265-A.bin` (4KB character generator; some dumps label the same part `341-0265-A.bin`, which is also accepted)

`342-0272-A` is ROM 255, the original //c, which is the machine the profile describes. The later 32KB ROMs — `342-0033-A` (ROM 0), `341-0445-A` and `341-0445-B` (ROMs 3 and 4) — are bank-switched and carry different peripherals in slots 4 and 5, so they are a different machine and are not taken.

The IIgs takes a ROM 01 image, either as its two socket ROMs or pre-combined:

- `341-0728.bin` with `341-0749.bin` (or `341-0748.bin`), 64KB each
- **or** `342-0077-B.bin` (128KB) **or** `apple2gs.rom`

A ROM image's banks can be either way round, and the loader asks rather than assumes: it looks for the emulation reset vector, which every IIgs ROM has at `$FF:FFFC`. Get it wrong and the machine resets to `$00:0000`.

Put them in `roms/` and rebuild. Without them the machine is still fully described and still listed in the menu, but is marked **unavailable** — a machine that could never reach a prompt is not offered rather than failing silently.

## How It Works

A machine is **data, not polymorphism**. What differs between a //e and a II Plus is overwhelmingly numbers — a clock rate, a scanline count, how much RAM answers, which CPU is fitted, whether the video generator inhibits colour burst in text mode — and those live in a `MachineProfile` struct that the subsystems read.

They are deliberately not virtual methods. `MMU::read`, the video emitters and the CPU dispatch loop are the hottest code in the emulator, and an indirect call on a per-cycle or per-dot path would cost real speed to serve a machine count of four. The rule is: **a number or a flag goes in the profile; a different mechanism goes in a different class that the profile names.**

A profile also says which **family** it belongs to, and that is what selects the parts. `MachineFamily::AppleII` is the three 8-bit machines, built from `MMU`, `Video`, `Audio` and `CPU6502`. `MachineFamily::AppleIIgs` is a different computer, built from its own classes in `core/iigs/`. The family is chosen once, at construction, and is also what the compile-time validation asks before applying a rule that only holds for one design — a IIgs is not measured against "a visible column clocks out 14 dots" when its picture is 640 dots wide.

Every profile is validated at compile time — that a scanline is its blanking plus one cycle per visible column, that a machine with no auxiliary bank does not claim auxiliary RAM, that double hi-res does not exist without 80 columns, that nothing is fitted to a slot the machine does not have, that a machine with no sockets ships nothing the user could then remove — so a broken profile does not compile.

Save states carry the machine id in their header, and a state saved on one machine is refused by another rather than being read as garbage — the host reads that header itself and offers to switch instead. The Apple II family's layout is one version; a IIgs's is its own, because the two share nothing after the header and have no reason to move together.

Menus follow the machine too. `machine-availability.js` says which items the running machine can use, from its profile and the cards fitted, and the rest are **hidden rather than disabled** — a greyed "Expansion Slots" on a //c only invites the question of how to enable it, and the answer is a different computer.

Adding a machine needs a profile entry, a subsystem class for anything that is a different mechanism rather than a different number, and its ROMs. Nothing in the browser layer needs to know: the //c arrived in the menu, the slot window and the window title without any of them being told about it, because all three read the profile.

See also: [[Expansion-Slots]], [[Input-Devices]], [[Architecture-Overview]], [[Video-Rendering]]
