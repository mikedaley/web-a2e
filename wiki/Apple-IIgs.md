# Apple IIgs

**Status: it boots, reads both its drives, and gets as far as the GS/OS startup screen.** The real ROM runs, passes its power-on diagnostics, draws the Apple IIgs splash screen through the Mega II, and then reads track zero through its own IWM and comes up at the DOS 3.3 prompt — white on blue, in a blue border, with the drive panel and the drive sounds following the head the way they do on every other machine here. With no disk in it the machine stops at **Check startup device!**, which is what a real one says. You can type at it, and it has a speaker as well as an Ensoniq. Super Hi-Res is drawn, but nothing in the firmware turns it on, so it appears when a program does.

It takes about ten seconds of emulated time to get through the diagnostics, so the screen is black for a while before the splash appears. This page is the plan — what a IIgs is, why it cannot be another profile, where its code goes, and the order the parts arrive in.

---

## Table of Contents

- [Why This Is Not Another Profile](#why-this-is-not-another-profile)
- [What Is Shared and What Is Not](#what-is-shared-and-what-is-not)
- [Where the Code Goes](#where-the-code-goes)
- [The Order of Work](#the-order-of-work)
- [Things That Will Have to Give](#things-that-will-have-to-give)
- [ROMs](#roms)

## Why This Is Not Another Profile

A //e, a II Plus and a //c are one computer with different parts fitted. Everything that differs between them is a number or a flag — a clock rate, a scanline count, how much RAM answers, which CPU is fitted — so a `MachineProfile` describes all three and one set of subsystems reads it. That is the whole point of the machine profile layer, and it has now been proved three times.

A IIgs is a different computer:

| | //e, II+, //c | IIgs |
|---|---|---|
| CPU | 6502 or 65C02, 16-bit address bus | 65C816, 24-bit bus, 16-bit registers, two modes |
| Memory | 128KB, one map | 256KB-8MB of fast RAM, 128KB of slow, banks, and a shadowing map |
| Video | One generator | The Mega II's **and** Super Hi-Res, per-line palettes, 4096 colours |
| Sound | One bit, or a card | Ensoniq 5503: 32 oscillators and 64KB of its own RAM |
| Input | Keyboard and game port | ADB microcontroller |
| Settings | None | Battery RAM and a Control Panel |

None of that is a number. The rule the codebase has followed from the start says what to do about it: **a number or a flag goes in the profile; a different mechanism goes in a different class that the profile names.** The IIgs is where the second half of that sentence finally gets used.

`MachineProfile` therefore gained one field — `MachineFamily`, which is `AppleII` or `AppleIIgs` — and it is what selects the parts, once, at construction. It is also what the compile-time validation asks before applying a rule that only holds for one design: a IIgs is not checked against the //e-sized arrays it does not use, or against "a visible column clocks out 14 dots" when its picture is 640 dots wide.

## The Two Clocks

A IIgs runs at 2.8MHz until it reaches across to the Mega II, and then it runs at the Mega II's 1.023MHz for that access. So the machine is not "a //e at 2.8MHz": how fast a program goes depends on where it is reading.

The slow clock lives in `IIgsMemory` rather than in the machine, and ticks on each access that reaches the slow side — during an instruction, not between instructions. That matters for one thing in particular: a disk read loop is a handful of cycles with a single I/O access in it, and a drive whose clock only moved when an instruction ended would see that loop in lumps. Everything else — the video, the frame boundary — is counted in the same clock.

Those ticks are then *subtracted* from the instruction before the rest of it is converted, because a cycle spent waiting on the Mega II is not also a cycle spent running. Charging both is easy to do and hard to see: the machine simply runs at about half speed wherever it touches I/O, which is exactly where the timing matters.

The other half of the arrangement is `$C036`, and it is not a speed setting so much as a veto — see [The Drive](#the-drive).

## The Drive

Slot 6 holds an `IWM`, the same class a //c has, with `DiskController` under it: the drives, the stepper, the motor and the sequencer are shared with the card a //e takes. There is no card ROM, because the boot code that drives it is the machine's own firmware.

**A IIgs boots a disk**, and every step below had to be right before it would. Each is worth writing down, because each of the last three looks like something else — a broken drive, a broken image, a broken sequencer — and none of them is.

- The registers the firmware polls before it will look for a drive at all — `$C02D` and `$C031` — answer properly. They used to return the floating bus, and the machine never spun a drive.
- The mode register write reaches the chip. It arrives at `$C0EF` on Q7 alone, without Q6; requiring both hangs the machine in a loop writing the mode and reading it back, 786,905 times in one boot.
- Slot 6 carries the disk boot signature (`$Cn01=20`, `$Cn03=00`, `$Cn05=03`, `$Cn07=3C`) out of the machine's own firmware, so the slot is recognised as bootable, and `$C600` holds the boot code.
- **ENABLE is not the motor.** The firmware switches the drive off and writes the IWM's mode register on the next instruction — and `$C0EF` is both that register and Q7. A drive keeps turning for about a second after the CPU switches it off, so a controller that asks "is the disk spinning" instead of "is the drive enabled" does two wrong things with that one access: it sends the byte to the write data register rather than the mode register, so the firmware's read-back never matches and it spins for a second; and it lets the sequencer write Q7's output to the disk for all of that second, which erases track zero. The machine was wiping the disk it was about to boot, twice over, and then reading a drive that answered `$A0` for ever.
- **A cycle spent on the slow side is not also spent on the fast side.** Each access that reaches the Mega II charged the slow clock as it happened, and then the whole instruction was charged again at the end — so the boot ROM's read loop, which runs out of bank `$00`'s I/O space and is therefore slow accesses all the way through, came out at thirteen cycles where the disk expects seven. Every second byte went past unread.
- **`$C036`'s bottom four bits are why a IIgs can read a Disk II at all.** They are slot motor detect, one each for slots 4 to 7: with a slot's bit set, a drive turning in that slot drops the whole machine to 1.023MHz until it stops. That is not a courtesy to slow cards. A Disk II holds a finished byte for about two bit cells before the sequencer takes it apart again, so the boot ROM's thirteen-cycle poll arrives once per byte on the machine it was written for — and three times per byte at 2.8MHz, reading half of them twice and failing every checksum on the disk. The firmware sets bit 2 before it goes looking, and the hardware is expected to do the rest.

`tests/integration/test_iigs_boot.cpp` holds both halves of the claim: that the machine reaches the DOS 3.3 prompt, and that the image it booted from is byte-for-byte what was inserted.

## The Colours the Machine Draws In

Two registers decide what a IIgs's //e-mode screen looks like, and neither
belongs to the //e:

- **`$C022` TCOLOR** — the foreground and background the text is drawn in, a
  nibble each out of the VGC's sixteen colours.
- **the bottom nibble of `$C034`** — the border around the picture. The top
  nibble of that address is the clock's transaction control, which is why the
  firmware can write `$06` there and mean "medium blue" without starting one.

The firmware writes `$F6` and `$06` on the way up: white on medium blue, in a
medium blue border. That is the screen everyone remembers, and it is not
something a //e could produce. **A IIgs does not send its text down a composite
lead at all.** The VGC generates the picture digitally and substitutes two
colours of its own for lit and unlit text dots, which is why IIgs text is crisp
where a //e's fringes, and why the Control Panel can offer sixteen of each.

So a text line takes no decoder: `Video::setTextColours` is a pair of colours,
and `endScanline` uses them in place of the receiver for text lines — the text
rows of a mixed screen included, since graphics dots carry a colour of their own
and the VGC leaves them alone. A machine that never calls it decodes text
exactly as it always did, which is how this is a number in the //e's video
rather than a second video generator.

The one display setting that still wins is **Monochrome**: a monochrome monitor
has one phosphor and no opinion about what the machine sent it. Every other
setting in that window — the colour mode, the CRT shader, the character set —
reaches a IIgs the way it reaches the others, because a IIgs's //e-mode picture
is drawn by the //e's own `Video` from the //e's own memory.

## Sound

A IIgs has two sound sources and one amplifier, and it needs both. The Ensoniq
is the famous one; the other is **the speaker at `$C030`**, which is a Mega II
address and the same one-bit speaker every Apple II has. A machine given only
the synthesiser is silent through every beep, every click and every game written
before 1986 — which is most of what it runs. `IIgsMachine` owns an `Audio` for
it, toggled on the slow clock, and the Ensoniq's samples are added on top.

## What the Host Sees

`wasm_interface.cpp` is where "which machine is running" is answered, and three
accessors there mean the rest of the host does not have to ask: `diskController()`,
`videoGenerator()` and `speaker()` each return the part of whichever machine is
live. The drive panel, the activity lights, the seek and motor sounds, the track
heat map, the file explorer, the display settings, the volume slider and the mute
button are all the same code they were; they were only ever asking a `DiskController`,
a `Video` and an `Audio`, and a IIgs has all three.

## Memory

**How much fast RAM the machine has is a choice, and the menu makes it.** A ROM
01 shipped with 256K soldered to the board and almost nobody left it there — a
memory expansion card was the first thing most owners fitted. So the Machine
menu carries a row of sizes under the IIgs, from 256K to the eight megabytes the
24-bit bus reaches, and the choice is remembered like the machine itself.

Setting it rebuilds the machine, for the same reason switching machines does:
RAM cannot grow underneath a running program. It is applied before the machine
is built at startup rather than after, or the size would throw away the machine
it had just started.

`clampFastRamSize` rounds a requested size to whole 64K banks and holds it
between what a machine could have, because RAM arrives a bank at a time; a size
somebody typed, or one written by a later version, gives a machine that starts
rather than an error. The empty banks above what is fitted must *not* answer —
the firmware sizes memory by writing to a bank and reading it back, so a machine
whose unpopulated banks answered would report memory it has not got.

**GS/OS gets to its startup screen.** System 6.0.4 turns Super Hi-Res on and
draws *Welcome to the IIgs — System 6.0.4* in the box with the progress bar. It
does not finish booting; where it stops is written down below.

Two memory-map bugs stood in the way, and both were the same mistake a bank
apart: **a bank names which half of the language card it reaches, and this asked
ALTZP instead.**

The Mega II's 128K is banks `$E0` (main) and `$E1` (auxiliary), and banks `$00`
and `$01` carry the same language card at `$D000-$FFFF`, a half each. A //e has
only ALTZP to ask with, because a //e has no bank to name — so the //e's own
read path takes ALTZP, and using it for a IIgs makes each pair one 48K instead
of two. `IIgsMemory::languageCardAux` is the rule in one place: bank `$01` and
bank `$E1` are the auxiliary half whatever the switches say, and ALTZP still
moves bank `$00` across, because //e software on the fast side has to behave as
it would on a //e.

What each looked like is worth knowing, because neither looked like a memory map:

- **`$E0`/`$E1`** presented as a *memory* fault. System 6 stopped at *Error
  allocating memory for GS/OS. Error =$0201* — Memory Manager error 1 — with the
  same message at 256K, 1M, 2M and 4M, and with the firmware's own bank count
  correctly following the RAM (`$E1:1624` goes `$04` → `$10` at 1M). The Memory
  Manager was not miscounting memory. Its tables live in `$E1`'s language card,
  and bank `$E0` had been writing over them.
- **`$00`/`$01`** presented as a *video* fault: a screen of repeating green and
  lavender, which is the Mega II's hi-res page showing what was never written to
  it. GS/OS runs code out of `$01:D000` upward; it got bank `$00`'s card, which
  held data — four `CMP (dp,S),Y` in a row at `$01:D06F` — and walked up through
  bank `$00` executing whatever it found until it ran off `$00:FFFF` and fell to
  `$00:0000`, where every vector was zero and it sat in a BRK storm.

**Where it stops now**, so that whoever picks this up does not re-derive it. At
about 2.54 million instructions, `$00:AE15` executes an `RTS` to `$0000`. The
address it returns through was pushed at `$00:AE02` from direct page `$28` — the
machine is running on GS/OS's direct page at `$00:9900` — and that word is zero.
It is zero legitimately: `$01:B9A5` calls `$01:D06F`, which allocates the direct
page through the Tool Locator (490 instructions, returning `A=$9900`, carry
clear — a success), and the caller then clears all 256 bytes of it at `$01:B9AF`.
Nothing writes `$28` between that clear and the `RTS` twenty thousand
instructions later. So the question to answer next is what was supposed to fill
it, and it is **not** a question about how much RAM is fitted: the run is
identical at 1M, 4M and 8M.

## The SmartPort

**Slot 5 is the SmartPort, and it is part of the machine.** There is no card to
fit, no entry in the Expansion Slots window and no Control Panel setting to
change: insert a hard drive image and a IIgs boots ProDOS 8 off it. With nothing
inserted it has no ROM at all, and the machine's own slot 5 firmware shows
through unchanged.

Two things about how that works are worth knowing, because both were wrong at
first in ways that looked like something else.

**The machine's own SmartPort firmware is real, and it is not what serves these
images.** Slot 5 of a ROM 01 carries a genuine SmartPort signature — `$C501=$20`,
`$C503=$00`, `$C505=$03`, and `$C507=$00`, the `$00` being what distinguishes a
SmartPort from slot 6's Disk II — and behind it is a stub that `JSL`s into bank
`$FF`. What that firmware does is **poll the IWM**: 1,270 reads of `$C0EE` in one
boot, looking for a Sony 3.5" drive and for whatever is daisy-chained off the
port behind it. It cannot read a block image out of nowhere, and making it work
means emulating the 3.5" recording scheme, the drive's register file on the
IWM's SENSE line, and the SmartPort bus — each its own piece of work, and what
they buy is 800K floppies rather than hard disks. So slot 5 holds the same
block-device SmartPort the other machines use, answering the same ProDOS and
SmartPort calls.

**`$C02D` decides whether a slot shows the machine's firmware or a card's, and
a IIgs comes up with every slot internal.** That is correct — on real hardware
you would go to the Control Panel and set a slot to "Your Card" before a card in
it answered. A part the machine *has* is on the internal side of that switch, so
`IIgsMemory::setInternalCardSlot` names the one slot that answers either way.
`$C02D` is left reading exactly what the firmware wrote.

**A trap card must be told when the CPU is executing, not guess.** The card's
entry points are traps: a read of `$C510` during a fetch is a driver call to
service, and the same read by a ProDOS scan is just a byte. Telling those apart
means knowing what the processor has done to the program counter by the time the
read arrives — and a 6502 fetches with `read(pc_++)`, so it has already moved
past the opcode, where a 65816 reads and then advances. The card used to assume
the 6502's answer. On a IIgs that matched the boot call by luck and missed every
driver call after it, so the volume booted and then said **UNABLE TO LOAD
PRODOS**. `SmartPortCard::setExecutingAt` is now a predicate the machine
supplies, because only the machine knows which processor it has.

## Super Hi-Res

A IIgs screen is not a mode. Each of its 200 lines has a control byte of its own — in bank `$E1` at `$9D00` — saying whether that line is 320 pixels wide or 640, which of sixteen palettes it draws from, and whether it fills. So one screen can be 320-wide artwork above a 640-wide menu bar with different colours in each, and programs really did that.

| | 320 mode | 640 mode |
|---|---|---|
| Pixels per byte | 2 | 4 |
| Bits per pixel | 4 | 2 |
| Colours per pixel | any of the line's 16 | 4 of them |

640 mode's trick is worth knowing: a two-bit pixel can only count to four, so **which quarter of the palette it draws from depends on where it sits in the byte** — positions left to right use entries 8-11, 12-15, 0-3 and 4-7. Software draws with it by arranging neighbouring groups to dither into each other.

320 mode has **fill mode**, where colour zero is not a colour but "the same as the pixel to my left". It made horizontal runs cheap to draw. A line's leftmost pixel can never be transparent, because there is nothing to its left to copy.

A palette entry is `$0RGB` — four bits a channel, 4096 colours — and each nibble is expanded by repeating it, so `$F` becomes 255. Shifting instead would make white come out grey next to full red.

Programs draw all of this by writing to bank `$01` at full speed; shadowing copies it to `$E1`, and the video reads `$E1`. That is the arrangement the whole machine is built around, and there is a test that draws a IIgs screen entirely through it.

## What Is Shared and What Is Not

The reason a IIgs belongs in this emulator at all is that a large part of it is already here.

**Shared, and used as-is:**

- **The Mega II is a //e.** Same video timing, same soft switches, same 128KB in banks `$E0`/`$E1`, same text, lo-res, hi-res and double hi-res. A IIgs runs //e software because it contains one.
- Disk images (DSK, WOZ, 2MG), GCR encoding, the filesystems, the BASIC tokenizer and detokenizer, the assembler, the disassembler's 6502 half, the printers, the expansion cards, and every host-side window.

**Not shared, and deliberately not mixed in:**

- The 65816 core. It is not a CPU6502 with extra opcodes: it has 16-bit registers, a bank register on both the program and the data side, an emulation mode that must behave exactly like a 65C02, and addressing modes that do not exist on an 8-bit part. Sharing a dispatch loop between them would slow the //e down to serve a machine it is not.
- The memory controller, the Super Hi-Res half of the video, the Ensoniq, the ADB, and the battery RAM.

## Where the Code Goes

```
src/core/
├── machine/machine_profile.hpp   # gains MachineFamily; the IIgs's shared numbers
├── cpu/
│   ├── 6502/                     # unchanged
│   └── 65816/                    # the IIgs's CPU, on its own (done)
└── iigs/
    ├── iigs_spec.hpp             # the numbers no other machine has (done)
    ├── iigs_memory.*             # FPI/Mega II map, banks, shadowing (done)
    ├── iigs_video.*              # Super Hi-Res, over the Mega II's picture (done)
    ├── iigs_sound.*              # Ensoniq 5503 DOC (done)
    ├── iigs_adb.*                # keyboard and mouse microcontroller (keyboard done)
    ├── iigs_clock.*              # battery-backed clock and 256 bytes of settings (done)
    ├── iigs_battery_ram.*        # settings and the clock chip
    └── iigs_machine.*            # the coordinator, as Emulator is for the rest (done)
```

The rule for this directory: **nothing in `core/iigs/` is included by a machine that is not a IIgs, and nothing outside it grows an `if (machine is a IIgs)`.** Where the two designs genuinely share a part — the Mega II's video generator is the obvious one — the shared code stays where it is and the IIgs's class uses it, the way `IWM` and `Disk2Card` share `DiskController`.

## The Order of Work

Each step is meant to be a commit that stands on its own, with tests that pass before the next one starts.

1. **Describe the machine.** Profile, family, spec header, ROMs, and an honest "not runnable". *(Done.)*
2. **The 65816.** *(Done.)* A standalone core in `src/core/cpu/65816/` with no emulator wiring at all: registers, both modes, every addressing mode, all 256 opcodes, cycle counts. Tested on its own against a flat 16MB of memory, and checked against 5.1 million recorded states from a real chip — see [[CPU-Emulation]] and `tests/conformance/test_65816_vectors.cpp`.
3. **Memory.** *(Done.)* `IIgsMemory` in `src/core/iigs/`: banks, fast and slow RAM, ROM, the language card, shadowing, and the machine's own registers. The Mega II side is an `MMU` — the same class a //e is built from — rather than a second copy of that map, so the video will later read it exactly as a //e's video does.
4. **A machine that boots.** *(Done.)* `IIgsMachine` wires the CPU, the memory and the Mega II's video together and runs the firmware to its startup screen. Getting there needed two devices earlier than this plan expected, because the diagnostics run before anything is drawn: the **ADB** controller (`iigs_adb.*`), which the firmware syncs and interrogates before it will continue, and the **Ensoniq's RAM window** (`iigs_sound.*`), which is the chip's 64KB and the four registers the CPU reaches it through. Neither is finished — there is no keyboard, no mouse and no synthesiser — but both are real devices in their own files rather than stubs in somebody else's.
5. **Super Hi-Res.** *(Done.)* `iigs_video.*`: both widths, per-line control bytes, sixteen palettes of sixteen colours out of 4096, fill mode, and the `$C029` switch that decides which of the machine's two video systems is on screen. Nothing in the firmware turns it on — a IIgs boots in text — so it shows up when a program asks for it.
6. **Sound.** *(Done.)* The 32 oscillators: each walks a pointer through the sound RAM at its own frequency, scales what it reads by its volume, and adds it to one of sixteen channels — even ones to one speaker, odd to the other. A zero byte is the end of a sound, and the chip halts an oscillator that reads one, which is how a sample knows where it stops without anybody counting. The machine's audio call now asks the chip rather than returning silence.
7. **Input and settings.** *(Mostly done.)* The keyboard works: a browser key event is translated the //e's way, handed to the ADB controller, and put by it into the register the Mega II reads — so //e software finds the keyboard where it expects it without knowing a microcontroller is involved. The mouse reports through the same controller, seven bits of signed movement a byte with the button in the top bit. The clock and its 256 bytes of battery RAM work, and the firmware writes its settings there at startup. Still to come: the Control Panel hotkey, which the controller itself intercepts on real hardware, and the slots.
8. **The host.** *(Mostly done.)* The machine can be chosen from the menu and drives the display: the wasm layer holds either an `Emulator` or an `IIgsMachine` and routes the calls that run and show a machine to whichever it is. The shared framebuffer slot is sized for the largest picture (640x400) so the IIgs does not fall back to `postMessage`. Keys, the mouse, disk insertion and the audio the emulation is paced by all reach the machine. Everything else — printers, cards, the debugger, the agent tools — still asks for an `Emulator` and quietly does nothing while a IIgs is running.

## Things That Will Have to Give

Known places where the rest of the emulator assumes an 8-bit Apple II. None is a blocker; all are listed so they are not a surprise.

- ~~The shared framebuffer slot is sized for the //e's 560x384~~ — now 640x400, the largest any machine here draws. The other three write a smaller picture into a larger slot, which costs 164KB of address space and saves the one machine that would not fit from falling back to `postMessage`.
- **Save states** are laid out to the saving machine's shape and carry a machine id. A IIgs state is a different shape again; `STATE_VERSION` will have to move.
- **The debugger** — disassembler, breakpoints, the trace — speaks 6502 and 16-bit addresses.
- **The agent tools** are `g_emulator`-shaped, and `Emulator` is the Apple II family's coordinator. The wasm interface no longer is everywhere: see [What the Host Sees](#what-the-host-sees) for the three accessors that answer "which machine is running" once, so the drive panel, the display settings and the volume slider need not.

## ROMs

**The character generator is not in the ROM.** A //e keeps its font in a part of its own and a //c keeps its inside the system ROM; a IIgs keeps its inside the video chip, where the CPU cannot read it — searching a ROM 01 image for so much as one glyph finds nothing. So the machine is given the //e's set, which is the same font. Without one every glyph is blank, and since an inverse blank is solid, the screen shows white bars where the text should be.

**The banks can be either way round.** The obvious reading of a 128KB ROM 01 image is that it ends at `$FF:FFFF`, so its first half is bank `$FE` — and the dump this was written against is stored the other way. It is not a subtle difference: the emulation reset vector lives at `$FF:FFFC`, and reading it out of the wrong half gives zero and a machine that resets to `$00:0000` and sits there. So `IIgsMemory::loadROM` asks the image which way round it is, by looking for a usable reset vector at the top of each candidate bank.

Not distributed. A IIgs ROM 01 is a single 128KB image; a ROM 3 is 256KB across two chips, concatenated in bank order:

- `342-0077-B.bin` (128KB, ROM 01, banks `$FE-$FF`)
- **or** `341-0728.bin` (banks `$FC-$FD`) and `341-0749.bin` or `341-0748.bin` (banks `$FE-$FF`) for ROM 3
- **or** a pre-combined `apple2gs.rom`

Put them in `roms/` and rebuild. ROM 01 is what the work targets first: it is the common one, it is a single file, and ROM 3's extra 128KB is firmware the machine can run without.

See also: [[Machines]], [[Architecture-Overview]], [[CPU-Emulation]], [[Memory-System]]
