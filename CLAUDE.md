# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Apple //e Browser Based Emulator - A cycle-accurate Apple II Enhanced emulator running in the browser using WebAssembly (C++ backend) and WebGL rendering. No JavaScript frameworks; vanilla ES6 modules with Vite for bundling.

## Build Commands

```bash
npm install           # Install dependencies
npm run build:wasm    # Build WASM module (required first time and after C++ changes)
npm run dev           # Start dev server at localhost:3000 (hot-reload for JS only)
npm run build         # Full production build (WASM + Vite bundle)
npm run clean         # Clean build artifacts
npm run deploy        # Deploy to the configured rsync target (see .env.deploy.example)
npm test              # JavaScript tests (Vitest)
npm run check         # check:exports + check:core-purity + check:basic-tokens + npm test
npm run generate:basic-tokens  # Regenerate src/js/utils/basic-tokens.js from C++
```

## Deployment

`npm run deploy` (production) and `npm run deploy:staging` run `scripts/deploy.sh`, which rsyncs `dist/` to a target taken from the environment: `DEPLOY_TARGET` and `DEPLOY_STAGING_TARGET`. Copy `.env.deploy.example` to `.env.deploy` and fill it in; that file is gitignored, so the server's user, host and paths stay out of a public repository.

The script is one rsync invocation and nothing else, deliberately: the host locks out additional concurrent SSH sessions, so verification belongs over HTTPS rather than in a second connection.

## Testing

### JavaScript (Vitest)

`npm test` runs `tests/js/`. Config is in `vitest.config.js`, kept separate from
`vite.config.js` so the app build settings do not obscure test failures. The
modules under test are pure logic and run in plain node — a new DOM dependency
in one of them is a smell, not a reason to add jsdom.

Covers the printer emulation (characterization tests capturing the event stream
from `PrinterBase.setEventSink()`), the Applesoft listing parser, input
mapping, and the host-side machine profile (that a fetch failure leaves callers
with a usable //e rather than nothing, that a fetched profile actually reaches
them, and that a machine key is marshalled into the core's heap as a pointer
rather than passed as a JavaScript string), the game port device (that an
edited storage value falls back to the Apple joystick, and that an opposing
pair of Joyport directions is dropped rather than sent), which menu items each
machine is offered (`machine-availability`), and the save-state header the
host reads to tell which machine wrote a state (`state-header`).

### Consistency checks

`npm run check` runs three guards, each verified to fail when violated:

- `scripts/check-exports.sh` — `EMSCRIPTEN_KEEPALIVE` functions vs the
  `EXPORTED_FUNCTIONS` list in `CMakeLists.txt`, both directions
- `scripts/check-core-purity.sh` — no host-platform dependencies in `src/core/`
  (matches code, not comments, so docs may name what they warn about)
- `scripts/generate-basic-tokens.mjs --check` — the generated JS token table is
  in step with `src/core/basic/basic_tokens.hpp`

### C++ (Catch2)

All C++ tests use the Catch2 framework and are built/run via CMake's native build:

```bash
mkdir -p build-native && cd build-native
cmake ..
make -j$(sysctl -n hw.ncpu)
ctest --verbose
```

