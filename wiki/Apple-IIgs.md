# Apple IIgs

**Status: not yet runnable.** The machine is in the registry, the menu and the window title, and the emulator marks it unavailable. Its processor is written and verified; nothing is wired to it yet. This page is the plan — what a IIgs is, why it cannot be another profile, where its code goes, and the order the parts arrive in.

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
    ├── iigs_memory.*             # FPI/Mega II map, banks, shadowing
    ├── iigs_video.*              # Super Hi-Res, over the Mega II's picture
    ├── iigs_sound.*              # Ensoniq 5503 DOC
    ├── iigs_adb.*                # keyboard and mouse microcontroller
    ├── iigs_battery_ram.*        # settings and the clock chip
    └── iigs_machine.*            # the coordinator, as Emulator is for the rest
```

The rule for this directory: **nothing in `core/iigs/` is included by a machine that is not a IIgs, and nothing outside it grows an `if (machine is a IIgs)`.** Where the two designs genuinely share a part — the Mega II's video generator is the obvious one — the shared code stays where it is and the IIgs's class uses it, the way `IWM` and `Disk2Card` share `DiskController`.

## The Order of Work

Each step is meant to be a commit that stands on its own, with tests that pass before the next one starts.

1. **Describe the machine.** Profile, family, spec header, ROMs, and an honest "not runnable". *(Done.)*
2. **The 65816.** *(Done.)* A standalone core in `src/core/cpu/65816/` with no emulator wiring at all: registers, both modes, every addressing mode, all 256 opcodes, cycle counts. Tested on its own against a flat 16MB of memory, and checked against 5.1 million recorded states from a real chip — see [[CPU-Emulation]] and `tests/conformance/test_65816_vectors.cpp`.
3. **Memory.** Banks, fast and slow RAM, ROM, the language card, and shadowing. Testable without a CPU.
4. **A machine that boots.** `IIgsMachine` wiring the two together with the Mega II's video borrowed from the existing `Video`, far enough to reach the Apple IIgs splash screen and a `]` prompt in 40 columns.
5. **Super Hi-Res.** The second video system and its palettes.
6. **Sound.** The Ensoniq, and the existing audio pipeline behind it.
7. **ADB, battery RAM, the Control Panel, and the slots.**
8. **The host.** A bigger framebuffer, the menu, the windows that assume a 6502.

## Things That Will Have to Give

Known places where the rest of the emulator assumes an 8-bit Apple II. None is a blocker; all are listed so they are not a surprise.

- **The shared framebuffer slot** (`FB_WIDTH`/`FB_HEIGHT` in `worker/shared-buffers.js`) is sized for the //e's 560x384 and cannot be resized once handed to the Worker. A IIgs's 640x400 does not fit, so the transport falls back to `postMessage` until the slot is sized for the largest machine.
- **Save states** are laid out to the saving machine's shape and carry a machine id. A IIgs state is a different shape again; `STATE_VERSION` will have to move.
- **The debugger** — disassembler, breakpoints, the trace — speaks 6502 and 16-bit addresses.
- **The agent tools and the wasm interface** are `g_emulator`-shaped, and `Emulator` is the Apple II family's coordinator.

## ROMs

Not distributed. A IIgs ROM 01 is a single 128KB image; a ROM 3 is 256KB across two chips, concatenated in bank order:

- `342-0077-B.bin` (128KB, ROM 01, banks `$FE-$FF`)
- **or** `341-0728.bin` (banks `$FC-$FD`) and `341-0749.bin` or `341-0748.bin` (banks `$FE-$FF`) for ROM 3
- **or** a pre-combined `apple2gs.rom`

Put them in `roms/` and rebuild. ROM 01 is what the work targets first: it is the common one, it is a single file, and ROM 3's extra 128KB is firmware the machine can run without.

See also: [[Machines]], [[Architecture-Overview]], [[CPU-Emulation]], [[Memory-System]]
