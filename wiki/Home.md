# Apple //e Emulator

A cycle-accurate Apple II emulator running entirely in the browser. Built with WebAssembly (C++ backend) and WebGL rendering, it faithfully reproduces the 1.023 MHz processor, the memory architecture, all standard video modes, the Disk II controller, expansion cards, and speaker audio -- no plugins or installs required.

Four machines are modelled: the **Apple IIe Enhanced**, the **Apple II Plus**, the **Apple IIc** and the **Apple IIgs**. The badge in the header names the one you are running, and clicking it switches. See [[Machines]], and [[Apple-IIgs]] for the machine that is a different computer rather than a different set of numbers.

---

## Feature Highlights

- **Four machines** -- Apple IIe Enhanced, Apple II Plus, Apple IIc and Apple IIgs, chosen from the header badge, each described by a profile and each remembering its own slot layout, display settings and save states
- **Cycle-accurate CPU** at 1.023 MHz -- a 65C02 on the //e and the //c, an NMOS 6502 on the II Plus, and a 65C816 on the IIgs at 2.8 MHz, verified against 5.1 million recorded states from real silicon
- **128KB memory** (64KB main + 64KB auxiliary) with Language Card and double hi-res capability; the IIgs adds 256K to 8M of fast RAM on a 24-bit bus
- **All Apple IIe video modes** -- Text, Lo-Res, Hi-Res, Double Hi-Res, Double Lo-Res, and 80-column text -- plus **Super Hi-Res** on the IIgs, 320 and 640 wide with a palette per scanline
- **GS/OS and the Finder** -- the IIgs boots System 6.0.4 from its built-in SmartPort, with a working mouse, an Ensoniq for sound, and a battery-backed clock whose settings survive a reload
- **Runs in a Web Worker** -- the emulator and WASM live off the main thread, with the framebuffer and audio ring shared through `SharedArrayBuffer`
- **CRT shader effects** -- monitor presets (composite, RGB, monochrome green/amber), scanlines with a brightness-dependent beam, selectable shadow mask geometry, phosphor glow, curvature, and analog artefacts
- **Disk II emulation** with DSK, DO, PO, NIB, and WOZ format support and real-time surface visualization
- **SmartPort hard drives** -- two block devices with `.hdv`, `.po` and `.2mg` image support
- **Expansion card system** -- Mockingboard, Thunderclock Plus, Mouse Card, SmartPort, Super Serial Card, Parallel Card, Microsoft Z-80 SoftCard, No-Slot Clock. A //c has no sockets but decodes every slot address to a part soldered to its board; a IIgs carries its SmartPort in slot 5
- **Game port devices** -- the Apple resistive joystick and paddles, or a Sirius Joyport with two Atari-style digital sticks
- **Virtual printers** -- Epson FX-80 and Apple DMP over the parallel card, ImageWriter I/II over the serial card, with a print browser
- **Audio-driven timing** using Web Audio API at 48 kHz for accurate, drift-free emulation even when backgrounded
- **Save states** -- autosave plus five manual slots stored in IndexedDB, kept per machine, with a state that names the machine that wrote it and switches to it on load
- **Built-in debugger suite** -- CPU debugger, memory browser, heat map, stack viewer, soft switch monitor, BASIC program viewer, rule builder, and more, asking every question at 24 bits so a 65816 and a 6502 answer the same windows
- **Applesoft BASIC editor** and **6502 assembler** with direct memory injection
- **File Explorer** for browsing DOS 3.3 and ProDOS disk contents
- **AI agent interface** over MCP and AG-UI, so tools such as Claude Code can drive the emulator
- **Shareable links** -- `?disk=`, `?hd=` and friends open the emulator with media already inserted
- **Installable PWA** with light, dark, and system-follow themes

## Quick Start

1. Open the emulator in a modern browser (Chrome, Firefox, Safari, or Edge).
2. Click the **Power** button in the toolbar to turn the machine on. While it is off the screen shows a **NO SIGNAL** message.
   To run something other than a //e, click the machine badge in the header first -- see [[Machines]].
3. If no disk is inserted, press **Ctrl+Reset** to reach the Applesoft BASIC prompt.
4. To load software, open **View > Disk Drives**, then drag a disk image onto a drive or click to browse.
5. Press **F1** at any time for built-in documentation.

See [[Getting-Started]] for a full walkthrough.

---

## Wiki Contents

### User Guide

| Page | Description |
|------|-------------|
| [[Getting-Started]] | First-time setup, powering on, inserting disks, keyboard basics, paste, and full-page mode |
| [[Machines]] | Choosing a machine, what differs between the four, ROMs, and what survives a switch |
| [[Apple-IIgs]] | The IIgs in detail -- the 65816, the 24-bit memory map, Super Hi-Res, the Ensoniq, the ADB, and GS/OS |
| [[Display-Settings]] | Monitor presets, CRT effects, analog artefacts, image controls, and rendering options |
| [[Disk-Drives]] | Disk formats, drive UI, surface visualization, write protection, and drive sounds |
| [[SmartPort-Hard-Drives]] | Hard drive images, the SmartPort card, and ProDOS volumes |
| [[File-Explorer]] | Browsing DOS 3.3 and ProDOS disk contents, viewing files, and disassembly |
| [[Printers]] | Parallel and serial printers, paper output, and the print browser |
| [[Save-States]] | Autosave, manual save slots, and state management |
| [[Expansion-Slots]] | Slot configuration and every available card |
| [[Input-Devices]] | Keyboard mapping, the game port (Apple joystick or Sirius Joyport), cursor-key joystick, mouse support |
| [[Keyboard-Shortcuts]] | Complete keyboard shortcut reference |
| [[URL-Parameters]] | Opening the emulator with disks and hard drives already inserted |

### Developer Guide

| Page | Description |
|------|-------------|
| [[Architecture-Overview]] | Two-layer design, the Worker split, audio-driven timing, WASM interface pattern |
| [[Worker-Architecture]] | Web Worker isolation, `WasmProxy`, RPC, and the shared memory transport |
| [[CPU-Emulation]] | 65C02 and 65816 implementations, cycle accuracy, Klaus Dormann and SingleStepTests compliance |
| [[Memory-System]] | MMU soft switches, Language Card, bank switching, auxiliary memory, and the IIgs's 24-bit map and shadowing |
| [[Video-Rendering]] | Scanline rendering, NTSC decoding, Super Hi-Res, WebGL pipeline, CRT shader architecture |
| [[Audio-System]] | Speaker toggle emulation, Mockingboard synthesis, the IIgs's Ensoniq, AudioWorklet pipeline |
| [[Disk-System-Internals]] | GCR encoding, WOZ format handling, Disk II controller state machine |
| [[Debugger]] | CPU debugger, breakpoints, rule builder, BASIC tools, memory windows |
| [[Agent-Integration]] | MCP server, AG-UI protocol, and the frontend tool surface |

---

## Version

This wiki is not pinned to a release. Open **Help > Release Notes** inside the emulator for the current version and what changed in it.