Test suites cover CPU (6502/65C02), memory (MMU, slots), video, audio, disk images (DSK/WOZ/GCR), expansion cards (Disk II, the IWM behind a //c's drive, Mockingboard, Thunderclock, Mouse, SmartPort, SSC), filesystems (DOS 3.3, ProDOS, Pascal), BASIC tokenizer/detokenizer, assembler, disassembler, keyboard, the Sirius Joyport, condition evaluator, machine profiles (each machine's numbers, the registry, that the subsystems take their timing from the profile they were handed, that the II+'s differences are real — an NMOS CPU, the //e's soft switches ignored, and colour burst left on in text mode — and that the //c is a //e in its numbers but has no expansion sockets, so every slot it decodes is fixed and every slot address reads its own ROM — each machine also booted to its prompt), the IWM (that it reads the same nibbles off the same image as the card, and that its register file answers to the Q7/Q6 pair), a //c's serial ports (where the ACIA answers, both directions of the line, and that its firmware drives them through PR# and IN#), a //c's IOU mouse (each switch, one interrupt per step, and its own firmware tracking a mouse across the screen and back to the clamp), and full emulator integration — including every machine booting DOS 3.3 through the controller it has, the IIgs included (which also checks the image it booted from is byte-for-byte what went in).

## Architecture

### Two-Layer Design

**C++ Core (src/core/)** - Pure emulation logic compiled to WebAssembly:

- `cpu/6502/cpu6502.cpp` - Cycle-accurate 65C02 processor (1.023 MHz)
- `cpu/65816/` - The IIgs's 65C816: 24-bit bus, 16-bit registers, native and emulation modes. `cpu65816.cpp` is bus, stack, addressing and operations; `cpu65816_dispatch.cpp` is the 256-opcode table
- `mmu/mmu.cpp` - 128KB memory management, soft switches ($C000-$CFFF), expansion slots, and the video scanner address generator behind floating-bus reads (Sather's counter equations, so blanking cycles read real video data rather than zero)
- `video/video.cpp` - TEXT/LORES/HIRES/DHIRES per-scanline rendering, split into a **signal stage** and a **decode stage** (see Composite Video below)
- `video/ntsc.cpp` - NTSC composite demodulation, the ideal/RGB digital decoders, and the calibrated 16-colour palette they all share
- `audio/audio.cpp` - Speaker emulation from $C030 toggles
- `disk-image/` - Disk image format support (DSK/DO/PO/NIB/WOZ). `gcr_encoding` holds the one copy of the GCR encode/decode routines and the DOS/ProDOS sector interleave tables that both image classes and the filesystem readers use; plus `disk_converter` — converts a loaded image between save formats (DOS order, ProDOS order, WOZ), including encoding a sector image to a WOZ bit stream
- `disassembler/` - instruction disassemblers: `disassembler.*` is the 65C02's, `disassembler65816.*` the 65816's. They are separate because a 65816 has no illegal opcodes, 24-bit addresses, modes a 6502 never had, and instruction lengths that depend on the M and X flags — so the processor's state is an input to disassembly. Its table is read off `cpu65816_dispatch.cpp` rather than a datasheet, and `test_disassembler65816.cpp` executes every opcode on the CPU in both widths and both modes and checks the distance the program counter moved against the length reported
- `assembler/` - Merlin-compatible 65C02 assembler (see Assembler below)
- `input/keyboard.cpp` - Keyboard input handling
- `input/joyport.cpp` - Sirius Joyport (two Atari-style digital sticks on the game connector)
- `input/mouse_iou.cpp` - A //c's mouse: IOU soft switches and an interrupt per unit of travel, rather than a card
- `iigs/` - The Apple IIgs's own parts, kept apart from every other machine's. `iigs_spec.hpp` holds the numbers no other machine has (two clock rates, fast and slow RAM, shadowing, Super Hi-Res geometry, sound RAM); `iigs_memory.*` is the 24-bit address space — banks, fast RAM, ROM, shadowing, and the SHADOW/SPEED/STATE registers; `iigs_video.*` is Super Hi-Res and the `$C029` switch between the machine's two video systems; `iigs_adb.*` is the keyboard and mouse controller; `iigs_clock.*` is the battery-backed clock and the 256 bytes of settings beside it; `iigs_sound.*` is the Ensoniq — its RAM, the window onto it, and the thirty-two oscillators, clocked and interrupting; `iigs_scc.*` is the Z8530 behind the serial ports, with nothing plugged into it; `iigs_machine.*` is the coordinator, as `Emulator` is for the 8-bit machines. Nothing here is included by a machine that is not a IIgs, and nothing outside it grows an `if (IIgs)`
- `machine/machine_profile.hpp` - Per-machine description (CPU variant, timing, memory sizes, display geometry, capabilities, slot layout) and the registry of machines. See Machine Profiles below
- `cards/` - Pluggable expansion card system (ExpansionCard interface)
- `cards/disk_controller.*` - The 5.25" drive mechanism both machines share: two drives, the stepper, the motor and Woz's Logic State Sequencer clocked from the P6 ROM
- `cards/disk2/` - Disk II controller card: the shared controller plus its P5A boot ROM
- `cards/iwm/` - Integrated Woz Machine, a //c's controller: the shared controller plus the status/handshake/mode registers, and no ROM
- `cards/mockingboard/` - AY-3-8910 sound chip + VIA 6522 timer + Mockingboard card
- `cards/mouse/` - Apple Mouse Interface Card
- `cards/parallel/` - Centronics parallel card (drives Epson FX-80 and Apple DMP)
- `cards/smartport/` - SmartPort hard drive controller (2 block devices, self-built ROM). A //e fits one in a slot; a IIgs has one in slot 5 as part of the machine
- `cards/softcard/` - Microsoft Z-80 SoftCard with Z80 CPU emulation
- `cards/ssc/` - Super Serial Card with ACIA 6551 (drives ImageWriter I and ImageWriter II)
- `cards/serial/` - A //c's two built-in serial ports: the SSC's ACIA 6551 with no card around it and no ROM
- `cards/thunderclock/` - Thunderclock Plus real-time clock card
- `filesystem/` - DOS 3.3, ProDOS and Pascal filesystem parsers, plus DOS 3.3 and ProDOS *writers* (`DOS33::writeFile`/`writeBinaryFile`, `ProDOS::writeFile`) used by the assembler's Merlin `DSK` directive; results are reported through the shared `FsWriteStatus` in `fs_write_status.hpp`
- `basic/` - Applesoft and Integer BASIC detokenizer, tokenizer, token tables, and
  variable representation (`applesoft_vars` — MBF floats, name/type decoding,
  VARTAB/ARYTAB walking)
- `debug/` - `machine_debug.*` (breakpoints, watchpoints, the trace ring, beam breakpoints — shared by both machines; see Debugging any machine), the condition evaluator for breakpoint expressions (supports BV/BA/BA2 for BASIC variable/array reads, and takes a `MachineView` so either machine can answer), and `debug_log` (host-installed log sink; the core never writes to a console itself)
- `noslot_clock.cpp` - DS1215 No-Slot Clock (ProDOS RTC at $C300)
- `emulator.cpp` - Core coordinator
- `emulator/emulator_state.cpp` - State serialization (exportState/importState)
- `emulator/emulator_debug.cpp` - Debug facilities (breakpoints, watchpoints, trace, beam)

**JavaScript Layer (src/js/)** - Browser integration:

- `main.js` - AppleIIeEmulator class orchestrating all subsystems
- `worker/` - Web Worker infrastructure for WASM isolation (see Worker Architecture below)
- `audio/` - Web Audio API driver and AudioWorklet
- `display/` - WebGL renderer, CRT shader effects, display settings, screen window, no-signal screen
- `disk-manager/` - Disk drive UI, SmartPort hard drives, persistence, surface rendering, drive sounds, URL-parameter media loading
- `file-explorer/` - DOS 3.3 and ProDOS disk browser with disassembler
- `debug/` - Debug window implementations (see Debugging section)
- `help/` - Documentation and release notes windows
- `input/` - Keyboard input, text selection, joystick, mouse
- `ui/` - Menu wiring, reminders, slot configuration, custom confirm dialogs
- `state/` - State serialization and persistence (autosave + 5 manual slots)
- `config/` - App version
- `utils/` - Shared utilities (storage, string, BASIC)
- `windows/` - Base window class and window manager

### Interrupts

**The IRQ input is a level, and the CPU samples it every instruction.**
`CPU6502::irq()` is an edge, latched until the CPU can take it — that is how a
device interrupting while the I flag is set is not forgotten — but the line
itself is polled through `setIRQStatusCallback()`, a predicate the `Emulator`
builds from the devices that can hold it down: the Mockingboard's VIAs, the
mouse (a card's, or a //c's IOU), and the serial ports. Without the poll, a
handler that returns without clearing its device is never re-entered, and a
device that lets go can still deliver one more interrupt from the latch —
neither of which is what the hardware does.

The predicate is deliberately a handful of null checks against pointers the
`Emulator` already holds, not a walk of the slot array asking every card: the
CPU dispatch loop is the hottest code here and eight virtual calls per
instruction would be paying for devices that do not exist. It is also only
sampled while the I flag is clear, which costs under 1% rather than ~4%. A card
that can hold the line and is not in that list is still heard through its own
edge; what it cannot do is re-interrupt a handler that ignored it.

### The 65816

`CPU65816` (`cpu/65816/`) is a separate class from `CPU6502`, and deliberately:
a 65816 has a 24-bit bus, 16-bit registers whose width changes at runtime, a
direct page and a stack that can sit anywhere in bank zero, separate banks for
code and data, and a second operating mode. Folding that into `CPU6502` would
put a width test on every load, store and arithmetic operation in the hottest
loop of a //e to serve a machine a //e is not. A machine is built from one or
the other.

**Cycle counts, not the cycle pattern.** `CPU6502` models which cycle of an
instruction touches which address, because a //e's video reads the bus during
those cycles. A IIgs's video does not read the 65816's bus at all — it reads the
Mega II's, on the other side of the machine — so this core counts cycles and
does not pretend to place them.

**Three rules in it are worth knowing before changing anything:**

- **Widths belong to the processor, not to the addressing mode.** Every
  operation takes an effective address and reads its own operand at whatever
  width the flags currently say, which is why `opADC` takes a `uint32_t` and
  not a value.
- **Two kinds of address behave differently at the top of a bank.** An
  immediate operand comes from the program bank and a direct page or stack
  operand from bank zero, and neither bank ever increments; an operand reached
  through the data bank does cross into the next one. They are the same number,
  so the addressing mode says which it produced (`operandWrapsInBank_`) and the
  next access consumes the answer.
- **The instructions a 6502 never had ignore emulation mode's stack wrap.**
  PHD, PLD, PEA, PEI, PER, PLB, JSL and RTL walk the stack pointer through all
  sixteen bits and it is forced back into page one at the end, which is why PLD
  with the pointer at `$01FE` really does read its high byte from `$0200`.

**It is verified against 5.1 million recorded states from a real 65816**
(SingleStepTests/65816): every opcode, both modes, 10,000 vectors each,
registers, memory and cycle count. `tests/conformance/test_65816_vectors.cpp`
runs them and skips unless `A2E_65816_VECTORS` points at the files, which are
3GB and not in the repository. Four bugs came out of it that the unit tests did
not find: the indexed page-cross cycle applies when the index is 16 bits wide
*or* crosses a page rather than only crossing; writes and read-modify-writes
never pay it; decimal mode takes V from the value before the top digit's
correction; and the two bank-wrap rules above.

### A IIgs's memory

`IIgsMemory` (`core/iigs/iigs_memory.*`) is the 24-bit map, and **the Mega II
side of it is an `MMU`** — the same class a //e is built from, constructed with
the IIgs profile. Banks `$E0`/`$E1` are its main and auxiliary RAM, `$C000-$CFFF`
in the four banks that see it are its soft switches, and `$D000-$FFFF` is its
language card. That is not a convenience: a IIgs really does contain a //e, and
when the video is written it will read that MMU exactly as `Video` already does.

Three things in it are worth knowing:

- **Shadowing is a copy, not a redirection.** A write to a display region of
  bank `$00` or `$01` lands in fast RAM *and* is copied to `$E0`/`$E1`, because
  the video only ever looks at the Mega II's side. Which regions those are is
  the `$C035` register, and its bits read backwards: a set bit turns a region's
  shadowing **off**.
- **`$C035` bit 6 changes what an address is**, rather than where a write also
  goes: with I/O and language card shadowing inhibited, banks `$00`/`$01` are
  plain RAM from `$C000` up, which is how a program gets a contiguous 128KB.
- **Bank `$00` still obeys the //e's memory switches, and they send it into
  bank `$01`.** A IIgs is a //e whose main RAM is bank `$00` and whose
  auxiliary RAM is bank `$01`, so RAMRD and RAMWRT move `$0200-$BFFF`, ALTZP
  the zero page, stack and language card, and 80STORE with PAGE2 (and HIRES)
  the text and first hi-res pages — overriding RAMRD/RAMWRT there.
  `IIgsMemory::effectiveBank` is the rule, applied before the write lands and
  before it shadows, so a bank `$00` write that belongs in `$01` reaches `$E1`.
  The 80-column firmware depends on it: a line's even columns go to the text
  page with 80STORE and PAGE2 on, and a machine that left them in bank `$00`
  drew every other column blank. Bank `$01` is never redirected. This is the
  same rule GSSquared applies in `calc_aux_read`/`calc_aux_write`.
- **`$C068` (STATEREG) is eight of the //e's soft switches in one byte**, and
  writing it drives those switches through their own addresses so everything
  watching them sees the change the usual way. It has no bit for the language
  card's *write* latch, so `setStateRegister` reads that off the machine and
  preserves it: changing the memory map must not quietly write-protect the
  card, or quietly unprotect it.

### A IIgs that boots

`IIgsMachine` (`core/iigs/iigs_machine.*`) is to a IIgs what `Emulator` is to
the other three: it owns the CPU, the memory and the video, and runs them. The
video is the //e's `Video` class reading the Mega II's MMU, because that is
what a IIgs's //e-mode picture is drawn by.

**The machine has two clocks, and the slow one lives in `IIgsMemory`.** The
65816 runs at 2.8MHz until it reaches the Mega II, and that *access* is
stretched to a 1.023MHz cycle — so the clock ticks inside the memory, as each
slow-side access happens, and `IIgsMachine` adds the rest of the instruction
afterwards at whatever the speed register says. Keeping it there is what lets
it advance *during* an instruction: a disk read loop is a few cycles with one
access in it, and a drive whose clock only moved between instructions sees that
loop in lumps.

**The rest of the instruction is the rest of it.** `takeSlowAccesses()` returns
how many of an instruction's cycles went to the slow side, and `step()`
subtracts them before converting what is left — a cycle spent waiting on the
Mega II is not also a cycle spent running. Charging both halves is easy to do
and invisible until something is timed against it: the boot ROM's read loop
runs out of bank `$00`'s I/O space, so *every* cycle of it is a slow access,
and it came out at thirteen cycles where the disk expects seven.

**A Mega II access from the fast side waits for the slow clock, and fast RAM
is refreshed.** `IIgsMemory::slowAccess` charges an access to the Mega II
the rest of the slow cycle in progress and then a whole one, because the
processor stops at the slow clock's edge; `IIgsMachine::slowCyclesFor`
stretches fast cycles run from RAM by one refresh cycle in every ten (2.8MHz
comes out near 2.5), and code in ROM goes at the full rate. Both are
GSSquared's rules, and the Apple IIgs Diagnostic's speed test is the check: it
counts a nine-cycle loop between two changes of `$C02E` and accepts 25 or 26
at fast speed and 14 or 15 at slow, which is what the machine now counts
(`test_iigs_boot.cpp` runs the same loop). `$C02E`/`$C02F` are the Mega II's
counters as the IIgs exposes them — vertical `$100-$1BF` over the picture and
`$1C0-$1FF` then `$FA-$FF` through blanking, horizontal 0 then `$40-$7F` — from
a beam query the machine installs. `$C046`'s flags say what happened whether
or not it was enabled, and only `$C047` clears them; the diagnostic's handler
switches VBL off before it looks and must still find the flag.

**`$C036`'s bottom four bits are a veto on the fast clock, not a speed
setting.** They are slot motor detect, one each for slots 4 to 7, and a drive
turning in an enabled slot drops the whole machine to 1.023MHz until it stops.
`IIgsMemory::isFastSpeed()` asks a `SlotMotorQuery` the machine installs, so
the memory needs to know nothing about drives. This is what makes a Disk II
readable at all: the controller holds a finished byte for about two bit cells,
and at 2.8MHz the firmware's poll comes round three times per byte and reads
half of them twice.

**Control-Reset and the power switch are two different things on a IIgs, as
on a //e.** `IIgsMachine::warmReset()` is the RESET line: the registers, the
Mega II's switches and the chips that take the line go back to their reset
values and the CPU takes the vector from ROM, but the fast RAM, the Mega II's
RAM and the disk in the drive are exactly as they were — so the firmware finds
its warm-start bytes at `$03F2` and restarts what was running. `reset()` is the
power switch, and it now clears the *fast* RAM too: it used to leave it, and
since the firmware decides between a cold and a warm start by what it finds in
bank `$00`, every "Reboot" was a warm one. `_warmReset` used to call `reset()`
for a IIgs, so Control-Reset was a reboot. `test_iigs_boot.cpp` pins both.

**`$C029` bit 5 shows double hi-res in black and white.** The System 2
Finder and the 80-column desktop programs draw a 560-dot double hi-res
picture with Super Hi-Res *off* and this bit *set*, and the VGC shows the dots
as they are; decoding them into colour instead — which the Mega II's video did,
knowing only bit 7 — fringed every letter red and green. `IIgsMemory` reports
the register through `setNewVideoCallback` and `Video::setDoubleHiResMonochrome`
sends a double hi-res line through the monochrome decoder, white on black; a
monochrome monitor still has the last word. `test_iigs_video.cpp` pins colour
without the bit, grey with it, and green on a green screen either way.

**A IIgs's text is drawn, not transmitted.** `$C022` (TCOLOR) holds the two
colours the VGC substitutes for lit and unlit text dots and the bottom nibble of
`$C034` holds the border — the top nibble of that address is the clock's, which
is why `$06` sets a blue border and starts no transaction. `Video::setTextColours`
is how that reaches the //e's video: a text line is decoded into those two
colours instead of through a receiver, and a machine that never calls it behaves
exactly as before. Monochrome still overrides it, because a monochrome monitor
has one phosphor whatever the machine sent.

**A IIgs's frame is the raster a monitor shows, border included.** The
Mega II's line is 65 cycles: 40 of picture, 12 of blanking, and 13 of border —
6 before the picture and 7 after; its frame is 262 lines: 200 of picture, 22 of
blanking, and 40 of border — 19 above and 21 below. Those are the cycles
GSSquared's scanner flags as border. **What is drawn is the part of that a
monitor's bezel does not hide**: three cycles either side and twelve lines
above and below, which keeps the border's shape and puts it at about the width
it has on the glass — every border cycle drawn made it a fifth of the picture's
width, which no monitor of the period showed. `iigs_spec.hpp` holds both sets
of numbers. At Super Hi-Res's 16 pixels a cycle the frame is therefore
**736x448** (lines doubled) with the 640x400 picture at (48, 24), and
`IIgsVideo` fills the rest with the colour in `$C034`'s bottom nibble — in
Super Hi-Res too, which used to fill the frame with no border at all. **The
//e's 560x384 goes in the same width, stretched to 640** (eight pixels for
every seven dots, linearly), because a text screen and a Super Hi-Res screen
are the same width on the monitor, **and centred in the 200 lines**
(`MEGAII_TOP`), because 192 is eight short of 200 and putting all eight at the
bottom made the bottom border deeper than the top. The profile's `text` rectangle says where the text
screen landed, so the host's text selection maps a pointer onto a cell through
it rather than assuming the text fills the frame, and its `aspect` says the
shape the monitor shows the frame at: a //e's 560x384 at its own ratio, as
always, and the IIgs raster at 4:3. `machineAspect()` drives the screen window
and a `--screen-aspect` CSS variable drives the full-page layout. The shared
framebuffer slot is sized 848x480, which holds it. `test_iigs_video.cpp` pins
the raster and `test_machine_profile.cpp` the profile.

**The SCC is a real Z8530 with nothing plugged into it.** `IIgsSCC`
(`core/iigs/iigs_scc.*`) at `$C038-$C03B` is the register file behind the
command/data pair per channel, the transmitter and receiver with their
timing, local loopback and auto echo, the baud rate generator, and the
interrupt logic — because software exercises all of that without a cable.
The Diagnostic's Serial Internal Test writes every register and reads it
back, then arms the zero-count interrupt with the slowest time constant and
measures the interval between two of them: **the zero count comes every
`TC + 2` clocks of 3.6864MHz, not twice that**, because the generator's output
toggles at each zero and the baud rate is half the zero count. A generator
counting the output's period fell outside the window. It then sends bytes
round the local loop at 600 baud, polling RR1's all-sent and RR0's receive
bit. `IIgsMachine::step` advances the chip on the slow clock beside the
Ensoniq, and `IIgsMemory::interruptPending()` includes it, gated on WR9's
MIE. **A loopback cable is fitted between the two ports** —
`IIgsSCC::setLoopbackCable`, on by default because nothing else is ever
plugged in — crossing each port's transmit into the other's receiver and
its DTR into the other's CTS, which is what the External Serial Ports Test
asks for. **The transmitter is clocked from whatever WR11 selects**: the
crystal on RTxC, the generator, or the TRxC pin, then WR4's divider. The
Serial Crystal Test clocks a byte straight from the crystal at x64 and times
its all-sent at 174 microseconds; a transmitter that took the generator's rate
regardless took a fifth of a second. `test_iigs_devices.cpp` pins the
register file, the zero count, the loop, the cable, the clock source and the
interrupts.

**The clock chip is a serial line, and it answers on the read transfer.**
`$C033` is the byte and `$C034` drives it: bit 7 starts a transfer, bit 6 is
its direction (set, the chip supplies the byte; clear, it takes the one in
`$C033`), and bit 5 holds the chip selected across the transfers of one
transaction — the firmware's driver drops it after every one, and
`IIgsClock` goes back to expecting a command when it does. A transaction is a
command byte and then a data byte, each its own transfer, with the 256 bytes of
battery RAM addressed across two command bytes. What matters is *when* the
chip answers a read: on the read-direction transfer, not when it sees the
command. The driver — the same routine in the ROM and in the IIgs Diagnostic —
stores whatever it is holding to `$C033` before every transfer, the read of
the data byte included, so a chip that answered early had its answer
overwritten and then took the junk as its next command. The Diagnostic's Clock
RAM Test read every clock byte back as that junk, retried 256 times, and
dropped into the monitor. The seconds are seeded from the host's clock when
the chip is made and then counted by the machine: `IIgsMemory::tickClocks`
ticks the chip on the same second that raises the VGC's one-second interrupt,
because on the real machine that interrupt *is* the chip's tick. The
Diagnostic writes `$FFFFFFFF` and waits for the roll-over, which a clock
reading the host would never show a machine running faster than real time.
`test_iigs_devices.cpp` pins the protocol, the junk, and the tick.

**Banks `$00` and `$01` are 64K of fast RAM each, language card included.**
Their `$D000-$FFFF` is the bank's own memory in the shape of a //e's card, with
the second `$D000` bank being the 4K hidden under `$C000`; the Mega II's card
belongs to `$E0`/`$E1` alone, and `MMU::readLanguageCardRAM`/`writeLanguageCardRAM`
take main-or-aux as an argument for it. `IIgsMemory::fastLanguageCardAddress` is
the rule for the fast side. Three earlier models of this each broke GS/OS in a
way that looked like something else — see `wiki/Apple-IIgs.md`.

**Vectors are pulled from ROM whatever the map shows** — the FPI answering the
65816's VPB line. `CPU65816::setVectorReadCallback` is the hook; nothing on a
IIgs writes a vector into RAM, and GS/OS copies its kernel over `$D000-$FFFF`
with interrupts enabled.

**Interrupts.** `IIgsMemory::interruptPending()` is the OR of the ADB
(`$C027`, full/enable pairs), the VGC (`$C023`: the scan line is enable bit 1
with flag bit 5, the one-second tick enable bit 2 with flag bit 6; both
acknowledged through `$C032`, bit 5 low for the scan line and bit 6 low for
the second), and the Mega II (`$C041`/`$C046`/`$C047`, VBL and quarter-second);
the CPU samples it every instruction. The scan-line interrupt is asked for by
bit 6 of a Super Hi-Res line's control byte and raised by
`IIgsMachine::raiseScanLineInterrupts` as the beam finishes that line.
QuickDraw II draws the mouse pointer from it, through the handler it installs
at `$E1:0028` — the vector the ROM's `AND #$22 / LSR / LSR` dispatch reaches —
so with the two VGC pairs swapped the pointer was redrawn once a second, on the
tick that arrived through QuickDraw's vector instead. The ROM's manager asks the SCC *first*
and the Ensoniq's `$E0` *last*, so `$C038-$C03B` must answer RR3 with nothing
pending until something is, and register `$E0` reads active-low "none" —
either one wrong is *Unclaimed Sound Interrupt*. `$C071-$C07F` map the ROM's vector firmware into the I/O page.

**The 256 bytes of battery RAM are the host's to keep**, because a real
machine's battery keeps them and the Control Panel's settings only mean
anything if they survive. `src/js/machine/iigs-battery-ram.js` restores them
before the machine runs and writes them back when the core says they changed
(`_batteryRamChanged()`, one boolean, rather than comparing 256 bytes).
**They go out and come back exactly as the firmware wrote them, checksum
included**: the firmware validates that checksum before trusting the contents
and its algorithm has not been worked out here, but it never needs to be as
long as nothing alters the bytes. Alter one and the firmware writes its own
defaults over the lot, which is what a machine with a dead battery does on
every start and is what this machine did before. GSSquared keeps its battery
RAM in a file the same way and also does not compute the checksum.
`test_iigs_boot.cpp` proves the firmware then leaves them alone: not one byte
written on the second start.

**The ADB controller has to take exactly the bytes each command carries, and
has to answer a bus transaction in a frame.** The firmware writes a command and
then its arguments to `$C026`, so a command whose argument count is wrong leaves
its own bytes to be read as commands: read-memory takes *two* bytes because its
address is sixteen bits, a Listen takes two, and the undocumented `$12`/`$13`
take two. A command above `$1F` addresses the bus rather than the controller —
high nibble the command, low nibble the device, `$8n-$Bn` Listen registers 0 to
3 and `$Cn-$Fn` Talk, with `$70-$73` the controller's own "stop polling that
device". A Talk is answered with a header byte with bit 7 set whose bottom three
bits are one *less* than the count that follows, because the firmware's read
loop counts down to one after an INY; a device with nothing to say still sends
the header. Register 3 is what the firmware enumerates the bus with, and answers
with the device's address and a handler byte. Get any of it wrong and the
firmware sits in a read loop until its own counter expires, reports the
transaction incomplete, and unwinds through a tool error path whose `RTL` lands
in the middle of an instruction in the Tool Locator — so the boot ends in the
monitor, with the message hidden behind whatever Super Hi-Res was showing. That
is what stalled a System 6.0.4 install disk at its splash screen.
`test_iigs_devices.cpp` pins the counts, the frame and the empty answer.

**`$C025` says which modifier keys are down**, and it used to read zero
whatever was held. The Event Manager reads it on every event, so a machine
answering zero has no shift-click and no command-key menu shortcut.
`IIgsMachine::reportModifiers` fills it in from the browser's own flags plus
the Apple keys the `Keyboard` already tracks for a //e's pushbuttons, and the
latch in bit 5 comes up on any change and clears on a read, which is how a
program tells "nothing held" from "pressed and released between two polls".

**The Control Panel's hotkey still does not work, and the reason is now
known rather than guessed.** Control-Open-Apple-Escape reaches the machine
correctly — the modifiers read `$A2` and the key latches as `$9B` — but
nothing running looks at it. Measured under ProDOS with a program polling for
a key: `$C000` read 111,899 times while the hotkey was held, `$C025` read
zero times. The ADB microcontroller is not the answer either: `$C026`'s
sequence-detect bits are Control-Command-Reset and Control-Command-Delete and
there is no bit for Escape (Table 6-3 of the Hardware Reference). That leaves
the firmware's interrupt-driven Desk Manager, which never starts here — the
ADB status register reads `$00` after boot, so the keyboard interrupt it would
need was never enabled. Finding what enables it is where the next attempt
should start.

**How much fast RAM a IIgs has is a user choice**, from 256K to 8M, in the
Machine menu and remembered in localStorage. `_setIIgsMemoryKB` rebuilds the
machine, as switching machines does, and `main.js` applies the remembered size
*before* the machine is built rather than after. `clampFastRamSize` rounds to
whole 64K banks; banks above what is fitted must not answer, because the
firmware sizes memory by writing to one and reading it back. System 6.0.4 boots to the Finder
with a working mouse; the built-in SmartPort in slot 5 serves it hard drive images
through GS/OS's extended calls.

**A IIgs's seven slots each hold two things, and `$C02D` says which answers.**
Chapter 8 of the Hardware Reference: every slot is a real socket *and* has a
built-in device assigned to it, and "only one device can be selected at a time
for each slot". The register moves **both the ROM and the I/O** for slots 1, 2,
5, 6 and 7; for slot 4 it moves the ROM only, because "I/O space for slots 3
and 4 is always enabled"; and slot 3 is not in the register at all — bit 3 is
reserved and its ROM follows the //e's own SLOTC3ROM. `IIgsMemory` holds the
user's cards in `slotCards_`, separate from the Mega II's slots where the
machine's own parts live, and `slotIOIsCard`/`slotRomIsCard` are those rules.
A slot switched to a card that is not there reads the floating bus, and the
machine's own device must *not* answer in its place.

**The Control Panel's setting is held against the firmware, because we are the
Control Panel.** On a real machine that choice lives in battery RAM and the
firmware copies it into `$C02D` on every start. We cannot write that battery
RAM — the checksum algorithm is not known here and the firmware would rewrite
its defaults — so `IIgsMemory::overrideSlot` remembers the bits the user
actually chose and a firmware write to `$C02D` is *merged* rather than obeyed
for those. A slot nobody has touched is left entirely to the firmware, which is
what keeps a machine with no cards behaving exactly as before. Without this a
card fitted before boot was ignored from the first reset onwards.

**`$C800-$CFFF` is one window seven cards share**, and a card claims it by
having its own `$Cn00` read; `$CFFF` hands it back, as INTC8ROM does on a //e.
Without it a card with more firmware than 256 bytes — a Super Serial Card, a
Thunderclock, a parallel card — has nowhere to put the rest of it.

**A card's samples are added after the `$C03C` amplifier, not through it.** A
Mockingboard in a socket has its own output on the real machine, so scaling it
by the volume nibble would fade a card's music along with the ROM's bell — the
same reason the Ensoniq is kept off that nibble.

**A slot given to "Your Card" with nothing in it reads the bus.** `$C02D`
says which slots the internal firmware answers for; a slot switched away from
it with no card fitted answers `$FF`, as an empty slot on any Apple II answers
with the bus, rather than showing the firmware the setting was meant to hide
(`IIgsMemory::slotIsExternalAndEmpty`). ProDOS 8 2.4.1 finds the AppleTalk
firmware's `ATLK` signature in slot 7 and calls into it, which ends in a BRK
at `$C711` on a ROM 01 — a ProDOS 2.4.1 bug, fixed in 2.4.2 ("not compatible
with the AppleTalk Workstation card"), and 2.4.3 boots here — and the known
way round it is to set slot 7 to Your Card, which only works if the firmware
then goes away.

**The IIgs's SmartPort answers where the machine's own firmware does.** The
real slot 5 firmware has `$C5FF = $0A`: its ProDOS entry at `$C50A` and its
SmartPort entry at `$C50D`, and software written for a IIgs hard-codes those
rather than reading `$C5FF`. `SmartPortCard::setProDOSEntry(0x0A)` lays the
card's ROM out that way (the fall-through boot path branches over the entries
to its stub at `$10`), and its `$C5FE` status byte is the firmware's `$BF`
whatever is fitted — four volumes, removable, interrupting. ProDOS 8 1.x needs
the drive 2 that byte implies: its device-table builder pushes a byte per
device that is not the boot device and pops one per other device in the boot
slot, which only balances with two drives there. A card laid out like a card,
reporting the one image it held, sent every demo disk booting ProDOS 8 1.x
into a BRK — first at `$C711`'s neighbour when a `JSR $C50D` found an RTS, then
after the ProDOS splash from the unbalanced stack. `SmartPortCard::
setTransferCallback` reports every block transfer, for a trace or a debugger.

**Slot 5 is the IIgs's SmartPort, and it is part of the machine** — no card to
fit, no Control Panel setting. `IIgsMemory::setInternalCardSlot` names the slot
that answers at `$Cn00` whatever `$C02D` says, because a part the machine has is
on the internal side of that switch. The machine's own slot 5 firmware is real
but polls the IWM for a Sony 3.5" drive, so it cannot serve a block image; with
nothing inserted the SmartPort has no ROM and that firmware shows through.
`SmartPortCard::setExecutingAt` is how a trap card is told the CPU is executing
its entry point rather than reading it — a 6502 has already advanced the program
counter by then and a 65816 has not, and the card must not guess.

**A IIgs has a game port like every other Apple II, and the host drives it
through the same three calls.** The paddle timers are the Mega II's, so
`IIgsMachine::setPaddleValue` hands the value to the MMU inside it, and
`setButton` holds one of the three pushbutton lines down — ORed with the Apple
keys in the button callback, because `$C061`/`$C062` are one line each rather
than two. The bindings for all three used to answer only `g_emulator`, which
returns early while a IIgs is running, so a joystick, a gamepad and the
Joystick window's cursor keys moved nothing at all on that machine.

**The Sirius Joyport fits that connector too**, and the multiplexing works
here as it does on a //e: the annunciators choose the stick and the axis pair,
and the Joyport answers `$C061-$C063` *instead of* the Apple keys, active low.
**Its reset guard has to be a hundred times longer than a //e's.** Both lines
idle high, which is a held Open and Closed Apple to firmware deciding how to
start, and a IIgs asks twice — measured at about 229,000 and 396,000 cycles of
the Mega II's clock, after its power-on diagnostics, and never again — where a
//e's reset routine asks within a few milliseconds. With the //e's 50,000-cycle
window a machine with a Joyport fitted went into the self test and drew nothing
at all; `IIgsMachine::JOYPORT_RESET_GUARD_CYCLES` is a second's worth, which
covers both looks and is still far shorter than the time anything takes to boot
off a disk and ask about a stick. `test_iigs_boot.cpp` pins the table and the
boot.

**A mouse report's two top bits are two buttons.** `$C024` gives X then Y,
seven bits of movement each; the X byte's bit 7 is button 1, which the mouse
here does not have, and the Y byte's is button 0. The same button in both
was two presses to the firmware, and the Finder opened a folder on a single
click.

**The volume nibble in `$C03C` reaches the speaker and not the Ensoniq.** One
amplifier really does carry both on the machine, and modelling that sounded
wrong: sound software drops the nibble to about 5 and puts it back to 15
around every burst of DOC access, in flips lasting well under ten
milliseconds, so scaling the synthesiser by it wobbles a steady note at
whatever rate the software happens to be transferring at, and leaves the
average level low as well. The host's volume control is the amplifier for the
Ensoniq instead. GSSquared does the same, for the same reason, and names two
more: a stereo card taps the DOC's channels ahead of the volume control, and
at least one game sets the nibble to zero while playing through one. The
speaker keeps the nibble, because the ROM's bell fades by walking it down.

**Where the nibble is applied it is a taper, not a ratio.**
`amplifierGain()` in `iigs_spec.hpp` is a cube root, and that came out of a
measurement: `nibble / 15` put the machine 9.5dB below a //e for the same
speaker click at the setting its own firmware boots with, 0.150 peak against
0.450. There is no turning it up from inside either, because the Control Panel
hotkey is not implemented and the firmware rewrites battery RAM's volume byte
(`$1E`) whenever its checksum does not match. The taper keeps what the nibble
is for — silence at zero, full output at fifteen, every step ordered — and
puts the default within 3dB of the other machines.

**The nibble also reads back as it was written**, which it did not:
`readControl()` forced it to 15, so the Control Panel's volume setting and the
toolbox's `SetSoundVolume`, which all change it by reading the register and
writing it back, were working from a machine that claimed to be at full
volume. `test_iigs_devices.cpp` pins the taper, the readback and the
Ensoniq's independence from the nibble; `test_iigs_boot.cpp` pins the
speaker's level at the firmware's own volume.

**A IIgs has a speaker as well as an Ensoniq.** `$C030` is a Mega II address, so
`IIgsMachine` owns an `Audio` toggled on the slow clock and adds the Ensoniq's
samples on top. Without it the machine is silent through every beep and click.
The volume nibble in `$C03C` is the amplifier's and scales both: the ROM's bell
fades out by turning it down, and the firmware sets it to 5 from battery RAM.
For the speaker the gain follows the nibble's writes at the slow-clock times
they happened (`IIgsMemory::setVolumeCallback`, applied per sample in
`IIgsMachine::generateStereoAudioSamples` through a twenty-millisecond slew),
after the coupling stage: one gain per buffer, taken from the nibble at the
buffer's end, made the fade a staircase and brought the speaker's decaying
tail back at full level when the ROM put the volume back — a note after the
bell. `test_iigs_boot.cpp` rings it and checks the envelope.

**The Ensoniq runs on the machine's clock and it interrupts.** `IIgsSound` is
the chip as GSSquared and MAME model it — resolution-shifted table addressing,
a zero byte halting every mode, the table's end wrapping free-run and halting
the rest, swap mode handing over to the partner, sync mode restarting the
oscillator below, one scan per `8 × (oscillators + 2)` ticks of 7.16MHz.
`IIgsMachine::step` feeds `advance()` the slow clock and the chip produces a
frame per scan into a ring that `generateSamples()` resamples to the host at
the chip's rate over the host's, nudged by up to half a percent to hold the
backlog near four milliseconds. **Every oscillator is summed, whatever channel it
is assigned to**, because the chip has one analogue output pin: it visits its
channels in turn and puts each one's sample on that same pin, with the channel
strobes saying which channel is on it. A stock machine filters the pin and
hears the sum; only a stereo card in a slot uses the strobes to pull the
channels apart, and there is no such card here. Splitting by the channel field
instead put a game's bass in one speaker and its melody in the other — Spy
Hunter played one or the other rather than both. **The uppermost enabled
oscillator is heard three times over**, which is real silicon and is MAME's
note. An
oscillator with its interrupt bit set raises one when it halts; `$E0` names it
active low and clears it on the read; `IIgsMemory::interruptPending()` includes
the chip. The sound tools play every sample through swapped pairs refilled
from those interrupts, so a chip that only ran when the host asked for a
buffer, and never interrupted, played the first buffer of anything and stopped.

**A IIgs's printer is on the back of the machine, and the port is channel A.**
The two sockets are the two halves of one Z8530, and which half is which was
measured rather than reasoned about: slot 1's firmware programs `$C039`/`$C03B`
and slot 2's `$C038`/`$C03A`, so the printer port is channel **A** — the
opposite of what both the address order and the port numbering suggest.
`IIgsMachine::setSerialTxCallback` hands the host a byte with the port it left
by (1 or 2) and `serialReceive` puts one into the modem port, which is what the
//c's pair of calls mean on a machine with two ports. The host's own printer
does not have to know any of it: `serial1`/`serial2` in slots 1 and 2 of the
IIgs profile are the same names a //c uses, so the printer manager finds an
ImageWriter reachable without being told about a third machine.

Two things in that path print nothing at all when they are wrong, and both are
pinned by `test_iigs_boot.cpp`. **An unplugged port answers as a device that is
present and ready** — CTS *and* DCD — because what is on the end of it is an
emulated printer, and the firmware polls both before every character; a port
that answered honestly sat in that loop for ever. And **the loopback cable
between the two ports is not fitted by default**: it is a test rig that only
the Apple IIgs Diagnostic's External Serial Ports Test asks for, and with it on
a byte the printer driver sends goes round to the other socket instead of out
of the machine. It is a tick box in the Serial Port window, deliberately not
remembered across sessions.

**ENABLE is not the motor, and `DiskController::isDriveEnabled()` is the
difference.** A drive keeps turning for about a second after the CPU switches
it off; `isMotorOn()` says so, and that is right for reading. But the IWM's
mode register is writable exactly while the *line* is low, and the sequencer
must not write flux when it is. The IIgs firmware exercises both in one
instruction — it switches the drive off and writes the mode register at
`$C0EF`, which is also Q7 — so a machine that asks about the mechanism instead
of the wire spins for a second and erases track zero while it does it.

**Two devices had to exist before the machine would draw anything**, which is
earlier than the plan expected: the firmware's power-on diagnostics sync and
interrogate the **ADB** controller and test the **Ensoniq's** RAM before the
splash screen. A IIgs whose `$C027` never answers stops with `Fatal system
error-> 0911`. Both are real devices in their own files now, with the keyboard,
the mouse and the synthesiser still to come.

**A ROM image's banks can be either way round**, and `loadROM` asks rather than
assumes: it looks for the emulation reset vector, which every IIgs ROM has at
`$FF:FFFC`. Get it wrong and the machine resets to `$00:0000`.

### Machine Profiles

The emulator models one machine at a time, and which machine it is comes from a
**profile**: `src/core/machine/machine_profile.hpp` holds a `MachineProfile`
per machine and a registry of them. There are four: `APPLE_IIE_PROFILE`,
`APPLE_II_PLUS_PROFILE`, `APPLE_IIC_PROFILE` and `APPLE_IIGS_PROFILE`.

**A profile also says which family it belongs to, and that is what selects the
parts.** `MachineFamily::AppleII` is the three 8-bit machines: one design, built
from `MMU`, `Video`, `Audio` and `CPU6502`, differing only by the numbers in
their profiles. `MachineFamily::AppleIIgs` is a different computer — a 65816 on
a 24-bit bus, a memory controller that shadows banks, a second display system,
an Ensoniq — and it is built from its own classes in `core/iigs/`. The family is
chosen once, at construction, and is also what the compile-time validation asks
before applying a rule that only holds for one design: a IIgs is not measured
against the //e-sized arrays it does not use, or against "a visible column
clocks out 14 dots" when its picture is 640 dots wide. See `wiki/Apple-IIgs.md`
for the plan; `Emulator::isMachineRunnable` returns false for the whole family
until its parts exist, whatever ROMs are in the build.

**The profile is data, not polymorphism.** The parts of a machine that differ
between a //e, a II+ and a IIgs are overwhelmingly numbers — a clock rate, a
scanline count, how much RAM answers, which CPU is fitted, whether the video
generator inhibits colour burst in text mode. Those live in a struct that the
subsystems read. They are deliberately not virtual methods: `MMU::read`, the
video emitters and the CPU dispatch loop are the hottest code in the emulator,
and an indirect call on a per-cycle or per-dot path would cost real speed to
serve a machine count of one. Anything a future machine cannot express as data
— a 65816's 24-bit bus, the IIgs shadowing map, Super Hi-Res — wants its own
subsystem class chosen once at construction, not a branch taken sixty million
times a second. The rule is: **a number or a flag goes in the profile; a
different mechanism goes in a different class that the profile names.**

The profile is threaded by construction, not by lookup. `Emulator(MachineId)`
selects it and hands it to `MMU` and `Audio`; `Video` takes it *from the MMU*
rather than as a second argument, because the video scanner and the floating
bus are the same counters read two ways and a pair that disagreed would be a
bug with no way to express it. Cards receive it through
`ExpansionCard::setMachine()`, called by `MMU::insertCard`. Most cards ignore
it — a Disk II does not care what is at the other end of the bus — but the
mouse card raises its interrupt at the start of vertical blank, and where
vertical blank falls belongs to the machine.

**The constants in `types.hpp` did not go away, and that is deliberate.**
`MAIN_RAM_SIZE`, `FRAMEBUFFER_SIZE` and the rest size `std::array` members at
compile time, which a runtime profile lookup cannot do. They remain as the
//e's values, and `machine_profile.hpp` `static_assert`s every one of them
against the profile, so the two descriptions cannot drift: change one and the
build fails. Further assertions pin the relationships rather than the numbers —
a scanline is its blanking plus one cycle per visible column, a visible column
clocks out 14 dots, the framebuffer is every visible line doubled.

**Every profile is validated at compile time.** `profileIsSelfConsistent()`
checks that a profile describes a machine that could exist — a scanline is its
blanking plus one cycle per visible column, a column clocks out 14 dots, the
framebuffer is every visible line doubled, a machine with no auxiliary bank
does not claim auxiliary RAM, double hi-res does not exist without 80 columns,
the ROM reaches the top of the address space, and nothing is fitted to a slot
the machine does not have. `profileFitsCompiledStorage()` checks it against the
arrays the build actually allocates, which are sized for the //e and are
therefore the ceiling for every machine. `allProfilesValid()` runs both over
the registry in a `static_assert`, so a broken profile does not compile.

**Save states carry the machine id, and every machine writes the same header.**
Twelve bytes — magic, format version, machine id — begin a state whichever
machine wrote it. Everything after that point is laid out to the saving
machine's shape, so a state restored into a different machine would be read as
garbage rather than fail; the id is what lets `importState` refuse it. The
Apple II family's layout is `STATE_VERSION` 9 in `emulator_state.cpp`; a IIgs's
is its own (`iigs_state.cpp`, version 1), because the two share nothing after
the header and have no reason to move together. The host reads the header
itself (`src/js/state/state-header.js`) and, asked to load a state saved on
another machine, switches to that machine first rather than let the core
refuse — a save is a save of a whole machine, and loading one is asking for it
back. The autosave is kept per machine for the same reason (see State
Serialization).

**The host asks rather than assumes.** `src/js/machine/machine-profile.js`
fetches the whole profile as one JSON string through `_getMachineProfileJSON`
(one round trip — the Worker services RPCs on the thread that runs the
emulation) and `main.js` does it immediately after the WASM module is up,
before anything sizes itself to the picture. The WebGL renderer, the
text-selection overlay, the screenshot path, the save-state preview, the
printer's screen dump and the agent's `captureScreenshot` all read
`machineDisplay()` instead of the 560x384 they each used to hardcode. A fetch
failure is not fatal: the module falls back to the //e, which is a correct
description of the only machine that exists.

The one place that still fixes a size is the shared framebuffer slot
(`FB_WIDTH`/`FB_HEIGHT` in `worker/shared-buffers.js`). A `SharedArrayBuffer`
cannot be resized once handed to the Worker and the AudioWorklet, so the slot
is allocated up front and must hold any machine's frame. `setupSharedBuffers()`
checks the fit and falls back to the `postMessage` transport rather than let a
frame write past the end of the slot.

#### The Apple II Plus

The second profile, and the one that proves the seam carries. Its video timing
is the same circuit, so every number in `MachineTiming` is identical to the
//e's and the differences fall entirely in what the machine *has*. Four of them
matter, because each exercises a different part of the mechanism:

- **An NMOS 6502 rather than a 65C02.** The CPU core already modelled both
  variants; the profile is what selects one.
- **No auxiliary bank.** `$C000-$C00F` are the //e's memory and display
  management switches — 80STORE, RAMRD/RAMWRT, INTCXROM, ALTZP, SLOTC3ROM,
  80COL, ALTCHARSET — and on a II+ that range manages no memory at all.
  `writeSoftSwitch` ignores the whole group when the machine has no auxiliary
  bank, and **that single guard is what makes every 80-column and
  double-resolution path unreachable**: `Video` selects those modes from the
  80COL switch, which can now never be set. No second guard in the video code
  is needed or wanted.
- **12KB of ROM at `$D000` rather than 16KB at `$C000`,** since nothing on a
  II+ motherboard answers at `$C100-$CFFF`. `MMU::loadROM` places a machine's
  image at the offset its `romBaseAddress` implies within the `$C000-$FFFF`
  window, so the read path — which indexes `address - ROM_WINDOW_BASE` — needs
  no knowledge of where a given machine's ROM begins.
- **It never inhibits colour burst.** A //e kills the burst on text lines and
  so shows crisp white text; a II+ sends a reference on every line and its text
  fringes green and violet in every mode. `Video::burstForScanline()` reads
  `caps.inhibitsBurstInText`, so this follows from the profile alone.

**Its character generator stores glyphs the other way round.** A //e's 8KB ROM
puts bit 0 at the left of a glyph row and leaves the blank scanline at the end
of each eight-byte cell; a II+'s 2KB ROM puts bit 6 at the left and the blank
scanline first. Neither is more correct — it is how the part was wired to the
video shift register — but the renderer reads one layout, so `MMU::loadROM`
rewrites the image into it (`normaliseCharROM`, driven by `MachineCharRom` in
the profile). This is done once at load rather than per dot, because it is a
property of the ROM image and the dot loop is the hottest code in the video
path. Get it wrong and every character on screen is drawn mirrored, which is
exactly what the II+ did before this existed.

**It has only one character set, and asking for a second blanks the screen.**
The UK set is a second bank inside the //e's larger ROM, reached by adding
0x1000 to the glyph offset. A II+ has nothing there, so every glyph reads back
blank and the display shows nothing but the cursor — which survives because it
is the inverse of a blank and so still solid. `caps.hasUkCharSet` gates the
offset, and the host hides the toggle on a machine that has no second set.

**The II+ ROMs are optional and are not in the repository.** A II+ motherboard
carries six 2KB ROMs in sockets D0 to F8 covering `$D000-$FFFF`: five of
Applesoft and the Autostart monitor at `$F800`. `scripts/generate_roms.sh`
concatenates them in address order, or accepts a single pre-combined
`apple2plus.rom`, and emits empty arrays when they are absent. **A machine can
therefore be fully described and still be unable to start.** `Emulator::init()`
records this in `hasSystemROM()` rather than silently running a //e's ROM or
none at all, `Emulator::isMachineRunnable()` answers the same question about a
machine that is not running, and the host's `listMachineProfiles()` puts a
`runnable` flag on every entry so a chooser does not offer a machine that will
never reach a prompt.

**Switching machines rebuilds the emulator.** There is no way to convert a
running machine into a different one — the RAM, the cards and the save state
are all shaped to the machine that made them — so `_setMachine` destroys the
global emulator and constructs the new one. Inserted media and host state do
not survive, exactly as they would not across a page reload, and the caller is
responsible for putting them back.

**Slot 0 exists on a II+.** `slots_` is indexed by slot number with room for
eight, and `MMU::insertCard` asks the profile rather than assuming 1-7. What
goes in slot 0 on a real II+ is the 16K language card, which is how a 48K
machine becomes the 64K one nearly all II+ software expects; the profile fits
one as a *fixed* card, because the bank switching at `$C080-$C08F` is the same
hardware the //e carries on its motherboard, is implemented by the MMU, and is
not something the user could pull out.

The Expansion Slots window follows all of this. `SlotConfigurationWindow`
builds its slot list from the profile — which slots exist, and which carry a
card the user cannot change — rather than from a fixed //e table, and
`setMachine()` rebuilds it after a switch. A //e shows slots 1-7 with the
80-column card locked into slot 3; a II+ shows 0-7 with the language card
locked into slot 0 and slot 3 free for anything. What each free slot *offers*
stays host presentation (`SLOT_UI`), since that is convention rather than
machine fact.

**What a machine ships with is in its profile, not in the constructor.**
`Emulator`'s constructor used to fit a Mockingboard in slot 4 and a Disk II in
slot 6 whatever the machine was, and `getSlotCardName` reported an 80-column
card in slot 3 whatever the machine was. Both put hardware in a II+ that it
never had, and both leaked into the saved slot layout. Defaults now come from
`slots[].defaultCard` and a fixed slot reports `slots[].fixedCard`. A card the
machine does not ship is *parked* in `diskStorage_`/`mbStorage_` rather than
dropped, because `disk_` and `mockingboard_` still point at it and
`setSlotCard()` fits it later from exactly those members.

**Slot layouts are remembered per machine.** `src/js/machine/slot-storage.js`
keys them by machine (`a2e-slot-config:apple2e`), because the machines do not
agree about what a slot is: one shared layout put a II+'s slot 3 card into a
//e's built-in 80-column slot, and followed a //e's SmartPort onto a machine
whose defaults are a bare Disk II. A machine with nothing saved falls back to
its profile's defaults rather than to a shared constant, and an emptied machine
stays empty — "never configured" and "deliberately stripped" are different
states. The single pre-machine key is read once as the //e's starting point,
copied under the //e's own key, and then left alone; an orphan costs nothing,
and losing somebody's layout to a mistake in that copy would cost more.

#### The Apple //c

A //e folded into a slab: the same 65C02, the same 128K, the same IOU and MMU,
so every number in timing, memory and display is the //e's and almost every
capability is too. What differs is the back of the machine.

**It has no expansion sockets, but it decodes all seven slot addresses.** The
firmware and everything written for a //e depend on those addresses, so each
one answers to a part soldered to the board: two 6551 serial ports in slots 1
and 2, the 80-column firmware in slot 3, the mouse in slot 4, and the disk port
in slot 6. Every slot is therefore a *fixed* slot — the part of `MachineSlot` a
//e exercises only in slot 3 — and `caps.hasExpansionSlots` is false, which is
what stops the slot window offering a card to a machine that has nowhere to
take one.

**That capability is also a memory rule.** With no socket there is nowhere for
a card's ROM to live, so the firmware for all of it is inside the 16KB system
ROM and `$C100-$CFFF` reads the internal ROM whatever INTCXROM and SLOTC3ROM
say: those switches choose between the internal ROM and a slot that does not
exist. `MMU::read` and `MMU::peek` take that branch first, before the switches.
Without it the region reads zeroes, the reset lands on a `BRK`, and the vector
sends it to another one — a //c wedged at `$C803` before drawing anything.

**Two smaller differences are modelled.** 4KB of character generator rather
than 8KB, so there is no second set to ask for; and a disk that is not a Disk
II — the drive hangs off an IWM at `$C0E0`, so slot 6 names `"iwm"` rather than
the card a //e fits there.

**Its disk is an IWM, and the sequencer under it is the card's.** The chip
decodes the same sixteen addresses at `$C0E0-$C0EF` and means the same things
by them, so what is below it — the drives, the stepper, the motor, the LSS — is
`DiskController`, shared with `Disk2Card`; `IWM` adds the register file a read
sees in front of it (data, status, handshake, and a mode register writable only
with the motor off) and has no ROM, because a //c's disk firmware is in the
system ROM rather than in slot 6's 256 bytes. Which class gets built is the
profile's `slots[6].fixedCard`, and `Emulator::getDisk()` hands out the base,
so nothing the host asks about a drive had to change. A //c boots DOS 3.3 from
the drive in its case.

**Its serial ports are the SSC's ACIA with no card around it.** Slots 1 and 2
each hold a `SerialPort` composing an `ACIA6551` at the slot's offsets 8-B —
`$C098-$C09B` and `$C0A8-$C0AB`, the same four addresses an SSC answers — so
`PR#1` and `IN#2` work off the machine's own firmware. There is deliberately no
base class shared with `SSCCard`: what the two have in common *is* the ACIA and
they already share it by composing it, the way the hardware does. What is left
over is a card's DIP switches and 2KB ROM against a port's nothing, and a base
class holding four forwarding methods would describe a part that does not
exist. This is the other half of the IWM's rule — share a mechanism, not a
resemblance.

The host's serial calls (`setSerialTxCallback`, `serialReceive`) serve every
machine that has a serial line, a IIgs included, because the question is about
the line rather than about what provides it: transmit goes to every port there
is, and a byte arriving from outside goes to port 2, the modem port, since a
printer does not talk back.

**Its mouse is the IOU, and is the one part that is not a card at all.** A //e's
mouse is an MC6821 in a slot with a ROM and a command protocol; a //c's is two
quadrature lines into the IOU, so `MouseIOU` (`input/mouse_iou.cpp`) is owned by
the `Emulator` and hooked into `MMU::readSoftSwitch`/`writeSoftSwitch` by a
pointer that is null on every other machine — which is what keeps a //e's
`$C015`, `$C063` and `$C066` exactly as they were. The profile names "mouse" in
slot 4 only because that is where a //c's mouse *firmware* lives.

Two things about it are load-bearing:

- **Travel is interrupts, not a delta.** The IOU counts nothing. Each unit of
  movement toggles X0, the edge raises an IRQ, and the firmware's handler reads
  X1 for the direction and adds one to a position in slot 4's screen holes. So
  host movement is banked and released one step at a time, and never onto a
  flag the handler has not cleared — releasing them faster loses the ones in
  between, which looks like a mouse that moves part of the way and sticks.
- **`$C015` and `$C017` report; only `$C048` clears.** Table 9-2 of the //c
  Technical Reference calls them RstXInt and RstYInt and says a read resets
  them, and the machine's own handler proves otherwise: it reads `$C015` and
  ORs `$C017` to see whether either fired, BITs each again to see which, then
  writes `$C048` when it is done. A read that cleared would send every X
  movement down the Y path. The firmware is the authority, not the table.

#### Choosing a machine

**The header badge names the machine and is how it is changed.** It used to be
the right-hand half of `apple-logo.png`, so the header announced "//e" whatever
was running; the logo is now `applem-logo.png` (the wordmark alone) and the
badge is a live control. `MachineMenu` (`src/js/machine/machine-menu.js`) keeps
it in step and hangs an ordinary `.header-menu-container` dropdown off it, so
it inherits the app's open/close, click-outside and Escape handling rather than
inventing its own. Each entry draws the machine, names it, summarises its CPU,
memory and columns, ticks the one in use and marks any whose ROMs are missing.

The badge wears `profile.logotype`, not `shortName`. Apple's own marks are not
always what you would write in a sentence: a II Plus is badged `][`, and
rendering "II+" in the badge's heavy oblique face produces "//+", which is not
a designation Apple ever used. `shortName` stays for prose.

The window title follows the machine too, so a browser's window switcher shows
which machine a tab is running.

Switching is destructive and the menu says so before doing it: the core
rebuilds the emulator, so inserted media and anything in memory are lost, just
as they would be on a reload. What is *not* lost is the user's preferences —
`AppleIIeEmulator.onMachineChanged()` pushes the display settings, volume,
character set and clock speed back into the new core, because those were the
user's choices rather than machine state. The chosen machine is remembered in
localStorage under `a2e-machine` and restored at startup, before the renderer
and windows are built, so they are made for the right machine rather than
rebuilt for it a moment later. A remembered machine the build cannot run is
ignored rather than honoured.

#### Menus follow the machine

`src/js/ui/machine-availability.js` says which menu items the running machine
can use, from its profile and the cards fitted, and
`UIController.applyMachineMenus()` hides the rest — at startup, after a switch,
and whenever the Expansion Slots window applies a change. Hidden rather than
disabled: a greyed "Expansion Slots" on a //c invites the question of how to
enable it, and the answer is a different computer. What goes: Expansion Slots
on a //c (no sockets) and on a IIgs (its core does not answer `_setSlotCard`;
its slots' "card or port" choice is not modelled); CPU Speed on a IIgs (the
multiplier is `Emulator`'s); SmartPort Drives, Serial Port and Printer unless
something provides them (a IIgs's slot 5 and its two sockets, a //c's ports, or
a card); the Mockingboard and Mouse Card debug windows unless the card is
fitted — a //c's "mouse" is the IOU, which has no PIA to show. A separator left
with nothing after it goes too. `tests/js/ui/machine-availability.test.js` pins
the table.

#### Adding a machine

A new `MachineId` and profile entry, a subsystem class for anything that is a
different mechanism rather than a different number, and its ROMs. Nothing in
the host needs to know.

### Paste / Typed Text

Pasted text (Ctrl+V, the BASIC and assembler windows, and the agent's
`typeKeyboard`) goes into a **type-ahead buffer inside the core**, not a queue
in JavaScript. `Emulator::pasteText()` translates the text with the one
`charToAppleKey` in `keyboard.cpp` and pushes it onto `pasteBuffer_`;
`loadNextPasteKey()` moves a character into the keyboard latch, and
`clearKeyboardStrobe()` schedules the next one once the program has read the
strobe. The machine therefore pulls characters at its own pace — a character
can never be overwritten before it is read — subject to the minimum gaps
below.

The host therefore does not meter the paste at all. `input-handler.js` writes
the text across in whole runs — a paste of any size costs a fixed handful of
RPCs, where the old path spent one `_charToAppleKey`, one `_isKeyboardReady`
and one `_runCycles` **per character** and drove the CPU in 500-cycle bursts
from the main thread, competing with the audio-paced worker for the same
emulation. What is left in JS is bookkeeping: the 8x speed boost, dropped and
restored around pauses, and a `_pastePending()` poll to notice the end and fire
the completion callback. `{token}` sequences (`{ctrl-c}`, `{left}`, `{chr:4}`)
are still resolved host-side and pushed as single codes with `_pasteKey`.

Two details are load-bearing. `loadNextPasteKey()` refuses to touch the latch
while the strobe is set, which is what makes the buffer lossless. And it
refuses to refill the latch *immediately*: a character becomes available
`PASTE_KEY_GAP_CYCLES` (~15ms of emulated time) after the previous one was
taken, and `PASTE_LINE_GAP_CYCLES` (~150ms) after a carriage return.

The gap is not cosmetic. Clearing the strobe twice is a keyboard **flush** —
`POKE -16368,0`, `STA $C010`, the ROM and DOS doing it before settling down to
wait for input — and it is everywhere in Apple II software. A person typing
leaves nothing pending for a flush to eat; a buffer that refilled the latch the
instant the strobe cleared handed each flush a fresh character to throw away.
The symptom is a paste that arrives with characters missing, in some programs
and not others: `10 GET A$: POKE -16368,0: PRINT A$;: GOTO 20` received
`ACEGIKMOQSUWY02468` from `ABCDEFGH…9`, exactly every second character.
`test_emulator.cpp` pins that case. The carriage-return gap is longer because
the machine has a line to digest afterwards — Applesoft tokenises it, DOS and
BASIC.SYSTEM run their command parsers — with flushes at the end of that work.

A read of `$C000` that finds the keyboard empty looks like a better signal than
a timer — the program asking for input — and it was tried and measured to fail:
Applesoft polls `$C000` between every statement to check for Ctrl-C, so it
releases the next character long before the program reaches its flush. Do not
reintroduce it. Because the gaps are emulated time, the 8x paste speed boost
shortens the wall-clock wait proportionally; a 2400-character program pastes in
around seven seconds.

The buffer is host state like the speed multiplier — it is not serialized into save states,
and `reset()` discards it. Mobile input goes through the same buffer, because
an on-screen keyboard can deliver several characters in one `input` event and
writing the latch per character overwrote keys the machine had not read yet.

### AKD (any key down)

`$C010` bit 7 says a key is *physically held*, as opposed to `$C000` bit 7,
which says a key code is waiting to be read. `Emulator::updateAnyKeyDown()`
**derives** it from the only two things that can hold a key down — a host key
currently held (`Keyboard::isAnyKeyDown()`) and a pasted character still
sitting unread in the latch (`pasteHoldsKey_`) — rather than any path setting
`keyDown_` directly. That is deliberate: every earlier version asserted AKD
somewhere with no matching release, so the line went high on the first
keystroke of the session and stayed there, and key-repeat or game input code
polling it saw a key held forever.

`Keyboard` tracks held keys by *browser keycode*, with a running count, so a
key-up always clears the key it names whatever the modifier state was when it
was pressed, and auto-repeat's stream of key-downs cannot double-count.
Shift, Control, Caps Lock and the Apple buttons deliberately do not assert AKD
— they are separate lines on real hardware, not keys in the matrix. Losing
window focus releases everything, held keys included, because a key held
across a blur never delivers its key-up.

### The Game Port

The game I/O connector takes one device, and which one is a user choice:
`GamePortDevice` in `src/core/input/joyport.hpp` — the Apple resistive
joystick, or Sirius Software's **Joyport**.

The Joyport put two Atari CX40-style digital sticks on the connector. Each has
five switches and the connector has three pushbutton inputs, so it multiplexes:
AN0 selects the stick, AN1 selects the axis pair, and PB0-PB2 report fire, the
first of the pair, and the second.

    AN0   AN1   PB0 ($C061)   PB1 ($C062)   PB2 ($C063)
    off   off   fire 1        left 1        right 1
    off   on    fire 1        up 1          down 1
    on    off   fire 2        left 2        right 2
    on    on    fire 2        up 2          down 2

**The switches are active low, which is why this is a device choice rather than
an addition.** A line reads *high* while nothing is pressed — the opposite of a
pushbutton — so a Joyport cannot share PB0/PB1 with the Open and Closed Apple
keys. `Emulator::getButtonState()` therefore consults the Joyport *instead of*
`buttonState_` when it is selected, and switching devices releases whatever the
old one was holding. It is not a slot card and does not want to be: it hangs
off the 16-pin connector, so the emulator owns it and the pushbutton read path
consults it.

**The Joyport lets go of PB0/PB1 across a reset, and it has to.** The //e's
reset routine reads `$C061` and `$C062` to see whether an Apple key is held —
Open Apple asks for a cold boot, Closed Apple runs the self test. A Joyport
idles both lines *high*, which is exactly what a held key looks like, so a //e
with one fitted ran the self test on every reset and never reached a prompt.
That is faithful: pins 2 and 3 of the game connector really are the Apple keys
on a //e, which is why the Joyport belongs to the II and II+ era. It is also
useless. `joyportResetGuardCycle_` therefore releases those two lines for
`JOYPORT_RESET_GUARD_CYCLES` (~50ms) after a reset — long enough to cover the
ROM's check, far too short for a game to have asked about the stick. PB2 is
never released, because no Apple key is wired to it, and a *closed* switch
still reads low inside the window, so a fire button held through a reset is not
lost.

The device is a host preference like the speed multiplier — `reset()` clears
the sticks but keeps the device, and neither is written into a save state. The
core starts every machine on an Apple joystick, so `main.js` pushes the
remembered choice back in after startup and again in `onMachineChanged()`.

Host-side, `src/js/input/game-port.js` owns the selection, its storage and the
mapping from a browser gamepad to five switches (unit-tested in
`tests/js/input/game-port.test.js`); `JoystickWindow` shows one panel per
device and `GamepadHandler` now tracks *every* connected pad rather than the
first, because the Joyport takes two. A single pad drives both sticks — a
one-player game that happens to read stick 2 then still plays, which is worth
more than a dead second stick. An opposing pair is dropped rather than sent:
a real gate cannot close left and right at once, and a program that saw both
would take whichever it tested first.

`test_joyport.cpp` pins the table above and `test_emulator.cpp` pins it through
the machine's own read path.

### CPU Speed

**View > CPU Speed** picks 1x/2x/4x/8x of the 1.023 MHz clock. The mechanism
was already in the core: `generateStereoAudioSamples()` runs
`samples * CYCLES_PER_SAMPLE * speedMultiplier_` cycles for a fixed number of
samples, so audio keeps pacing the machine and the picture keeps arriving at
60fps — the emulator just covers more emulated time per frame, and the speaker
rises in pitch along with it, like an accelerator card.

**Both sound sources measure their rate in CPU cycles, so both must be told the
speed.** `Emulator::applySpeedToAudio()` pushes it to each on every change and
on card insertion. `Audio::generateStereoSamples()` clamps a buffer whose cycle
span looks implausible, and that "expected" span scales with the multiplier —
left at 1x it discarded all but the last eighth of an 8x buffer and replayed the
remainder at real-time pitch, which is heard as sound that cuts out or refuses
to speed up. `MockingboardCard` emits one frame per `cyclesPerOutputSample_`,
also scaled — otherwise it produced eight frames for every one the mixer
consumed and the backlog grew without bound, heard as normal-pitch music
falling further and further behind. `test_audio.cpp` and `test_mockingboard.cpp`
pin both.

`src/js/ui/emulation-speed.js` owns the selection (localStorage, unit-tested in
`tests/js/ui/emulation-speed.test.js`); `UIController.setupSpeedSelector()`
wires the menu and `ScreenWindow.setSpeedState()` shows a title-bar chip above
1x.

Two things are deliberate. `Emulator::reset()` does **not** clear
`speedMultiplier_` — it is a host preference (or a paste boost in flight), not
machine state, so a reboot must not drop the user back to 1 MHz;
`test_emulator.cpp` pins this. And the selector writes through
`InputHandler.setBaseSpeed()`, which records the new baseline instead of
touching WASM while a paste boost is live — otherwise `restorePasteSpeed()`
would put the old speed back when the paste ended. The multiplier is not part
of a save state.

### Assembler

`src/core/assembler/` is a Merlin-compatible 65C02 assembler, not a generic
one. Four things in it are load-bearing.

**There is no "pass 1 sizes, pass 2 emits" split.** `assemble()` runs the whole
source to completion repeatedly until the symbol table and the object size stop
moving, then once more with diagnostics on. Macros, conditionals and `LUP`
blocks make the *line stream itself* depend on symbol values, so a sizing pass
that did not emit could not have followed the same path as the pass that did. A
forward reference reads its value from `lastPassSymbols_` — the previous pass's
table — and sets `unresolved_`, which forces absolute addressing so an
instruction never shrinks to zero page on a guess and oscillates.

**Expressions run strictly left to right with no operator precedence.** `1+2*3`
is 9. That is Merlin, and real Merlin sources are written assuming it, so adding
precedence would silently change the bytes they produce. The operators are
`+ - * /` plus `.` (or), `!` (exclusive or) and `&` (and); `<`, `>` and `^`
select the low, high and bank byte of the *whole* expression, which is why the
selector is parsed once in `evaluate()` and applied to the total rather than
being a term-level unary. Outside an immediate, a leading `<` or `|` forces the
address width instead.

**A line is four whitespace-separated fields and the comment needs no
semicolon.** `parseLine` ends the operand at the first space outside a string —
that is why a Merlin operand never contains a space, and why the editor's
validator has no "unexpected extra token" rule. String directives are the
exception: their operand opens with a delimiter of the author's choosing, so it
is scanned to the matching character first. `STRING_DIRECTIVES` in
`merlin-highlighting.js` holds the same list for the editor.

**The editor no longer assembles anything itself.** `AsmLineInfo` reports each
main-source line's address, cycle count and bytes, and
`assembler-editor-window.js` reads that block through one `heapRead`. It used to
carry its own 65C02 opcode table, operand parser and expression evaluator to
fill the gutter; those could not survive macros or conditional assembly, and a
second encoder is a second thing to be wrong. A macro call site is credited with
the bytes its expansion produced (`expandAndRecord`), because the body's lines
are not in the source being edited. Cycle counts come from `CYCLE_TABLE`, per
opcode rather than per mnemonic, so `LDA $10` and `LDA $1000` differ correctly.

`PUT` and `USE` resolve through an `AsmIncludeProvider` callback so the core
stays host-free; `wasm_interface.cpp` installs one that reads text files off the
disk in drive 1 then drive 2, on DOS 3.3 or ProDOS, honouring Merlin's `T.`
prefix convention. Multiple `ORG`s produce multiple `AsmSegment`s, and
`loadAsmIntoMemory` places each where it belongs instead of assuming one block.
`REL`/`ENT`/`EXT`/`LNK` need a linker and are reported as errors; `KBD` and a
second `XC` are reported as *warnings*, a severity `AsmError::warning` carries
so an assembly still succeeds.

### Theming

Light, dark, and system-follow themes controlled by `ThemeManager` (`src/js/ui/theme-manager.js`). Sets `data-theme` attribute on `<html>` for CSS variable switching. All accent and syntax highlighting colours are derived from the six-stripe Apple rainbow logo palette (Green `#61BB46`, Yellow `#FDB827`, Orange `#F5821F`, Red `#E03A3E`, Purple `#963D97`, Blue `#009DDC`), with brightness adjusted per theme for contrast. Speaker, Mockingboard, and disk drive sound volumes are all wired to a single main volume slider with a unified mute toggle.

Control sytles, sizes and layout must be consistent across the entire app.

**Window surfaces are opaque and carry no `backdrop-filter`.** Use the `--glass-bg`, `--glass-bg-solid` and `--glass-bg-header` tokens for any window, panel, menu or popout background; they are fully opaque in both themes despite the legacy names. Do not reintroduce translucency or blur on these surfaces: they sit over a canvas that repaints 60 times a second, so a backdrop filter forces the compositor to re-blur the full area of every open window on every frame regardless of whether its content changed. Translucency is still correct for two things — dimming scrims behind modals and the window switcher, and accent-tinted inner chips (CPU flags, soft switch badges) layered on an already-opaque window.

### Display / CRT Shader

`public/shaders/crt.glsl` is the whole picture pipeline. Three things in it are load-bearing and must not be undone:

**Animated effects are bounded by the photosensitive-epilepsy limits.** No full-screen luminance modulation may exceed three flashes per second or a 10% relative luminance change (WCAG 2.3.1). `flicker()` is a slow two-sine undulation at 3% amplitude for this reason, and the full-screen TV static that used to play while the machine was off was removed outright — it ran at 50Hz with a 12Hz brightness modulation on top. The constraint is documented in the functions themselves; read those comments before touching them.

**The mask is in physical screen space, the beam is not.** `shadowMask()` derives position from `gl_FragCoord` divided by `u_pixelRatio` — a mask has a fixed pitch in millimetres, so it must neither resize with display density nor move when jitter and horizontal sync displace the picture. Effects that model the *signal* take the distorted UV; effects that model the *glass* do not.

**Phosphor persistence is exponential and per-phosphor.** `burnin.glsl` decays each channel by `exp(-dt/tau)` against real elapsed time, not a per-frame subtraction — the pass is throttled to every fourth frame, so a per-frame decay tied persistence to frame rate. In colour mode green holds longest and blue fades quickest, which tints a moving trail green; in monochrome modes one phosphor means one rate for all channels, held longer.

**Scanlines model a beam spot, not a stripe pattern.** `scanlines()` takes the displayed luminance and widens its Gaussian with it, because a CRT beam grows with current. The framebuffer is 560x384 (280x192 doubled), so a scanline pitch is two texel rows — 192 lines.

The powered-off screen is built by `src/js/display/no-signal-frame.js` as an ordinary 560x384 RGBA framebuffer and uploaded as the source texture, so it passes through the whole CRT chain like emulator video. `WebGLRenderer.updateTexture()` ignores emulator frames while it is displayed.

`WebGLRenderer.draw()` re-derives the drawing buffer size when `devicePixelRatio` changes, because a density change alters no CSS size and so never reaches the `ResizeObserver` that drives `resize()`.

### Composite Video / Colour Decoding

The //e emits no pixels. It emits one bit per 14.31818 MHz dot, four dots to a
cycle of the 3.579545 MHz colour subcarrier, and every colour is manufactured by
the *receiver*. `video.cpp` therefore renders in two stages: the mode emitters
(`emitText40Scanline`, `emitHiResScanline`, …) write a 1-bit dot stream into
`dots_`, and `endScanline()` hands the finished line to one of four decoders in
`ntsc.cpp`. A visible line is 560 dots and the framebuffer is 560 wide, so the
signal maps 1:1 onto pixels and nothing is ever resampled.

Four things here are load-bearing:

**The luma filter must null both 3.58 MHz and 7.16 MHz exactly.** A flat LORES
colour *is* a subcarrier-frequency pattern, so subcarrier leaking into luma makes
greys ripple; leakage at the second harmonic makes a colour's brightness depend
on which subcarrier phase a dot lands on, which shows up as faint banding. A
four-tap boxcar, integrating exactly one colour cycle, annihilates both, and it
is now the whole filter. Do not replace it with a plain low pass, and do not
cascade anything on top of it without a reason: it used to be followed by an
11-tap 4 MHz windowed sinc, and that cascade was where a composite picture's
extra softness came from. The two agree to within a decibel below 3 MHz, so the
sinc bought nothing where the shape of a character lives; what it did was take
13 to 30 dB out of the 4 to 6 MHz band, which carries the edges. The boxcar
alone is also the more faithful model, because a period set's luma path was a
trap at the subcarrier rather than a brick wall at 4 MHz, and nothing above
7.16 MHz exists in the input to leak back in. `hannSinc` is kept, unused and
marked so, because it is the right tool if a future change does need shaping.

**The calibration constants in `ntsc.hpp` were fitted, not chosen.**
`BURST_PHASE`, `CHROMA_GAIN` and `LUMA_GAMMA` come from a least-squares fit
against the physically self-consistent entries of the Apple II palette: black,
white, both greys, and the four single-bit LORES hues, whose phases a real
decoder fixes exactly 90 apart. The multi-bit palette entries were excluded
because they had been hand-tuned and sit 17-24 degrees off. Two independent fits
agreed on the phase to a quarter of a degree.

**The HIRES high bit is a one-dot delay, not a palette swap.** It pushes the
byte's seven pixels half a HIRES pixel right, landing them on the opposite
subcarrier phase; that *is* the mechanism behind orange and blue. The vacated dot
holds the shift register's previous output rather than going blank.

**Double-resolution modes are one dot later than 40-column ones.** The
80-column shift path is clocked a dot behind, so the same pattern comes out a
quarter turn round the colour wheel (`DOUBLE_RES_DELAY` in `video.cpp`). This is
the fact the old `DLGR_COLORS` table encoded as a copy of `LORES_COLORS` with the
nibble rotated left by one. Get it wrong and every DHGR picture is hue-rotated
by 90 degrees — subtle enough to look plausible, so `test_video.cpp` pins it.

**Colour burst is per scanline, but the colour killer is per field.**
`burstForScanline()` models the machine: a //e inhibits burst in text mode,
including the bottom four rows of a mixed screen. `chromaEnabled_` models the
monitor, and it is the flag the decoders actually receive. A real colour killer
integrates burst presence over a time constant far longer than one line, and the
3.58 MHz reference flywheels through gaps, so chroma is switched on or off a
whole field at a time.

Both halves are needed to get the two observable behaviours right. Full text mode
sends no burst at all, the killer engages, and text is crisp white. Mixed mode
sends burst on 160 of 192 lines, so the killer never engages and the four text
rows at the bottom **fringe green and violet along with everything else** — which
is what real hardware does, and what a per-line gate would wrongly suppress. The
same mechanism explains why a II+, which never inhibits burst, fringes its text
in every mode. It also means 80-column text on the Composite preset is genuinely
mushy, exactly as it was on real hardware.

`VideoColorMode` (types.hpp) selects the decoder: MONOCHROME (dots straight to
one phosphor), PIXEL_EXACT and RGB_MONITOR (idealised, see below) and COMPOSITE
(full demodulation). The composite decoder is a 512 KB lookup table indexed by a
15-dot window and the subcarrier phase — an exact memoisation of the FIR, not an
approximation, which `test_ntsc.cpp` verifies over all 131072 entries.

**The sharp modes apply no composite effects whatsoever, and that is a
requirement rather than a nicety.** `decodeIdeal()` is not a demodulator and must
never become one: an unlit dot is black, and colour never extends past the pixels
that are actually lit. An earlier version slid a four-dot window over every dot,
which painted colour onto dots that were off and past the edges of white blocks —
a composite artifact in a mode whose whole purpose is not having any.

Because the signal alone cannot say what a pattern means — a flat LORES cell and
a lit HIRES pixel can carry identical dots — each emitter tags its dots with an
`ntsc::IdealKind`. The two kinds name mechanisms rather than modes, because the
split does not fall along mode lines:

- `CELL` — one flat colour across the aligned four-dot group. LORES, DLORES and
  DHGR, whose dots encode an actual colour value.
- `DOT_GATED` — unlit dots are black, lit ones take the artifact colour their run
  implies. HIRES *and text*, whose dots are drawn shapes rather than an encoded
  colour, and which on real hardware pick up artifact colour the same way.

`DOT_GATED` decides between an artifact colour and white by **run length**, not
byte alignment: a run of two lit dots is one isolated pixel (or one text stroke)
and takes a colour, three or more reads as white. Run length is used precisely
because it stays correct across the high bit's half-dot shift without needing to
know where byte cells begin.

Text therefore carries NTSC colour in the sharp modes too, whenever the burst is
live — mixed-mode text fringes green and violet exactly as it does under the
demodulator. What the sharp modes do differently is refuse to let that colour
spread: the background stays pure black and the strokes keep hard edges. Full
text mode kills the burst, so an all-text screen is still crisp white.

Display Settings (`src/js/display/display-settings-window.js`) leads with a **Monitor preset** — Pixel Exact, Composite Color, RGB Monitor, Monochrome Green, Monochrome Amber — with every individual slider behind an Advanced disclosure. Each preset also carries a `colorMode`, which selects the core decoder above. Presets set only the picture, never the user's brightness/contrast/saturation, bezel or screen border; editing a setting a preset owns relabels the selection Custom without changing values.

**Display settings are remembered per machine** (`src/js/display/display-storage.js`, unit-tested): a //e's composite look for games has no business on a IIgs's RGB desktop. Each machine has its own localStorage key; the pre-machine key is read once as the //e's. Each machine's defaults differ in one value, the **Screen Border**: 35% on the 8-bit machines, whose picture fills the frame, and 0 on a IIgs, which draws its own border. Saved display profiles stay global — they are named snapshots any machine may pick.

**Display profiles.** Beyond the built-in presets, the user can save the current
picture as a named profile (`src/js/display/display-profiles.js`, unit-tested in
`tests/js/display/display-profiles.test.js`). Profiles live under their own
localStorage key, so Reset to Defaults does not take them with it, and they are
identified by name — saving over a name replaces it.

Two things differ from a built-in preset, both deliberate. A profile captures
*everything*, brightness, contrast, saturation and bezel included: a built-in
imitates a monitor and has no business resetting someone's calibration, but a
profile is a snapshot of a picture the user liked, so restoring it has to give
that picture back whole. And editing a profile keeps it selected and marks it
modified, rather than dropping to Custom the way a built-in does — losing the
name would leave Save nothing to write back to and force a Save As for every
tweak. Save is enabled only while a selected profile is dirty.

There is deliberately no NTSC Fringing slider. One existed when the shader faked
composite artifacts by tinting detected edges; the core now produces real
fringing from the real signal, so a shader knob for it would only double-count.

### URL Media Parameters

`?disk=`, `?disk1=`, `?disk2=`, `?hd=`, `?hd2=`, `?name=` and `?autostart=` let a link open with images already inserted. Two modules:

- `src/js/utils/url-params.js` — pure parsing and URL validation (http/https only; relative paths resolve against the page). Unit-tested in `tests/js/utils/url-params.test.js`.
- `src/js/disk-manager/url-media-loader.js` — fetches (`credentials: "omit"`, size-capped) and inserts.

`main.js` parses the URL *before* `DiskManager.init()` / `HardDriveManager.init()` and populates `urlOwnedDrives` / `urlOwnedDevices`, which those managers use to skip restoring persisted images into units a link is about to claim — otherwise the two loads race.

`?autostart=` (or a bare `?autostart`) powers the machine on at the end of
`init()` with **no interaction at all** — `main.js:autostart()`. It runs
immediately because the Worker paces itself while the `AudioContext` is still
suspended (see Free-Run Clock below); the one thing a browser genuinely
forbids before a gesture is *sound*, so the machine starts silent and the
speaker joins in when the visitor first clicks or types.

It routes through `UIController.powerOn()` rather than the power button's
handler, so the power reminder is *hidden* rather than permanently dismissed (a
machine that started on its own may have started before the visitor ever read
the hint), and the "No disk? Press Ctrl+Reset for BASIC" hint is suppressed
when the URL put a floppy in the drive.

Loads are transient: `DiskManager.loadDiskFromUrlData()` deliberately skips `saveDiskToStorage`/`addToRecentDisks`, and `StateManager.suspendAutoSave()` is called for the session so the periodic autosave cannot persist the URL disk by the back door. The stored autosave preference is untouched.

### Worker Architecture

The WASM emulator runs in a dedicated Web Worker to keep the main thread free:

```
Main Thread                    Worker Thread                AudioWorklet Thread
-----------                    -------------                -------------------
WasmProxy (ES6 Proxy)  ←msg→  emulator-worker.js           audio-worklet.js
  - WebGL renderer               - WASM module                - reads shared ring
  - Debug windows                 - audio generation           - requests refill
  - Input capture                 - framebuffer write            when buffer low
  - Agent tools                   - RPC handler
        ↑                               ↓                            ↑
        └──── SharedArrayBuffer: framebuffer (2 slots) + control ─────┘
                             audio ring buffer
```

- `src/js/worker/wasm-proxy.js` — ES6 Proxy intercepts `_functionName()` calls and sends async RPC to Worker. Fire-and-forget calls (input, control) skip waiting for responses.
- `src/js/worker/emulator-worker.js` — Classic Worker (not module, for `importScripts` compatibility). Loads WASM, handles RPC, generates audio samples on request.
- `src/js/worker/rpc-protocol.js` — Shared message type constants.
- `src/js/worker/shared-buffers.js` — SharedArrayBuffer layouts, allocation and control-block offsets.

Key patterns:
- **Fire-and-forget**: Input/control calls (`_keyDown`, `_setPaused`, `_writeMemory`, etc.) post to Worker without waiting for a response.
- **Batch queries**: `wasmProxy.batch([['_getPC'], ['_getA'], ...])` collapses multiple reads into one round-trip. Prefer ONE batch per window update — the Worker services RPCs on the same thread that runs the emulation, so sequential round-trips directly steal emulation time. `CPUDebuggerWindow.update()` is the reference example: a single 25-call batch, indexed via `UPDATE_BATCH`.
- **String returns**: `wasmProxy.callString(fn, ...args)` calls a `char*`-returning export and decodes it in the Worker, so a string costs one round-trip instead of two.
- **Heap access**: Direct `HEAPU8`/`HEAPF32` access is forbidden from the main thread. Use `wasmProxy.heapRead(ptr, size)`, `heapWrite(ptr, data)`, `heapReadU32()`, `heapReadF32()`, `heapDataViewU32()` instead. These return **typed arrays** and transfer their buffers; never box heap data into plain Arrays.
- **Transferable**: Disk images sent to Worker via `wasmProxy.transfer()` for zero-copy ownership transfer.
- **Pushed pause state**: The Worker posts `MSG_PAUSE_STATE` whenever pause changes, cached on `wasmProxy.isPaused`. Per-frame code reads that synchronously instead of awaiting `_isPaused()`.
- **Bulk work belongs in C++**: A loop that would make one RPC per iteration should become one export. `_disassembleRange` and `_getBasicHeatMapData` exist for this reason.

### Shared Memory Transport

When `SharedArrayBuffer` is available (requires the COOP/COEP headers Vite sets), `main.js:setupSharedBuffers()` allocates three buffers and both the framebuffer and audio bypass `postMessage` entirely:

- **Framebuffer** — double-buffered (`FB_SLOTS`). The Worker writes the slot the renderer is not reading and publishes the index via `CTRL_FRAME_INDEX` + `CTRL_FRAME_READY`; `pollSharedFrame()` claims it with `Atomics.exchange`. This replaced allocating a fresh 860KB array every frame.
- **Audio ring** — the AudioWorklet reads generated samples directly, so the main thread is no longer in the audio critical path. Only the small refill request still routes through it.
- **Control block** — Int32 status fields (see `CTRL_*` in `shared-buffers.js`). Currently only pause and frame state are consumed; the register fields are groundwork for removing debug-window RPCs.

The `postMessage` path remains as a fallback and must keep working — do not delete it.

### Audio-Driven Timing

The emulator uses Web Audio API for precise timing:

1. AudioWorklet `process()` fires at 48kHz hardware rate
2. When the ring buffer runs low, the AudioWorklet requests samples from the main thread
3. Main thread forwards request to Worker via `MSG_REQUEST_SAMPLES`
4. Worker generates samples (running ~21.3 CPU cycles per sample)
5. Worker writes them into the shared audio ring, which the AudioWorklet reads directly

Sample *data* therefore never crosses the main thread; only the refill request does. Without `SharedArrayBuffer` the Worker falls back to posting samples for the main thread to relay, which works but puts a busy main thread in the audio path — and because audio paces the emulation, that shows up as speed instability rather than just crackle.

This ensures consistent speed driven by the audio hardware clock.

### Free-Run Clock

Audio pacing has a hole in it: no browser starts an `AudioContext` before a
user gesture, so on a page nobody has touched there is nothing asking the
Worker for samples, and a machine that has been powered on **sits frozen** —
powered, but not running. That is what `?autostart` originally ran into.

`AudioDriver.start()` therefore turns on a stand-in when it finds the context
suspended (and when audio fails outright): `MSG_SET_FREE_RUN` puts a 16ms
`setInterval` in the Worker which asks for the samples the elapsed real time is
worth. Measured at 1.019 MHz against audio pacing's 1.022 MHz. The generated
audio goes nowhere — the ring drops writes once full, since nothing is reading
— but frames publish exactly as they do under audio.

Three details keep it honest:

- **The Worker stops free-running the instant a real sample request arrives**,
  not only when told to. Two clocks driving one emulation would run it at
  roughly double speed, and this also covers a context that resumes on its own.
- **`stopFreeRun()` empties the ring** by moving the write position to the read
  position (the Worker owns the write side, so no race with the reader).
  Otherwise the AudioWorklet's first sound would be seconds of stale audio.
- **A tick is capped at 100ms of emulated time.** A throttled or backgrounded
  tab returns with a huge elapsed time, and chasing all of it would freeze the
  Worker catching up; the machine loses that time instead, as it does when the
  audio ring runs dry.

### WASM Interface Pattern

Single global `Emulator` instance in C++ (`wasm_interface.cpp`). WASM runs inside a Web Worker; all JS code accesses it via `WasmProxy` which returns Promises. Heap operations use `wasmProxy.heapRead()`/`heapWrite()` instead of direct `HEAPU8` access. `_malloc()` must be awaited; `_free()` is fire-and-forget. `stringToUTF8()`/`UTF8ToString()` are async. New WASM exports must be added to `CMakeLists.txt` EXPORTED_FUNCTIONS list.

### Key Constants (src/core/types.hpp)

- CPU: 1.023 MHz clock
- Audio: 48kHz sample rate
- Screen: 560x384 pixels (280x192 doubled)
- Memory: 64KB main + 64KB aux RAM, 16KB ROM

## Development Workflow

**C++ changes** require rebuilding WASM: `npm run build:wasm`

**JavaScript changes** auto-reload via Vite dev server

**Full build** for production: `npm run build` (outputs to `dist/`)

**ROM files** are embedded into WASM at compile time. Place in `roms/` directory before building:

- `342-0349-B-C0-FF.bin` (16KB system ROM)
- `342-0273-A-US-UK.bin` (4KB character ROM, US/UK)
- `341-0160-A-US-UK.bin` (alternate character ROM variant)
- `341-0027.bin` (256 bytes Disk II ROM)
- `Thunderclock Plus ROM.bin` (2KB Thunderclock card ROM)
- `Apple Mouse Interface Card ROM - 342-0270-C.bin` (2KB Mouse Interface Card ROM)
- `Apple Parallel Interface Card ROM - 341-0057.bin` (512 bytes; upper half is 341-0005 "Parallel Printer" firmware)

**Apple II Plus ROMs are optional.** Without them the II+ profile still exists
and is listed, but reports itself unrunnable (see Machine Profiles). Supply
either the six motherboard ROMs or one pre-combined 12KB image:

- `341-0011.bin`, `341-0012.bin`, `341-0013.bin`, `341-0014.bin`,
  `341-0015.bin` (Applesoft, `$D000-$F7FF`) and `341-0020.bin` (Autostart
  monitor, `$F800-$FFFF`)
- or `apple2plus.rom` (12KB, `$D000-$FFFF`)
- `341-0036.bin` (2KB II+ character generator)

## Code Organization

```
src/
├── core/               # C++ emulator (namespace a2e::)
│   ├── cpu/
│   │   └── 6502/          # Cycle-accurate 65C02 processor
│   ├── mmu/            # Memory management and soft switches
│   ├── video/          # Per-scanline signal generation + NTSC/RGB decoding
│   ├── audio/          # Speaker audio
│   ├── disk-image/     # Disk image formats (DSK/DO/PO/NIB/WOZ), GCR encoding, format conversion
│   ├── disassembler/   # 65C02 disassembler
│   ├── assembler/      # Merlin-compatible 65C02 assembler
│   ├── input/          # Keyboard handling, Sirius Joyport
│   ├── machine/        # Machine profiles (timing, memory, display, capabilities, slots)
│   ├── cards/          # Expansion card system
│   │   ├── disk2/         # Disk II controller card
│   │   ├── mockingboard/  # AY-3-8910 + VIA 6522 + Mockingboard card
│   │   ├── mouse/         # Apple Mouse Interface Card
│   │   ├── parallel/      # Centronics parallel card
│   │   ├── smartport/     # SmartPort hard drive controller
│   │   ├── softcard/      # Microsoft Z-80 SoftCard
│   │   │   └── z80/       # Z80 CPU emulation core
│   │   ├── serial/        # A //c's built-in serial ports (compose ACIA 6551)
│   │   ├── ssc/           # Super Serial Card + ACIA 6551
│   │   └── thunderclock/  # Thunderclock Plus real-time clock
│   ├── filesystem/     # DOS 3.3, ProDOS and Pascal parsers; DOS 3.3/ProDOS file writing
│   ├── basic/          # BASIC tokenizer, detokenizer, Applesoft variable model
│   ├── debug/          # Condition evaluator, host debug log sink
│   ├── emulator/       # Split emulator implementation files
│   │   ├── emulator_state.cpp  # State serialization (exportState/importState)
│   │   └── emulator_debug.cpp  # Debug facilities (breakpoints, watchpoints, trace, beam)
│   ├── noslot_clock.cpp # DS1215 No-Slot Clock (ProDOS RTC at $C300)
│   ├── emulator.cpp    # Core coordinator
│   ├── emulator.hpp    # Emulator class declaration
│   └── types.hpp       # Shared constants and types
├── bindings/           # wasm_interface.cpp - WASM export glue
└── js/                 # ES6 modules, no framework
    ├── main.js         # Entry point, AppleIIeEmulator class
    ├── agent/          # AI agent tools and manager (MCP/AG-UI)
    ├── audio/          # Web Audio API driver and worklet
    ├── config/         # App version
    ├── debug/          # Debug window implementations
    ├── disk-manager/   # Disk drive operations, persistence, surface rendering, sounds
    ├── display/        # WebGL renderer, CRT shaders, display settings, user profiles, no-signal screen
    ├── file-explorer/  # DOS 3.3 and ProDOS file browser, disassembler
    ├── help/           # Documentation and release notes
    ├── input/          # Keyboard input, text selection, joystick, mouse
    ├── machine/        # Host-side machine profile fetched from the core; the IIgs memory size
    ├── state/          # Save state manager and persistence
    ├── ui/             # Menu wiring, reminders, slot configuration
    ├── utils/          # Shared utilities (storage, string, BASIC)
    ├── windows/        # Base window class and window manager
    └── worker/         # Web Worker: WASM proxy, emulator worker, RPC protocol
├── css/                # Stylesheets (bundled by Vite)
public/                 # Static assets, built WASM files, shaders
├── shaders/           # CRT vertex/fragment shaders
├── assets/            # Images and sounds
└── index.html         # Main HTML entry point
tests/
├── unit/               # Catch2 unit tests (CPU, cards, disk, audio, etc.)
├── integration/        # Catch2 integration tests (full emulator)
├── common/             # Shared test helpers (disk image builder, BASIC program builder)
└── catch2/             # Catch2 header-only framework
```

### File Naming Convention

All JavaScript files use **kebab-case** (e.g., `audio-driver.js`, `cpu-debugger-window.js`). Class names remain PascalCase in the code.

## Expansion Card Architecture

The MMU supports pluggable expansion cards matching real Apple IIe hardware. Cards implement the `ExpansionCard` interface (`src/core/cards/expansion_card.hpp`).

### Slot Memory Map

| Slot | I/O Space   | ROM Space   | Default Card                |
| ---- | ----------- | ----------- | --------------------------- |
| 1    | $C090-$C09F | $C100-$C1FF | Empty                       |
| 2    | $C0A0-$C0AF | $C200-$C2FF | Empty                       |
| 3    | $C0B0-$C0BF | $C300-$C3FF | 80-column (built-in, fixed) |
| 4    | $C0C0-$C0CF | $C400-$C4FF | Mockingboard                |
| 5    | $C0D0-$C0DF | $C500-$C5FF | Thunderclock                |
| 6    | $C0E0-$C0EF | $C600-$C6FF | Disk II                     |
| 7    | $C0F0-$C0FF | $C700-$C7FF | SmartPort                   |

### Card Interface Methods

```cpp
class ExpansionCard {
    virtual uint8_t readIO(uint8_t offset);      // I/O space ($C0x0-$C0xF)
    virtual void writeIO(uint8_t offset, uint8_t value);
    virtual uint8_t readROM(uint8_t offset);     // ROM space ($Cx00-$CxFF)
    virtual void writeROM(uint8_t offset, uint8_t value);
    virtual void reset();
    virtual void update(int cycles);
    // ... serialization, IRQ callbacks, etc.
};
```

### Available Cards

- `Disk2Card` (`cards/disk2/`) - Wraps Disk2Controller (slot 6)
- `MockingboardCard` (`cards/mockingboard/`) - Dual AY-3-8910 + VIA 6522, stereo output (slot 4)
- `MouseCard` (`cards/mouse/`) - Apple Mouse Interface Card via MC6821 PIA command protocol (slot 4)
- `ParallelCard` (`cards/parallel/`) - Centronics parallel port; drives Epson FX-80 and Apple DMP virtual printers (slots 1–2)
- `SmartPortCard` (`cards/smartport/`) - SmartPort hard drive controller, 2 block devices, self-built ROM (user-configurable slot)
- `SoftCardZ80` (`cards/softcard/`) - Microsoft Z-80 SoftCard with Z80 CPU emulation (`cards/softcard/z80/`)
- `SSCCard` (`cards/ssc/`) - Super Serial Card with ACIA 6551; drives ImageWriter I and ImageWriter II virtual printers (slots 1–2)
- `SerialPort` (`cards/serial/`) - One of a //c's two built-in ports: the same ACIA 6551, no DIP switches and no ROM (slots 1 and 2, fixed)

**The paper canvas is a window, not the whole job.** A browser caps a canvas
(~32767 px a side, and iOS Safari by total area), and one 8.5x11" page at the
default SS=3 is already ~13.5M backing pixels — about 54MB. `PrinterWindow`
therefore keeps a few pages live (`_liveWindowPages`, asked of the browser via
`canvasFits` and bounded by `MAX_LIVE_BACKING_PX`) and scrolls the paper through
it: `_scrollWindow` writes the departing pages to the page store, shifts the
bitmap up by whole pages and advances `_pagesScrolled`, which `_yToCanvas`
subtracts from every coordinate. Whole pages, because the page-break overlay and
every slice in the snapshot and export paths are page-aligned.

Two consequences are load-bearing. **Ink asks for its row rather than working
it out** (`_reserveRow`): making room can scroll the window, so the canvas y is
only settled after the call — a caller that computed it first drew a page-height
off once a long print started scrolling. And **an export is the job, not the
window**: `_allJobPages()` puts the stored pages before the live ones, which is
what the PDF and the multi-page ZIP use. Page records are numbered from the
start of the job, and the Print Browser counts a job's pages itself rather than
trusting the `pageCount` stamped on a record that was written while the job was
still short.

Before this the height was simply clamped, and every dot past the last page that
fitted was dropped: a four-page print kept one page and silently lost three.

**A GS/OS print is graphics, and the Automatic Line Feed switch nearly ruins
it.** The ImageWriter driver rasterises the page into 8-dot bands and writes
`CR`, `ESC T 16`, `LF` before each — 16/144" is exactly eight dots at the head's
1/72" pitch, so the bands abut. `CItohPrinter` treats `CR`+`LF` as one line
ending when the switch is on (which plain Applesoft text needs), and that
pairing has to survive an escape sequence that prints nothing: the driver's
escape sets the distance for the very `LF` it precedes. With any `ESC` byte
breaking the pairing, every band fed twice and each line of a real GS/OS print
came out sliced in half by a 1/8" white stripe. `_inked()` drops the pairing
whenever a character or a graphics column is laid down, so `CR`, ink, `LF`
still feeds twice. `tests/js/printer/citoh.test.js` pins the band pitch against
a byte stream captured from System 6.0.4 printing through ImageWriter/Printer
v4.2.
- `ThunderclockCard` (`cards/thunderclock/`) - ProDOS-compatible real-time clock (slots 5, 7)
- `NoSlotClock` - DS1215 real-time clock piggybacking on $C300 ROM (not a slot card; toggle in Expansion Slots UI)

## State Serialization

Binary format with a versioned header that every machine shares (magic,
version, machine id — see Machine Profiles). `src/core/emulator/state_stream.hpp`
is the `StateWriter`/`StateReader` pair every machine writes through, so the
rules — little-endian, a blob is its length then its bytes, a read past the end
fails once rather than crashing — are in one place; `drive_state.hpp` is the
two floppies, image and head position, shared by both machines.

**Apple II family** (`emulator_state.cpp`): CPU, 128KB RAM, both language
cards, the soft switches (packed and restored by `MMU::packSwitchesForState` /
`restoreSwitchesFromState`, which writes the switches' own addresses so
everything watching them sees the change), the keyboard latch and buttons,
then **every slot by card id with that card's own state** — so a state refits
the cards it was saved with, through `setSlotCard`, and an SSC, a parallel
card, a SoftCard, a //c's built-in ports and its IWM's mode register all come
back — then the disks, the No-Slot Clock, and a //c's IOU mouse with the steps
it had banked. A card's state is sized in 32 bits because a SmartPort card's
state is its hard drive images.

**IIgs** (`iigs_state.cpp`): the 65816 — mode first, then the flags, then the
registers, because `setEmulation` and `setP` each force the widths the mode
requires — then `IIgsMemory::serialize`: the fast RAM (refused on restore if a
different amount is fitted), the Mega II's RAM, language card and switches,
every register of the memory controller, the slow clock, and the devices,
each with its own `serialize`/`deserialize` — ADB (queues included), the clock
chip (battery RAM, seconds, a transaction in flight), the SCC's two channels,
the Ensoniq (RAM, oscillators, registers; not its output ring, which is the
host's backlog). Then the machine's own counters, the IWM, the floppies and
the SmartPort with its images. `test_iigs_state.cpp` round-trips each part.

**A card's state is written straight into the buffer, and the buffer is
reserved for it.** A SmartPort card's state is its hard drive images, so a
machine with two 32MB volumes writes a state of about 72MB. Serializing each
card into a temporary and copying that in held two further copies of the
payload at once, and the buffer's own growth doubled it again — about 190MB of
heap to write 72MB. That went past `MAXIMUM_MEMORY` and **aborted the module**,
which is worse than it sounds: an aborted module rejects everything asked of it
afterwards, so the symptom was every control in the app going dead rather than
one save failing. `StateWriter::blobFrom` writes the card's bytes in place and
patches the length to what `serialize` actually returned, both `exportState`s
reserve the card sizes up front, and the ceiling is 512MB. The host also
reports a failed save now: every notification in `handleSave` came after the
await, so a rejected save said nothing at all.

Autosave plus 5 manual save slots, stored in browser IndexedDB, each record
naming the machine that wrote it. **The autosave is per machine**
(`autosave:<key>`; the record from before there was more than one machine is
the //e's), because a state restores only into the machine that wrote it and
one shared autosave would come back to nothing for every machine but the last.
The Save States window labels a slot saved on another machine and, on Load,
asks before switching to it. Window option state (toggles, view modes, mute
states) is persisted separately via localStorage.

## Release Process

When the user says "release", perform all of the following steps:

1. **Review git log** since the last release notes entry to identify all changes
2. **Bump version** in `src/js/config/version.js`
3. **Update release notes** in `src/js/help/release-notes.js`
4. **Update README.md** to reflect any new features, changed commands, or updated project information
5. **Update CLAUDE.md** to reflect any architectural changes, new files/directories, new build steps, new expansion cards, new debug windows, or other structural changes to the codebase

## Debugging

Built-in debug windows accessible via Debug menu:

- CPU Debugger: registers (REGS, FLAGS, TIMING, BEAM sections), breakpoints, stepping, disassembly with symbols. The Breakpoints/Watch/Beam panel under the disassembly has a splitter on its top edge and a fold button at the end of its tab bar; its height and whether it is folded live in the window state, and picking a tab on a folded panel opens it
- Memory Browser: hex/ASCII view of 128KB address space with search
- Memory Heat Map: real-time memory access visualization (read/write/combined modes)
- Memory Map: address space layout overview
- Stack Viewer: live stack contents
- Zero Page Watch: monitor zero page locations with predefined and custom watches
- Soft Switch Monitor: Apple II switch states ($C000-$C0FF)
- Mockingboard: unified channel-centric view with AY-3-8910 and VIA registers, inline waveforms, level meters, and per-channel mute controls
- Mouse Card: PIA registers, position, mode, interrupt state, protocol activity
- BASIC Program Viewer: view, load, and tokenize BASIC programs from memory, line heat map, trace toggle, statement-level breakpoints, conditional breakpoints on variables/arrays, condition-only rules, variable inspector, run/stop/pause/step controls
- Rule Builder: complex conditional breakpoints with C-style expressions, supports CPU registers/memory and BASIC variables/arrays as subjects

### Debugging any machine

**Every debug question is asked once, at the widest shape, and a machine
answers as much of it as it has.** A 6502's answer is a 65816's with the high
halves zero and no banks, so addresses are 24 bits throughout the debug layer
and A/X/Y/SP are 16. What a machine does not have — a program bank, a data
bank, a direct page, a second mode — reads as zero rather than as an error,
because "this machine has none" is the answer. The alternative was a second
set of exports and a second set of windows, and two of everything to keep in
step.

- **`MachineDebug` (`core/debug/machine_debug.*`) is the mechanism**, owned by
  both `Emulator` and `IIgsMachine`: breakpoints (with the temporary one
  behind step over and step out), watchpoints, the trace ring and beam
  breakpoints. None of them is about an instruction set, so none belongs to a
  machine. `beamPosition()` is the beam arithmetic, which both derive from
  their own profile's timing. The //e's older 16-bit methods forward to it.
- **Two things are deliberately not shared.** Cycle profiling is a counter per
  address — 256KB for a 6502 and 64MB for a 65816 — so it stays //e-only and
  the host's heat overlay simply switches itself off. The call-stack summary is
  built by the //e's run loop as it executes JSRs and the IIgs machine keeps no
  such list, so it reports none rather than showing a //e's.
- **A watchpoint on a IIgs is checked on the processor's bus, not inside the
  memory.** That is the difference between the program touching an address and
  anything touching it: the Mega II's video reads the text page on every one of
  192 lines, and a watchpoint there that fired for the scanner would stop the
  machine before a program had run.
- **`ConditionEvaluator` takes a `MachineView`** — a peek function and the
  registers — rather than a `const Emulator&`. Before that, a conditional
  breakpoint on a IIgs was evaluated against a machine that did not exist: it
  silently never fired and every expression read zero.
- **The profile describes the processor** (`processor` in the JSON: address
  bits, register bits, whether there are banks, a direct page and modes, and
  the two sets of flag names a 65816 has), and the host builds its panels from
  it. `machineProcessor()`, `formatMachineAddress()` and `machineAddressMask()`
  in `src/js/machine/machine-profile.js` are how; `BaseWindow.formatAddr()`
  goes through the same formatter so every window writes an address the same
  way — four digits, or a bank and a slash as the machine's own monitor writes
  it. A machine change reaches every window through
  `WindowManager.notifyMachineChanged()`, so a window added later is included
  without anyone remembering.
- **Disassembly is chosen by the core, not the host**, because only something
  holding the live processor can walk a 65816's code stream: its instruction
  lengths depend on the M and X flags. `_disassembleRange` emits three
  tab-separated fields — address, bytes, text — rather than one fixed-width
  string the caller sliced by column, which stopped working the moment an
  address needed six digits and would have failed silently.
- **The trace's rows are formatted in the core** (`_formatTraceRange`), which
  is one round trip for the visible window instead of one heap read per row,
  and one operand formatter per processor rather than one per place that wants
  one. `_getTraceEntrySize` is asked for rather than assumed, because the entry
  grew when it had to hold a 65816's registers.
- **What only covers part of a IIgs says so.** The heat map tracks the Mega
  II's MMU — the side where the video, the firmware's workspace and Applesoft
  live — and its titles name the banks and note that fast RAM is not covered,
  rather than letting a sparse map read as an idle machine. The zero page watch
  shows the direct page register and marks it when it has moved, because its
  addresses are absolute bank-zero ones.

## Keyboard Shortcuts

| Shortcut         | Action                   |
| ---------------- | ------------------------ |
| F1               | Open/close Help window   |
| Ctrl+Escape      | Exit full page mode      |
| Ctrl+V           | Paste text into emulator |
| Ctrl+`           | Open window switcher     |
| Option+Tab       | Cycle to next window     |
| Option+Shift+Tab | Cycle to previous window |
| F5               | Run / Continue execution |
| F10              | Step Over                |
| F11              | Step Into                |
| Shift+F11        | Step Out                 |

**Which host key is Open Apple is the machine's choice.** On the 8-bit
machines the two Option keys are the Apple keys — left Open, right Closed — and
⌘ is left to the browser. A IIgs's keyboard is a Mac's: ⌘ *is* its Open Apple
and Option its Closed Apple, and GS/OS drives its menus with ⌘-letter, so on
that machine the emulator takes ⌘ while it has the keyboard. **View > ⌘ as
Open Apple** is the switch, remembered per machine
(`src/js/input/apple-keys.js`, default on for the IIgs only, unit-tested). With
it on, `InputHandler.translateAppleKeys()` sends ⌘ to the core as the left Alt
and either Option as the right, so the core's Apple-key tracking needs no
second mapping; every ⌘ combination is `preventDefault`ed (a browser still
keeps ⌘W, ⌘Q and the like for itself, which is why this is a choice); and
because macOS delivers no key-up for a key let go while ⌘ is held, the keys
pressed under ⌘ are released when ⌘ is, or AKD would stay high.

The Joystick window has a **Cursor Keys** toggle that also drives the joystick from the arrow keys (full deflection 0/255 per axis). The arrows keep reaching the emulator's keyboard as normal, so ProDOS selectors, catalog menus and BASIC line editing still work while the toggle is on. When enabled, a "CURSOR KEYS" chip appears in the Monitor title bar. The same toggle is in the View menu (`btn-cursor-keys-joystick`), which is how it is reached in the layouts that have no Monitor title bar; menu item, header switch and state restores are kept in sync through `JoystickWindow.onCursorKeysChanged`. The setting persists via localStorage.

## Agent / MCP Integration

The emulator exposes an AI agent interface via the Model Context Protocol (MCP) and AG-UI event protocol. This allows AI agents (including Claude Code) to fully control the emulator programmatically. Multiple emulator browser tabs can connect simultaneously, each identified by a unique name.

### Architecture

Two coordinated components:

- **MCP Server** (`../appleii-agent/`) — Node.js process providing MCP tools over stdio + an HTTP/HTTPS server (port 3033) implementing the AG-UI event protocol (SSE)
- **Frontend Agent Manager** (`src/js/agent/agent-manager.js`) — Browser-side AG-UI client that connects to the server, receives tool calls via SSE, executes them against the emulator, and returns results

### Multi-Emulator Support

Multiple browser tabs can connect simultaneously. Each tab is assigned a unique name from a name pool (stored in `sessionStorage` so it persists across server restarts within the same tab session).

**Routing**: Tools with an optional `emulator` param route as follows:
- `emulator: "Name"` — target specific emulator
- `emulator: "all"` — broadcast to all connected emulators (where supported)
- omitted + 1 connected — use it
- omitted + multiple connected — use the one marked as default
- omitted + multiple + no default — Claude is prompted to pick

**Default emulator**: First tab to connect becomes default. Change with `set_default_emulator`. Use `list_connections` to see all connected emulators and current default.

**Rename**: Double-click the emulator name label on the sparkle button (connected state only) to rename inline. Valid names: Unicode letters, hyphens, underscores — no numbers or spaces. Rename POSTs to `/emulator-rename` on the MCP server and persists the new name to `sessionStorage`.

### Configuration

- `.mcp.json` (repo root) — MCP client config for running the agent
  - Recommended: `bunx -y @retrotech71/appleii-agent` (auto-installs with Bun)
  - Development: `node /path/to/appleii-agent/src/index.js` (local source)
- Environment variables: `PORT` (default 3033), `HTTPS=true` for TLS mode, `APPLEII_AGENT_SANDBOX` (path to sandbox config — required for all file operations)

### Sandbox Configuration

All MCP file operations (loading/saving disk images, BASIC programs, assembly files) are gated by a sandbox config. Without it the agent starts but file access is completely blocked.

**Config file format** (`~/.appleii/sandbox.config`):
```
# Lines starting with # are comments
[key]@/path/to/directory
```
- Key: alphanumeric, underscores, hyphens only
- Path: absolute or `~`-prefixed home-relative

**Wire it up in `.mcp.json`:**
```json
"env": { "APPLEII_AGENT_SANDBOX": "/path/to/sandbox.config" }
```

**Sandbox path syntax in tool calls:** `[key]/relative/path/file`

**Tools that accept sandbox paths:** `load_disk_image`, `load_smartport_image`, `load_file`, `save_to`

**Reload without restarting:** call `reload_sandbox` after editing the config file — no Claude Code restart needed.

Security: path traversal (`../`) and full paths outside all configured directories are blocked. Save tools default to `overwrite: false`.

### MCP Server Tools (`../appleii-agent/src/tools/`)

**Server / Connection**

| Tool | Description |
| ---- | ----------- |
| `server_control` | Start/stop/restart the agent server |
| `set_https` | Enable/disable HTTPS mode |
| `set_debug` | Set debug logging level |
| `get_state` | Return current server + emulator state |
| `get_version` | Agent version info |
| `reload_sandbox` | Reload sandbox.config without restart |
| `disconnect_clients` | Disconnect all SSE clients |
| `shutdown_remote_server` | Shut down another instance on the same port |

**Multi-Emulator**

| Tool | Description |
| ---- | ----------- |
| `list_connections` | List all connected emulators with name, state, isDefault |
| `set_default_emulator` | Set which emulator receives tool calls by default |

**Generic Command**

| Tool | Description |
| ---- | ----------- |
| `emma_command` | Delegate to any frontend app tool via AG-UI. Has optional `emulator` param for routing |

**File Operations — Load Into Emulator**

| Tool | Description |
| ---- | ----------- |
| `load_disk_image` | Load a disk image (.dsk/.do/.po/.nib/.woz) from filesystem → base64 |
| `load_smartport_image` | Load a SmartPort hard drive image (.hdv/.po/.2mg) → base64 |
| `load_file` | Load any file → base64 or text |

**File Operations — Save From Emulator**

| Tool | Description |
| ---- | ----------- |
| `get_screenshot` | Capture screen → returns MCP image content (viewable by LLM). Has optional `emulator` param |
| `save_to` | Load from emulator source → save to sandbox path. Has optional `emulator` param for routing |

`save_to` sources: `basic-editor`, `asm-editor`, `basic-memory`, `file-explorer`, `memory-range`, `screen`, `raw`

**Note:** `showWindow` / `hideWindow` / `focusWindow` are frontend tools — call via `emma_command`, not separate MCP tools.

### Frontend Agent Tools (`src/js/agent/`)

Registered in `agent-tools.js`, organized by category:

**Emulator Control** (`main-tools.js`)
- `emulatorPower` — on/off/toggle
- `emulatorCtrlReset` — warm reset (Ctrl+Reset)
- `emulatorReboot` — cold reset
- `directLoadBinaryAt` — load base64 data to memory address
- `directSaveBinaryRangeTo` — read memory range as base64
- `directLoadFileAt` — load a file from the disk in a drive to a memory address
- `directMemoryCopy` / `directMemoryFill` — bulk memory operations
- `captureScreenshot` — capture display as base64 PNG
- `captureScreenText` — read text from screen (optional row/col range)
- `typeKeyboard` — type text at the machine (goes through the core's paste buffer, so nothing is dropped)

**BASIC Program** (`basic-program-tools.js`)
- `directReadBasic` / `directWriteBasic` / `directRunBasic` / `directNewBasic` — direct memory operations
- `basicProgramLoadFromMemory` / `basicProgramLoadIntoEmulator` — transfer between editor and emulator
- `basicProgramRun` / `basicProgramPause` / `basicProgramNew` / `basicProgramRenumber` / `basicProgramFormat`
- `basicProgramGet` / `basicProgramSet` / `basicProgramLineCount`
- `basicProgramLoadFile` — load a sandbox file into the editor server-side (source bypasses LLM context); pairs with `save_to from:"basic-editor"`
- `saveBasicInEditorToLocal` / `directSaveBasicInMemoryToLocal` — export from editor or from memory
- `basicProgramSetBreakpoint` / `basicProgramUnsetBreakpoint` / `basicProgramListBreakpoints` — statement-level breakpoints
- `basicProgramStepNext` / `basicProgramGetCurrentLine` — stepping and position
- `basicProgramGetVariables` / `basicProgramSetVariable` — inspect and set Applesoft variables
- `basicProgramGetHeatMap` — per-line execution counts

**Assembler** (`assembler-tools.js`)
- `asmAssemble` — compile source code
- `asmWrite` — load assembled code into memory
- `asmLoadExample` — load template program
- `asmNew` / `asmGet` / `asmSet` — editor operations
- `saveAsmInEditorToLocal` — export from editor
- `asmLoadFile` — load a sandbox file into the editor server-side (source bypasses LLM context); pairs with `save_to from:"asm-editor"`
- `asmGetStatus` — compilation status (origin, size, errors)
- `directExecuteAssemblyAt` — execute at address with optional return address

**Disk Drives** (`disk-tools.js`)
- `driveInsertDisc` — load disk image (calls MCP `load_disk_image`)
- `driveInsertBlank` — insert a freshly formatted image
- `diskDriveEject` — eject a drive
- `getDiskImageData` — read the current image back out
- `driveRecentsList` / `driveInsertRecent` / `driveLoadRecent` / `drivesClearRecent` — recent disk management

**SmartPort Hard Drives** (`smartport-tools.js`)
- `smartportInsertImage` — load hard drive image (calls MCP `load_smartport_image`)
- `smartportEject` — detach an image
- `smartportRecentsList` / `smartportInsertRecent` / `smartportClearRecent` — recent image management
- Validates SmartPort card is installed before operations

**File Explorer** (`file-explorer-tools.js`)
- `listDiskFiles` — enumerate DOS 3.3/ProDOS catalog (returns filename, type, size, locked status)
- `getDiskFileContent` — read file from disk (base64 for binary, plaintext for text)

**Window Management** (`window-tools.js`)
- `showWindow` / `hideWindow` / `focusWindow`

**Expansion Slots** (`slot-tools.js`)
- `slotsListAll` — list all slots with current cards and available options
- `slotsInstallCard` / `slotsRemoveCard` / `slotsMoveCard` — card management
- Persists to localStorage, triggers emulator reset after changes

**Printers** (`printer-tools.js`)
- `printerOpen` / `printerClose` / `printerClear` / `printerGetState` — window and paper lifecycle
- `printerSetPower` / `printerSetOnline` / `printerSetModel` / `printerSetup` — printer configuration
- `printerSetPageSize` / `printerSetPaperDimensions` / `printerSetRibbon` / `printerSetAutoLineFeed` — media and ribbon
- `printerSendBytes` / `printerStrike` / `printerSuper` — drive the print head directly
- `printerFeed` / `printerLineFeed` / `printerFormFeed` — paper movement
- `printerDumpScreen` — print the current screen
- `printerCapturePaper` / `printerGetPage` / `printerListHistory` / `printerReloadJob` — read printed output back

**Agent Version** (`agent-version-tools.js`)
- `getAgentVersion` / `checkAgentCompatibility` — version handshake with the MCP server

### WASM APIs Used by Agent Tools

The frontend tools hook into these WASM exports (changes to these require updating agent tools):

- **CPU/Execution**: `_isPaused()`, `_setPaused(bool)`, `_getPC()`, `_setRegPC()`, `_getA/X/Y/SP()`, `_setRegA/X/Y/SP()`, `_getTotalCycles()`, `_reset()`, `_warmReset()`
- **Memory**: `_readMemory(addr)`, `_writeMemory(addr, val)`, `_peekMemory(addr)`, `_malloc()`, `_free()`
- **Disk**: `_isDiskInserted(drive)`, `_getDiskSectorData()`, `_isDOS33Format()`, `_isProDOSFormat()`, `_getDOS33Catalog()`, `_getProDOSCatalog()`, `_readDOS33File()`, `_readProDOSFile()`, `_getDOS33FileBuffer()`, `_getProDOSFileBuffer()`
- **Slots**: `_getSlotCard(slot)`, `_setSlotCard(slot, cardId)`, `_isSmartPortCardInstalled()`
- **Strings**: `stringToUTF8()`, `UTF8ToString()`

### Data Flow

1. Agent calls MCP tool (e.g., `load_disk_image`) → MCP server reads file from filesystem → returns base64
2. Agent calls frontend tool (e.g., `driveInsertDisc`) via `emma_command` → AG-UI SSE delivers tool call to browser
3. Frontend decodes data, calls disk manager / WASM APIs → emulator state updates → result returned via `/tool-result` POST
