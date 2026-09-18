# Apple //e Browser Based Emulator

A cycle-accurate Apple II emulator running in the browser using WebAssembly and WebGL. No JavaScript frameworks — vanilla ES6 modules with Vite for bundling. Having built native emulators in the past, this is my first attempt at a browser-based emulator, hopefully making it easier to allow cross platform users from making use of it :)

Four machines are modelled: the **Apple IIe Enhanced**, the **Apple II Plus**, the **Apple IIc** and the **Apple IIgs**. See [Machines](#machines).

**[Run it →](https://web-a2e.retrotech71.co.uk/)**  ·  **[Documentation wiki →](https://github.com/mikedaley/web-a2e/wiki)**

## Features

- **Four machines** — Apple IIe Enhanced, Apple II Plus, Apple IIc and Apple IIgs, chosen from the badge in the header; each is described by a profile and remembers its own slot layout, display settings and save states
- **Cycle-accurate 65C02 CPU** — All legal 6502 opcodes plus 65C02 extensions at 1.023 MHz
- **A 65C816 for the IIgs** — 24-bit bus, 16-bit registers, native and emulation modes, verified against 5.1 million recorded states from real silicon
- **Full Apple //e memory architecture** — 128KB RAM (64KB main + 64KB auxiliary), language card, soft switches; the IIgs adds 256K–8M of fast RAM, bank shadowing and a Mega II inside it
- **Multiple display modes** — Text (40/80 col), LoRes, Double LoRes, HiRes, Double HiRes, monochrome, and **Super Hi-Res** (320/640, palette per scanline) on the IIgs
- **GS/OS** — the IIgs boots System 6.0.4 to the Finder from its built-in SmartPort, with a working mouse, an Ensoniq synthesiser and a battery-backed clock
- **Signal-accurate composite video** — Decodes the machine's real 14.31818 MHz dot stream rather than looking colours up, so artifact colour, the hi-res half-dot shift and colour burst all emerge from the signal
- **WebGL rendering** — Hardware-accelerated display with configurable CRT shader effects and saveable monitor profiles
- **Web Worker architecture** — WASM emulation runs in a dedicated Worker thread, eliminating main-thread blocking
- **Audio-driven timing** — Web Audio API AudioWorklet drives frame timing at 48kHz via Worker RPC
- **Disk II controller** — DSK, DO, PO, and WOZ format support with write capability
- **Expansion cards** — Mockingboard sound card, Thunderclock Plus, Apple Mouse Interface Card, SmartPort hard drive, Super Serial Card, Parallel Card (Centronics), Microsoft Z-80 SoftCard, No-Slot Clock (DS1215). A //c has no sockets but decodes every slot address to a soldered-in part; a IIgs carries its SmartPort in slot 5
- **Virtual dot-matrix printer** — ImageWriter II (colour), ImageWriter I, Epson FX-80, and Apple DMP with period-correct fonts, sounds, and PNG/PDF export, plus a standalone font editor at `/printers/rom-editor.html`
- **File explorer** — Browse DOS 3.3 and ProDOS disk contents with BASIC detokenizer and disassembler
- **Shareable links** — Pass a disk image URL in the address (`?disk=`) to open the emulator with it already loaded
- **Save states** — Autosave slot plus 5 manual save slots, stored in IndexedDB, kept per machine; a state names the machine that wrote it and loading one switches to it
- **Built-in debugger** — CPU debugger, memory browser, heat map, soft switch monitor, BASIC conditional breakpoints, and more
- **Light/Dark/System themes** — Switchable colour scheme with Apple rainbow logo accent palette
- **AI Agent integration** — Full programmatic control via MCP and AG-UI event protocol for AI-assisted development
- **PWA support** — Install as a standalone app with offline functionality

### Machines

The emulator runs one machine at a time, and the badge in the header names it — click it to switch. Four machines are described:

| | Apple IIe (Enhanced) | Apple II Plus | Apple IIc | Apple IIgs |
| --- | --- | --- | --- | --- |
| Released | 1983 | 1979 | 1984 | 1986 |
| CPU | 65C02 | NMOS 6502 | 65C02 | 65C816 @ 2.8 MHz |
| RAM | 128KB (64 main + 64 aux) | 48KB + 16KB language card in slot 0 | 128KB | 128KB Mega II + 256K–8M fast |
| Columns | 40 / 80 | 40 | 40 / 80 | 40 / 80 |
| Double-resolution modes | Yes | No | Yes | Yes, plus Super Hi-Res |
| Character sets | US/UK | One | One | One |
| Colour burst in text | Inhibited | Always sent (text fringes) | Inhibited | Inhibited |
| Expansion slots | 1–7, slot 3 fixed | 0–7, slot 0 fixed | None — every slot address is a soldered-in part | Slot 5 is the built-in SmartPort |
| Disk controller | Disk II card | Disk II card | IWM (built in) | IWM (built in) |
| ROMs | Included | Included | Included | Included |

A machine's differences are data in its profile rather than special cases scattered through the code, so the II+'s missing auxiliary bank is what makes 80 columns and double hi-res genuinely unreachable rather than merely hidden.

The IIgs is the exception to that rule, and deliberately: it belongs to its own `MachineFamily` and is built from its own classes in `src/core/iigs/` — a 65816, a 24-bit memory controller that shadows banks, a second display system, an Ensoniq, an ADB controller and a Z8530. A number or a flag goes in the profile; a different mechanism goes in a different class that the profile names. The [Machines wiki page](https://github.com/mikedaley/web-a2e/wiki/Machines) covers each machine in full, and [Apple IIgs](https://github.com/mikedaley/web-a2e/wiki/Apple-IIgs) covers that one in depth.

Switching rebuilds the emulator, so inserted media and anything in memory are lost exactly as they would be on a page reload — the menu says so first. Volume, character set and CPU speed follow you across, because those were your choices rather than the machine's. Each machine remembers its own slot layout, display settings and save states, and the machine you last chose is restored at startup.

Menu items for hardware the running machine does not have are hidden rather than disabled: a greyed-out "Expansion Slots" on a //c only invites the question of how to enable it, and the answer is a different computer.

## Prerequisites

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (3.0+)
- CMake 3.20+
- Node.js 18+

## ROM Files

Place the following ROM files in the `roms/` directory before building. ROMs are embedded into the WASM binary at compile time via `scripts/generate_roms.sh`.

| File | Size | Description |
|------|------|-------------|
| `342-0349-B-C0-FF.bin` | 16KB | Apple IIe system ROM |
| `342-0273-A-US-UK.bin` | 4KB | Character generator ROM (US/UK enhanced) |
| `341-0027.bin` | 256 bytes | Disk II controller ROM |
| `Thunderclock Plus ROM.bin` | 2KB | Thunderclock card ROM |
| `Apple Mouse Interface Card ROM - 342-0270-C.bin` | 2KB | Mouse Interface Card ROM |
| `Apple Parallel Interface Card ROM - 341-0057.bin` | 512 bytes | Parallel card PROM (341-0057); upper half is 341-0005 "Parallel Printer" firmware |
| `Super Serial Card ROM - 341-0065-A.bin` | 2KB | Super Serial Card firmware |

An alternate character ROM variant `341-0160-A-US-UK.bin` (8KB) is also supported.

### The other machines' ROMs

Every machine's ROMs are in `roms/` and are embedded at compile time, so all
four run out of the box. The rest of this section matters only if you want to
substitute a different dump.

**Apple II Plus**, either the six motherboard ROMs in address order or one
pre-combined 12KB image:

| File | Size | Description |
|------|------|-------------|
| `341-0011.bin` … `341-0015.bin` | 2KB each | Applesoft, `$D000-$F7FF` |
| `341-0020.bin` | 2KB | Autostart monitor, `$F800-$FFFF` |
| `apple2plus.rom` | 12KB | ...or one pre-combined image instead of the six |
| `341-0036.bin` | 2KB | II+ character generator |

**Apple //c**, whose system ROM is 16KB because everything a //c would have put
in a slot is inside it:

| File | Size | Description |
|------|------|-------------|
| `342-0272-A.bin` (or `apple2c.rom`) | 16KB | //c system ROM |
| `342-0265-A.bin` (or `341-0265-A.bin`) | 4KB | //c character generator |

**Apple IIgs**, a ROM 01 image, either as the two socket ROMs or pre-combined.
`loadROM` looks for the emulation reset vector at `$FF:FFFC` to work out which
way round the banks are, so either order loads:

| File | Size | Description |
|------|------|-------------|
| `341-0728.bin` + `341-0749.bin` (or `341-0748.bin`) | 64KB each | ROM 01, as its two parts |
| `342-0077-B.bin` (or `apple2gs.rom`) | 128KB | ...or one pre-combined image |

## Building

### Install Emscripten

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

### Build and Run

```bash
npm install           # Install dependencies
npm run build:wasm    # Build WASM module (required first time and after C++ changes)
npm run dev           # Start dev server at localhost:3000 (hot-reload for JS only)
```

Open http://localhost:3000 in your browser.

### Other Commands

```bash
npm run build         # Full production build (WASM + Vite bundle)
npm run clean         # Clean build artifacts
npm run deploy        # Deploy to the configured rsync target (see .env.deploy.example)
npm test              # Run the JavaScript test suite (Vitest)
npm run check         # Consistency checks + JavaScript tests
```

`npm run check` runs three guards before the tests:

- **`check:exports`** — every `EMSCRIPTEN_KEEPALIVE` function matches the
  `EXPORTED_FUNCTIONS` list in `CMakeLists.txt`, in both directions. A missing
  entry is dead-stripped by the linker and fails at runtime; a stale one is a
  leftover.
- **`check:core-purity`** — `src/core/` contains no host-platform dependencies.
  Platform glue belongs in `src/bindings/`.
- **`check:basic-tokens`** — `src/js/utils/basic-tokens.js` is still in step with
  `src/core/basic/basic_tokens.hpp`, which generates it
  (`npm run generate:basic-tokens`).

## Usage

### Quick Start

1. Click **Power** to start the emulator
2. Click on the screen to give it keyboard focus
3. Use **Insert** buttons to load disk images (DSK, DO, PO, WOZ)
4. Type `PR#6` and press Return to boot from drive 1
5. Or press **Ctrl+Reset** to enter Applesoft BASIC

### Keyboard Mapping

| PC Key | Apple II |
|--------|----------|
| Backspace | Delete (left arrow) |
| Arrow Keys | Arrow Keys |
| Left Alt / Option | Open Apple (joystick button 0) |
| Right Alt / Option | Closed Apple (joystick button 1) |
| ⌘ (IIgs only, by default) | Open Apple — see below |
| Ctrl+Letter | Control characters |
| Escape | ESC |
| Enter | Return |
| Ctrl+Break | Reset (Ctrl+Reset) |

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| F1 | Open help and documentation |
| Ctrl+Escape | Toggle full-page mode |
| Ctrl+V | Paste text into keyboard buffer |
| Ctrl+` | Open window switcher |
| Option+Tab | Cycle to next window |
| Option+Shift+Tab | Cycle to previous window |
| F5 | Run / Continue execution |
| F10 | Step Over |
| F11 | Step Into |
| Shift+F11 | Step Out |

**Which host key is Open Apple is the machine's choice.** On the 8-bit machines
the two Option keys are the Apple keys — left Open, right Closed — and ⌘ is
left to the browser. A IIgs's keyboard is a Mac's: ⌘ *is* its Open Apple and
Option its Closed Apple, and GS/OS drives its menus with ⌘-letter, so on that
machine the emulator takes ⌘ while it has the keyboard. **View > ⌘ as Open
Apple** is the switch, remembered per machine and on by default for the IIgs
only. A browser still keeps ⌘W, ⌘Q and the like for itself, which is why this
is a choice rather than a rule.

### Text Selection

Click and drag on the screen to select text. The selection is automatically copied to the clipboard when you release the mouse.

### Disk Drives

Each drive supports:
- **Insert** — Load a disk image from file
- **Recent** — Quick access to last 20 used disks (tracked per drive)
- **Blank** — Create a new formatted blank disk
- **Eject** — Remove disk (offers to save only if its contents actually changed)

Drag and drop disk files directly onto drives. Drive seek and motor sounds can be toggled on or off.

### Sharing a Link

A disk image URL can be passed in the address so a link opens with the disk already in the drive:

| Parameter | Target |
| --------- | ------ |
| `?disk=` / `?disk1=` | Drive 1 |
| `?disk2=` | Drive 2 |
| `?hd=` / `?hd2=` | SmartPort devices 1 and 2 |
| `?name=` | Filename to use when the URL has none (e.g. `download?id=…`) |
| `?autostart` | Power on and boot immediately, with no interaction |

```
https://your-emulator/?disk=https://example.com/demo.dsk
```

Relative paths work too, for images hosted alongside the emulator: `?disk=/disks/demo.dsk`

A path on your own machine (`/Users/you/Downloads/demo.dsk`) will not work — a web page cannot read local files. Use **Insert** or drag the file onto a drive for those.

Notes:
- Disks loaded this way are **not** written to browser storage or the Recent list, and autosave pauses for the session. A link someone shares can't replace the disks in your own drives — reopen the plain address and your session is intact.
- **The host must send `Access-Control-Allow-Origin`.** This is the main practical limit, and it is decided by the host, not the emulator — a browser cannot read a file the server declines to share, even though `curl` downloads it fine.

  | Works | Doesn't |
  | ----- | ------- |
  | GitHub raw / Pages, Google Drive, Dropbox | Asimov (`asimov.applefritter.com`) |
  | Anything hosted alongside the emulator (`?disk=/disks/x.dsk`) | Most classic archive mirrors and plain Apache/nginx sites |

  To share something from an archive that refuses, re-host the image somewhere CORS-friendly and link that.
- `?name=` is required for `.nib` and `.2mg` images behind extensionless URLs, since those formats can't be identified from their contents.
- `?autostart` boots the machine on load, with nothing to click. The one thing a browser will not allow before a gesture is **sound**, so an autostarted machine runs silent until the visitor clicks or types anything, at which point the speaker joins in. Without `?autostart`, the visitor clicks Power as usual.

### File Explorer

Browse the contents of inserted disks:
- **DOS 3.3** — Catalog listing with file type icons (Text, Integer BASIC, Applesoft BASIC, Binary, etc.)
- **ProDOS** — Folder navigation with full ProDOS file type support
- **File viewer** — View files as hex, BASIC listing (detokenized), or disassembly
- **Disassembler** — Recursive descent flow analysis with symbol resolution

### Save States

- **Autosave** — Saves every 5 seconds while running (enabled by default), kept per machine
- **5 manual slots** — Save and restore at any time from the Save States window
- State includes CPU registers, RAM, language card, soft switches, every slot by card id with that card's own state, disk images with modifications, filenames, and debugger state
- Stored in browser IndexedDB

Every machine writes the same twelve-byte header — magic, format version, and
the id of the machine that wrote it — so the host can tell a //e's state from a
IIgs's before handing either to the core. Everything after the header is laid
out to the saving machine's shape, which is why the id matters: a state
restored into the wrong machine would be read as garbage rather than fail.
Loading a state saved elsewhere switches to that machine first (asking first),
because a save is a save of a whole computer.

The Apple II family's layout is `STATE_VERSION` 9 in `emulator_state.cpp`; a
IIgs's is its own (`iigs_state.cpp`, version 1), since the two share nothing
after the header. A IIgs state carries its fast RAM, the Mega II's RAM and
switches, the memory controller's registers, and every device — ADB, clock
chip with its battery RAM, SCC, Ensoniq — plus the IWM, the floppies and the
SmartPort with its images. That last one is why a state can be tens of
megabytes: a SmartPort card's state *is* its hard drive images.

### Display Settings

The display settings window opens with a **Monitor** preset that sets the whole picture in one choice:

| Preset | What it imitates |
|--------|------------------|
| Pixel Exact | No CRT simulation and no composite effects — sharp square pixels |
| Composite Color | Colour TV or composite monitor — true NTSC decoding, dot triad mask, chroma bleed |
| RGB Monitor | Separate colour signals — sharp, with the mild chroma softening of an analogue RGB stage |
| Monochrome Green | P1 phosphor — long persistence, no mask |
| Monochrome Amber | P3 phosphor — the warmer mono tube |

Each preset also chooses how the core decodes the machine's dot stream — see [Composite Video](#composite-video) below.

Presets leave brightness, contrast, saturation and the bezel alone; adjusting anything a preset covers relabels it as Custom without changing the value. Every individual control remains under **Advanced**:

- Screen curvature, scanlines, beam bloom, shadow mask (aperture grille or dot triad)
- Phosphor glow, vignette, colour bleed
- Flicker, static noise, jitter, horizontal sync lines
- Burn In — phosphor persistence, decaying exponentially and per phosphor (green lingers, blue fades first; monochrome tubes decay as one)
- Brightness, contrast, saturation
- Sharp pixels toggle, overscan/border control
- Monochrome modes: Green, Amber, White

Scanlines model the beam spot as a Gaussian that widens with brightness, so bright lines are fatter than dark ones — the reason white text on a CRT looks bolder than the same glyphs in a screenshot. The shadow mask has a fixed apparent size regardless of display density.

Animated effects are kept within the accessibility limits for flashing content (no more than three flashes per second, and under a 10% change in screen luminance). Flicker is a slow undulation rather than a rapid random one, and the powered-off screen shows a static no-signal message rather than animated television snow.

#### Saved profiles

Tune the picture however you like, then **Save As…** to keep it under a name of your own. Saved profiles appear under **My Profiles** in the Monitor dropdown, next to the built-in monitors.

Adjusting a saved profile keeps its name and marks it modified, and **Save** writes the changes back without prompting — refining a profile does not mean naming it again each time. Unlike the built-in monitors, a profile remembers *everything*, brightness, contrast, saturation and bezel included: it is a snapshot of a picture you liked, so selecting it restores that picture whole. Profiles live separately from the rest of the display settings, so **Reset to Defaults** does not remove them.

### Composite Video

An Apple //e does not output pixels. It outputs one bit per 14.31818 MHz dot, four dots to a cycle of the 3.579545 MHz colour subcarrier, and every colour it appears to produce is manufactured by the *receiver*. The emulator generates that dot stream and then decodes it, rather than looking colours up in a table:

| Decoder | What it models |
|---------|----------------|
| Composite | Full NTSC demodulation — what a composite monitor really shows |
| RGB Monitor | The digital decode an Apple RGB card does, plus mild chroma softening |
| Pixel Exact | The same digital decode with no filtering at all |
| Monochrome | The dot stream straight to a single phosphor |

Several behaviours follow from the signal rather than being special-cased:

- **The hi-res high bit is a real one-dot delay**, pushing a byte's pixels half a hi-res pixel right onto the opposite subcarrier phase. That is the whole mechanism behind orange and blue.
- **Artifact colours and lo-res colours are the same sixteen colours**, because they are the same mechanism.
- **Colour burst is modelled per scanline**, and the monitor's colour killer per field. A //e inhibits burst in text mode, so a full text screen is crisp and white; a mixed graphics screen still carries burst on most of its lines, so the text at the bottom fringes green and violet exactly as it does on real hardware.
- **80-column text on the Composite preset is genuinely soft**, which is why Apple sold a monochrome monitor. Use Pixel Exact, RGB Monitor or a monochrome preset to read it.

The demodulator's phase and gain were fitted by least squares against the physically self-consistent Apple II colours, and the composite lookup table is verified in the test suite to be an exact memoisation of the filter rather than an approximation of it.

Pixel Exact applies no composite effects whatsoever: unlit dots are black and colour never extends past the pixels that are lit. It still shows the artifact colours hi-res art was drawn to exploit — it simply does not smear them.

### Expansion Cards

Cards are configured via **View > Expansion Slots**.

| Slot | Default | Available Cards |
|------|---------|-----------------|
| 1 | Empty | Parallel Card, Super Serial Card, Z-80 SoftCard |
| 2 | Empty | Parallel Card, Super Serial Card, SmartPort, Z-80 SoftCard |
| 3 | 80-Column | Built-in (fixed) |
| 4 | Mockingboard | Mouse Card, SmartPort, Z-80 SoftCard, Empty |
| 5 | Thunderclock Plus | SmartPort, Z-80 SoftCard, Empty |
| 6 | Disk II | Empty |
| 7 | SmartPort | Thunderclock Plus, Z-80 SoftCard, Empty |

**Mockingboard** — Dual AY-3-8910 sound chips with VIA 6522 timers. Stereo output with per-channel mute controls. All audio (speaker, Mockingboard, drive sounds) is unified under a single volume slider and mute toggle.

**Thunderclock Plus** — ProDOS-compatible real-time clock card.

**Apple Mouse Interface Card** — Mouse input via MC6821 PIA command protocol.

**SmartPort Hard Drive** — Block device controller supporting two hard drive images (.hdv/.po/.2mg).

**Parallel Card** — Centronics parallel port (slots 1–2). Drives the Epson FX-80 and Apple DMP virtual printers.

**Super Serial Card** — ACIA 6551-based serial card with built-in WebSocket-to-TCP proxy and Hayes modem emulation for BBS and dial-up software. Drives the ImageWriter I and ImageWriter II virtual printers (slots 1–2).

**Microsoft Z-80 SoftCard** — Z80 co-processor card enabling CP/M software to run on the Apple //e. Full Z80 CPU emulation at 2.041 MHz with hardware-accurate address translation.

### Joystick & Game Controllers

A floating joystick window provides visual paddle/joystick controls that map to the Apple II game ports ($C064-$C067). Physical game controllers are supported via the Gamepad API — the left stick maps to paddle values and buttons A/B map to Apple II buttons 0/1, with a configurable deadzone. A **Cursor Keys** toggle makes the arrow keys drive the joystick as well as the keyboard — they still reach the emulator, so arrow-key navigation in ProDOS and BASIC keeps working — with an indicator chip in the Monitor title bar when active. The same toggle is in **View > Cursor Keys as Joystick**, which is the way to reach it in the layouts that have no Monitor title bar.

**Game port device.** The window's **Game port** selector chooses what is plugged into the game I/O connector: the Apple resistive joystick, or a **Sirius Joyport**. The Joyport was Sirius Software's 1981 adapter that put two Atari CX40-style digital sticks on the same connector, reading them through the annunciators — no RC discharge to wait for, and two players. Choose it and the window swaps its paddle knob for a pair of digital sticks, each with a D-pad and a fire button that can be held with the mouse; a connected gamepad drives stick 1 (or both, with only one pad connected) from either its D-pad or its left stick, and a second gamepad drives stick 2. Games from Sirius Software and others that offer a "Joyport" option will find it. The choice is remembered, and survives reset and a machine switch.

One deliberate departure from the hardware: a Joyport idles the two pushbutton lines *high*, and on a //e those lines are the Open and Closed Apple keys — so a real //e with a Joyport fitted read "Closed Apple held" at every reset and ran the self test instead of booting. The Joyport here lets go of those two lines for about 50ms after a reset, long enough for the ROM to look and far too short for a game to have asked about the stick.

### CPU Speed

**View > CPU Speed** runs the machine at 1x, 2x, 4x or 8x the real 1.023 MHz clock — 8x is roughly an accelerator card. Audio still paces the emulation and still plays, but it plays sped up: everything the speaker does happens in a fraction of the time and rises in pitch to match, exactly as it did on accelerated hardware. The display stays at 60fps; the machine simply gets through more work between frames.

The speed is a host preference, not machine state, so it survives reset and reboot, persists across sessions, and is not written into save states. Pasting still boosts to 8x for the duration of the paste and then hands the machine back to the chosen speed. Any setting above 1x shows a chip in the Monitor title bar.

## Architecture

```
+---------------------------------------------+
|            Browser Environment              |
|---------------------------------------------|
|  WebGL Renderer | Web Audio | IndexedDB     |
|---------------------------------------------|
|           JavaScript Layer (ES6)            |
|  +-------------------------------------+    |
|  | Emulator | DiskManager | Debugger   |    |
|  | Display  | FileExplorer| SaveStates |    |
|  +-------------------------------------+    |
|---------------------------------------------|
|           WebAssembly Module (C++)          |
|  +------+-----+-------+------+--------+     |
|  | CPU  | MMU | Video | Audio| Disk II|     |
|  |65C02 |128KB|       |      |        |     |
|  +------+-----+-------+------+--------+     |
|         ...or, for the Apple IIgs:          |
|  +------+--------+-------+-------+----+     |
|  |65816 | IIgs   | SHR + | Enso- | ADB|     |
|  |24-bit| Memory | MegaII| niq   | SCC|     |
|  +------+--------+-------+-------+----+     |
|                                             |
|  +-------+------------+-------+-------+     |
|  | Cards | Filesystem | BASIC | Disasm|     |
|  +-------+------------+-------+-------+     |
+---------------------------------------------+
```

**Which parts a machine is built from comes from its profile's family.**
`MachineFamily::AppleII` is the three 8-bit machines: one design, built from
`MMU`, `Video`, `Audio` and `CPU6502`, differing only by the numbers in their
profiles. `MachineFamily::AppleIIgs` is a different computer and is built from
its own classes in `src/core/iigs/`. The family is chosen once, at
construction. Nothing outside `src/core/iigs/` grows an `if (IIgs)`.

The emulator core runs in a dedicated Web Worker, and where the browser allows
`SharedArrayBuffer` the screen and audio travel through shared memory rather than
being copied between threads — so the main thread never sits in the audio path
and no memory is allocated per frame. The emulator falls back to message passing
automatically if shared memory is unavailable.

### Audio-Driven Timing

The emulator uses Web Audio API to drive timing:

1. AudioWorklet requests samples at 48kHz
2. WASM runs CPU for ~21.3 cycles per audio sample
3. Speaker toggle events ($C030) generate the audio waveform
4. Video frame rendered when cycle count crosses ~17,030 cycles (60 Hz)

This ensures consistent emulation speed, no audio drift, and operation even when the browser tab is backgrounded.

### WASM Interface

Single global `Emulator` instance in C++ (`wasm_interface.cpp`). JavaScript allocates WASM heap memory with `_malloc`/`_free` and uses `stringToUTF8()`/`UTF8ToString()` for string conversion. New WASM exports must be added to the `EXPORTED_FUNCTIONS` list in `CMakeLists.txt`.

## Debug Tools

All debug windows are accessible from the **Debug** menu.

| Tool | Description |
|------|-------------|
| **CPU Debugger** | Registers (REGS, FLAGS, TIMING, BEAM sections), breakpoints, step/over/out, disassembly with symbols |
| **Memory Browser** | Full 128KB hex/ASCII view with search |
| **Memory Heat Map** | Real-time memory access visualization (read/write/combined) |
| **Memory Map** | Address space layout overview |
| **Stack Viewer** | Monitor stack page ($0100-$01FF) |
| **Zero Page Watch** | Monitor zero page locations with predefined and custom watches |
| **Soft Switch Monitor** | Apple II soft switch states ($C000-$C0FF) |
| **Mockingboard** | Unified channel-centric view: AY-3-8910 and VIA registers, inline waveforms, level meters, per-channel mute controls |
| **Mouse Card** | PIA registers, position, mode, interrupt state |
| **Rule Builder** | Complex conditional breakpoints with C-style expressions |

The CPU debugger supports breakpoints (conditional with expression evaluation), watchpoints, beam breakpoints (video position with wildcard-scanline support), execution tracing, and a call stack viewer. Labels and symbols are supported for both system routines and user-defined addresses. Debugger state (breakpoints, watches, settings) persists across save/load.

## Dev Tools

Development tools are accessible from the **Dev** menu.

| Tool | Description |
|------|-------------|
| **BASIC Program** | Write, edit, and paste Applesoft BASIC programs with syntax highlighting, autocomplete, line heat map, trace toggle, statement-level breakpoints, variable inspector, and run/stop/pause/step controls |
| **Assembler** | Merlin-compatible 65C02 assembler with macros, conditional assembly, loops, Sweet-16, live validation, ROM routines reference, breakpoint support, and file save/load |

### Assembler Features

The assembler follows Merlin, not generic assembler convention: expressions run
strictly left to right with no operator precedence (`1+2*3` is 9), a comment
needs no semicolon because it is simply the fourth whitespace-separated field,
and a string's delimiter chooses whether the high bit is set.

- **Macros** — `MAC`/`EOM`, called by name or with `>>>` / `PMC`, parameters `]1`–`]8`, local labels scoped to one expansion
- **Conditional assembly** — `DO`/`IF`/`ELSE`/`FIN`, nested
- **Loops** — `LUP` … `--^`
- **Dummy sections** — `DUM`/`DEND` for laying out structures without emitting code
- **Local labels and variables** — `:LOCAL` scoped to the preceding global label, `]VAR` reassignable, `VAR` for the numbered set
- **Full data and string set** — `DFB` `DW` `DA` `DDB` `ADR` `ADRL` `HEX` `CHK` `DS` (with fill and page-align), `ASC` `DCI` `INV` `FLS` `REV` `STR` `STRL`
- **Included source** — `PUT` and `USE` read from the disk in a drive, DOS 3.3 or ProDOS, honouring Merlin's `T.` naming
- **Object files** — `DSK`/`SAV` write the object to a disk, `TYP` sets its ProDOS type
- **Sweet-16** — `SW` switches the opcode field to Woz's 16-bit interpreter
- **Syntax highlighting** for opcodes, directives, labels, operands, and comments
- **Column guides** for Merlin's column-based format (Label, Opcode, Operand, Comment)
- **Live validation** with inline error messages, and warnings for what Merlin could do and a browser cannot
- **ROM Routines Reference** (F2) — searchable database of Apple II ROM routines with insert capability
- **Breakpoints** — click gutter or press F9 to toggle breakpoints on instruction lines
- **File operations** — New, Open, Save with Ctrl/Cmd+N/O/S shortcuts
- **Symbols panel** — view all defined labels and their addresses
- **Hex output** — view assembled machine code bytes
- **Problems panel** — every error and warning from the last assembly in one list; click a row to jump to its line

`REL`, `ENT`, `EXT` and `LNK` produce relocatable object code for a linker,
which this assembler does not have, so they are reported rather than quietly
ignored. `MX` and a second `XC` ask for the 65816, which a //e cannot run.

## AI Agent Integration

The emulator exposes an AI agent interface via the [Model Context Protocol](https://modelcontextprotocol.io/) (MCP) and AG-UI event protocol, allowing AI agents (including Claude Code) to fully control the emulator programmatically.

- **MCP Server** (`mcp/appleii-agent/`) — provides tools over stdio + HTTP/SSE on port 3033
- **Frontend Agent Manager** (`src/js/agent/`) — browser-side client that executes tool calls against the emulator

Agent capabilities include: emulator power/reset, BASIC program editing and execution, 65C02 assembly, disk and hard drive management, file exploration, window management, and expansion slot configuration.

See the [Agent Integration wiki page](https://github.com/mikedaley/web-a2e/wiki/Agent-Integration) for full details.

## Testing

### JavaScript Tests

Vitest, covering the printer emulation, the Applesoft listing parser, and input
mapping:

```bash
npm test              # single run
npm run test:watch    # re-run on change
```

### CPU Compliance Tests

Klaus Dormann's 6502/65C02 functional test suites:

```bash
mkdir -p build-native && cd build-native
cmake ..
make -j$(sysctl -n hw.ncpu)
ctest --verbose
```

Test executables: `klaus_6502_test` (NMOS 6502), `klaus_65c02_test` (65C02 extended opcodes).

### Thunderclock Tests

Native C++ tests for Thunderclock card emulation, including MMU integration:

```bash
# Built and run via the same native CMake build above
```

### GCR Encoding Tests

Native C++ tests for Group Code Recording disk encoding logic.

### Integration Tests

JavaScript tests for disk boot, memory, and debugging:

```bash
node tests/integration/disk-boot-test.js
```

## Project Structure

```
web-a2e/
├── src/
│   ├── core/                # C++ emulator core (namespace a2e::)
│   │   ├── cpu/
│   │   │   ├── 6502/        # Cycle-accurate 65C02 processor
│   │   │   └── 65816/       # The IIgs's 65C816: 24-bit bus, 16-bit registers
│   │   ├── mmu/             # Memory management, soft switches
│   │   ├── machine/         # Machine profiles and the registry of machines
│   │   ├── iigs/            # The IIgs's own parts, kept apart from every
│   │   │                    #   other machine's: memory, video, ADB, clock,
│   │   │                    #   Ensoniq, SCC, and the machine that owns them
│   │   ├── video/           # Per-scanline signal generation + NTSC/RGB decoding
│   │   ├── audio/           # Speaker emulation
│   │   ├── disk-image/      # Disk formats (DSK/DO/PO/WOZ), GCR encoding
│   │   ├── disassembler/    # 65C02 and 65816 disassemblers
│   │   ├── input/           # Keyboard, Sirius Joyport, the //c's IOU mouse
│   │   ├── cards/           # Expansion card system
│   │   │   ├── disk_controller.*  # The drive mechanism both machines share
│   │   │   ├── disk2/       # Disk II controller card
│   │   │   ├── iwm/         # Integrated Woz Machine (//c and IIgs)
│   │   │   ├── mockingboard/  # AY-3-8910 + VIA 6522
│   │   │   ├── mouse/       # Apple Mouse Interface Card
│   │   │   ├── smartport/   # SmartPort hard drive controller
│   │   │   ├── softcard/    # Microsoft Z-80 SoftCard
│   │   │   │   └── z80/     # Z80 CPU emulation core
│   │   │   ├── parallel/    # Parallel (Centronics) card
│   │   │   ├── serial/      # The //c's two built-in serial ports
│   │   │   ├── ssc/         # Super Serial Card + ACIA 6551
│   │   │   └── thunderclock/  # Thunderclock Plus
│   │   ├── filesystem/      # DOS 3.3, ProDOS and Pascal parsers and writers
│   │   ├── basic/           # BASIC tokenizer and detokenizer
│   │   ├── assembler/       # 65C02 assembler (Merlin-style syntax)
│   │   ├── debug/           # Shared debug facilities, condition evaluator
│   │   ├── emulator/        # Split emulator implementation files
│   │   │   ├── state_stream.hpp    # The writer/reader every machine uses
│   │   │   ├── emulator_state.cpp  # State serialization
│   │   │   └── emulator_debug.cpp  # Debug facilities
│   │   ├── emulator.cpp     # Core coordinator
│   │   ├── emulator.hpp     # Emulator class declaration
│   │   └── types.hpp        # Shared constants
│   ├── bindings/            # wasm_interface.cpp (WASM exports)
│   └── js/                  # ES6 modules
│       ├── main.js          # AppleIIeEmulator entry point
│       ├── agent/           # AI agent tools and manager (MCP/AG-UI)
│       ├── audio/           # Web Audio API driver and AudioWorklet
│       ├── config/          # App version
│       ├── debug/           # Debug window implementations
│       ├── disk-manager/    # Drive UI, persistence, surface renderer, sounds
│       ├── display/         # WebGL renderer, CRT shaders, display settings, user profiles
│       ├── file-explorer/   # DOS 3.3/ProDOS browser, file viewer, disassembler
│       ├── help/            # Documentation and release notes
│       ├── input/           # Keyboard, text selection, joystick, mouse, Apple keys
│       ├── machine/         # Host-side machine profile, menu, slot storage,
│       │                    #   the IIgs's memory size and battery RAM
│       ├── printer/         # Virtual dot-matrix printer (IW-I, IW-II, FX-80, DMP)
│       ├── state/           # Save state manager and persistence
│       ├── ui/              # Menu wiring, reminders, slot configuration
│       ├── utils/           # Storage, string, BASIC utilities
│       └── windows/         # Base window class and window manager
├── public/                  # Static assets, built WASM, shaders
│   ├── css/                 # Stylesheets
│   ├── shaders/             # CRT vertex/fragment shaders
│   ├── assets/              # Images and sounds
│   └── index.html           # Main HTML entry point
├── roms/                    # ROM files (not included)
├── tests/
│   ├── klaus/               # Klaus Dormann CPU compliance tests
│   ├── thunderclock/        # Thunderclock card tests
│   ├── integration/         # JS integration tests
│   └── gcr/                 # GCR encoding tests
├── scripts/                 # Build scripts (generate_roms.sh)
├── CMakeLists.txt           # C++ build configuration
├── vite.config.js           # Vite bundler configuration
└── package.json
```

## Development Workflow

**C++ changes** require rebuilding WASM: `npm run build:wasm`

**JavaScript changes** auto-reload via the Vite dev server.

**Full build** for production: `npm run build` (outputs to `dist/`).

## Browser Compatibility

Requires WebAssembly, WebGL 2.0, Web Audio API (AudioWorklet), IndexedDB, and Service Worker support. Works in current versions of Chrome, Firefox, Safari, and Edge.

## TODO

### Input
- **Configurable key bindings** — Allow remapping of Apple II keys and shortcuts

### Disk & Storage
- **Further WOZ copy protection compatibility** — The stepper now uses the canonical four-magnet model and unformatted tracks return weak-bit noise; remaining work covers full quarter-track sub-positioning and cross-track sync
- **2IMG format support** — Universal disk image format with metadata

### Development Tools
- **Source-level debugging** — Step through assembly source with symbol mapping from assembler
- **Profiler** — Cycle-accurate performance profiling with per-routine breakdown and heat maps
- **I/O trace log** — Record and replay soft switch and card I/O activity

### Audio
- **Mockingboard speech synthesis** — SC-01 Votrax speech chip emulation
- **SAM speech synthesizer** — Software Automatic Mouth support

### Display
- **Video recording** — Capture emulator screen to video file
- **Screenshot export** — Save screen contents as PNG

### Networking
- **Uthernet / Ethernet emulation** — TCP/IP networking via WebSocket bridge for Contiki, etc.

### Platform
- **Disk image library** — Browse and load from a curated online software archive
- **URL disk loading** — Load disk images directly from a URL parameter
- **Mobile touch controls** — On-screen keyboard and virtual joystick optimized for touch devices

## License

MIT License. See [LICENSE](LICENSE) for details.

## Acknowledgments

- Based on the native [a2e](https://github.com/mikedaley/a2e) emulator
- CPU emulation derived from [MOS6502](https://github.com/mikedaley/MOS6502)
- Klaus Dormann's [6502 functional tests](https://github.com/Klaus2m5/6502_65C02_functional_tests)
- Inspired by [AppleWin](https://github.com/AppleWin/AppleWin) and [Apple2TS](https://github.com/nickmcummins/apple2ts), both outstanding Apple II emulators that have been invaluable references for hardware accuracy and feature direction
