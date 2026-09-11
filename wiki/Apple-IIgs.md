# Apple IIgs

**Status: it boots, and you can select it.** The real ROM runs, passes its power-on diagnostics, draws the Apple IIgs splash screen through the Mega II, and stops at **Check startup device!** — which is what a real IIgs with no disk in it says. The menu marks it *In progress*: there is no sound and no disk to boot. You can type at it, though there is not yet much that listens. Super Hi-Res is drawn, but nothing in the firmware turns it on, so it appears when a program does.

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
    ├── iigs_sound.*              # Ensoniq 5503 DOC (RAM and window done)
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
6. **Sound.** The 32 oscillators, on top of the RAM and window that already exist, and the audio pipeline behind them.
7. **Input and settings.** *(Partly done.)* The keyboard works: a browser key event is translated the //e's way, handed to the ADB controller, and put by it into the register the Mega II reads — so //e software finds the keyboard where it expects it without knowing a microcontroller is involved. The clock and its 256 bytes of battery RAM work, and the firmware writes its settings there at startup. Still to come: the mouse, the Control Panel hotkey (which the controller itself intercepts on real hardware), and the slots.
8. **The host.** *(Partly done.)* The machine can be chosen from the menu and drives the display: the wasm layer holds either an `Emulator` or an `IIgsMachine` and routes the calls that run and show a machine to whichever it is. The shared framebuffer slot is sized for the largest picture (640x400) so the IIgs does not fall back to `postMessage`. Everything else — disks, printers, cards, the debugger, the agent tools — still asks for an `Emulator` and quietly does nothing while a IIgs is running.

## Things That Will Have to Give

Known places where the rest of the emulator assumes an 8-bit Apple II. None is a blocker; all are listed so they are not a surprise.

- ~~The shared framebuffer slot is sized for the //e's 560x384~~ — now 640x400, the largest any machine here draws. The other three write a smaller picture into a larger slot, which costs 164KB of address space and saves the one machine that would not fit from falling back to `postMessage`.
- **Save states** are laid out to the saving machine's shape and carry a machine id. A IIgs state is a different shape again; `STATE_VERSION` will have to move.
- **The debugger** — disassembler, breakpoints, the trace — speaks 6502 and 16-bit addresses.
- **The agent tools and the wasm interface** are `g_emulator`-shaped, and `Emulator` is the Apple II family's coordinator.

## ROMs

**The character generator is not in the ROM.** A //e keeps its font in a part of its own and a //c keeps its inside the system ROM; a IIgs keeps its inside the video chip, where the CPU cannot read it — searching a ROM 01 image for so much as one glyph finds nothing. So the machine is given the //e's set, which is the same font. Without one every glyph is blank, and since an inverse blank is solid, the screen shows white bars where the text should be.

**The banks can be either way round.** The obvious reading of a 128KB ROM 01 image is that it ends at `$FF:FFFF`, so its first half is bank `$FE` — and the dump this was written against is stored the other way. It is not a subtle difference: the emulation reset vector lives at `$FF:FFFC`, and reading it out of the wrong half gives zero and a machine that resets to `$00:0000` and sits there. So `IIgsMemory::loadROM` asks the image which way round it is, by looking for a usable reset vector at the top of each candidate bank.

Not distributed. A IIgs ROM 01 is a single 128KB image; a ROM 3 is 256KB across two chips, concatenated in bank order:

- `342-0077-B.bin` (128KB, ROM 01, banks `$FE-$FF`)
- **or** `341-0728.bin` (banks `$FC-$FD`) and `341-0749.bin` or `341-0748.bin` (banks `$FE-$FF`) for ROM 3
- **or** a pre-combined `apple2gs.rom`

Put them in `roms/` and rebuild. ROM 01 is what the work targets first: it is the common one, it is a single file, and ROM 3's extra 128KB is firmware the machine can run without.

See also: [[Machines]], [[Architecture-Overview]], [[CPU-Emulation]], [[Memory-System]]
