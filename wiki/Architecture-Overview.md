# Architecture Overview

This page describes the internal architecture of the Apple //e emulator, covering the two-layer design, audio-driven timing model, WebAssembly interface, and build system.

---

## Table of Contents

- [Two-Layer Design](#two-layer-design)
- [Machine Profiles](#machine-profiles)
- [Component Wiring](#component-wiring)
- [Audio-Driven Timing](#audio-driven-timing)
- [Frame Synchronization](#frame-synchronization)
- [WASM Interface](#wasm-interface)
- [Rendering Pipeline](#rendering-pipeline)
- [Build System](#build-system)
- [Key Constants](#key-constants)

---

## Two-Layer Design

The emulator is split into two distinct layers:

**C++ Core** (`src/core/`) -- Pure emulation logic compiled to WebAssembly. This layer has no browser dependencies and contains:

| Component | File | Responsibility |
|-----------|------|----------------|
| CPU | `cpu/6502/cpu6502.cpp` | Cycle-accurate 65C02 processor |
| MMU | `mmu/mmu.cpp` | 128KB memory, soft switches, expansion slots |
| Video | `video/video.cpp` | Per-scanline rendering of all 6 video modes |
| Audio | `audio/audio.cpp` | Speaker toggle tracking and sample generation |
| Disk | `disk-image/` | DSK/DO/PO/NIB/WOZ format support with GCR encoding |
| Input | `input/keyboard.cpp` | Browser keycode to Apple II keycode translation |
| Cards | `cards/` | Pluggable expansion card system (Disk II, Mockingboard, Thunderclock, Mouse, SmartPort, Super Serial, Parallel, Z-80 SoftCard) |
| BASIC | `basic/` | Applesoft and Integer BASIC tokenizer, detokenizer, and variable model |
| Filesystem | `filesystem/` | DOS 3.3, ProDOS and Pascal filesystem parsers |
| Debug | `debug/` | Breakpoint condition evaluator and host log sink |
| Emulator | `emulator.cpp` | Core coordinator, state serialization |

**JavaScript Layer** (`src/js/`) -- Browser integration using vanilla ES6 modules (no frameworks):

| Component | Directory | Responsibility |
|-----------|-----------|----------------|
| Main | `main.js` | `AppleIIeEmulator` class, initialization, render loop |
| Worker | `worker/` | Web Worker hosting the WASM module, `WasmProxy`, RPC protocol, shared buffers |
| Audio | `audio/` | Web Audio API driver, AudioWorklet processor |
| Display | `display/` | WebGL renderer, CRT shader effects, display settings, no-signal screen |
| Disk Manager | `disk-manager/` | Disk drive UI, SmartPort drives, persistence, surface rendering, drive sounds |
| File Explorer | `file-explorer/` | DOS 3.3 and ProDOS disk browser with disassembler |
| Debug | `debug/` | CPU debugger, memory browser, BASIC viewer, assembler editor, and other debug windows |
| Printer | `printer/` | Virtual printer emulation and paper output |
| Serial | `serial/` | Serial port connection UI |
| Agent | `agent/` | AI agent tools and AG-UI client (see [[Agent-Integration]]) |
| State | `state/` | Save state manager (autosave + 5 manual slots) |
| Input | `input/` | Keyboard handling, text selection, joystick, mouse |
| UI | `ui/` | Menu wiring, theming, slot configuration, dialogs |
| Docking | `docking/` | Window docking and workspace layouts |
| Help | `help/` | Documentation and release notes windows |
| Windows | `windows/` | Base window class and window manager |
| Utils / Config | `utils/`, `config/` | Shared helpers, app version |

The C++ core is compiled to WebAssembly using Emscripten and exposed through a flat C function interface in `src/bindings/wasm_interface.cpp`.

**The WASM module does not run on the main thread.** It is hosted in a dedicated Web Worker, and the main thread talks to it through `WasmProxy`, an ES6 Proxy that turns `_functionName()` calls into asynchronous RPC. The framebuffer and audio ring are shared through `SharedArrayBuffer` so bulk data never crosses as messages. This split is important enough to have its own page -- see [[Worker-Architecture]] -- and it changes how almost every example below is written: WASM calls return Promises, and direct `HEAPU8` access from the main thread is forbidden.

---

## Machine Profiles

The emulator models one machine at a time, and which machine it is comes from a **profile**: `src/core/machine/machine_profile.hpp` holds a `MachineProfile` per machine — CPU variant, timing, memory sizes, display geometry, capabilities, slot layout — and a registry of them.

**The profile is data, not polymorphism.** What differs between a //e, a II Plus and a //c is overwhelmingly numbers, and those live in a struct the subsystems read. They are deliberately not virtual methods: `MMU::read`, the video emitters and the CPU dispatch loop are the hottest code in the emulator, and an indirect call on a per-cycle or per-dot path would cost real speed to serve a machine count of four. The rule is: **a number or a flag goes in the profile; a different mechanism goes in a different class that the profile names.**

**A profile also says which family it belongs to, and that is what selects the parts.** `MachineFamily::AppleII` is the three 8-bit machines: one design, built from `MMU`, `Video`, `Audio` and `CPU6502`, differing only by the numbers in their profiles. `MachineFamily::AppleIIgs` is a different computer — a 65816 on a 24-bit bus, a memory controller that shadows banks, a second display system, an Ensoniq — and it is built from its own classes in `core/iigs/`. The family is chosen once, at construction, and is also what the compile-time validation asks before applying a rule that only holds for one design: a IIgs is not measured against the //e-sized arrays it does not use, or against "a visible column clocks out 14 dots" when its picture is 640 dots wide. Nothing outside `core/iigs/` grows an `if (IIgs)`.

The profile is threaded by construction, not by lookup. `Emulator(MachineId)` selects it and hands it to `MMU` and `Audio`; `Video` takes it *from the MMU* rather than as a second argument, because the video scanner and the floating bus are the same counters read two ways and a pair that disagreed would be a bug with no way to express it. Cards receive it through `ExpansionCard::setMachine()`.

The compile-time constants in `types.hpp` remain, because they size `std::array` members at compile time and a runtime lookup cannot. They hold the //e's values and `machine_profile.hpp` `static_assert`s every one of them against the profile, so the two descriptions cannot drift. Further assertions pin *relationships* rather than numbers, and `allProfilesValid()` runs a self-consistency check and a compiled-storage check over the whole registry in a `static_assert` — a broken profile does not compile.

Save states carry the machine id straight after the version, because everything after that point is laid out to the saving machine's shape; the id is what lets `importState` refuse a state from a different machine rather than read it as garbage. The host reads that header itself and switches machine rather than let the core refuse. The Apple II family's layout and a IIgs's are separate versions, because they share nothing after the header.

The browser layer asks rather than assumes: `src/js/machine/machine-profile.js` fetches the whole profile as one JSON string in a single round trip, and the renderer, the text-selection overlay, the screenshot path and the save-state preview all read the machine's display geometry instead of hardcoding 560x384. A fetch failure falls back to the //e, which is a correct description of a machine that exists.

The profile also describes the **processor** — address bits, register bits, whether there are banks, a direct page and modes, and the two sets of flag names a 65816 has — and the debug windows build their panels from that rather than from a //e's shape. And `machine-availability.js` says which menu items the running machine can use, so the rest are hidden: the slot window on a //c and a IIgs, the CPU speed multiplier on a IIgs, and the printer, serial and SmartPort windows unless something provides them.

The one place that still fixes a size is the shared framebuffer slot. A `SharedArrayBuffer` cannot be resized once handed to the Worker and the AudioWorklet, so the slot is allocated up front at 848x480 and must hold any machine's frame — including the IIgs's 736x448 raster. `setupSharedBuffers()` checks the fit and falls back to `postMessage` rather than let a frame write past the end.

Switching machines **rebuilds** the emulator — there is no way to convert a running machine into a different one, since the RAM, the cards and the save state are all shaped to the machine that made them. See [[Machines]] for the user-facing side.

---

## Component Wiring

The `Emulator` class (in `emulator.cpp`) acts as the central coordinator. During construction, it creates all subsystems and wires them together using callbacks:

```
Emulator
  |
  +-- CPU6502  (read/write callbacks -> MMU)
  |
  +-- MMU      (keyboard, speaker, button, cycle callbacks -> Emulator)
  |     |
  |     +-- ExpansionCard slots[1..7]
  |           +-- Slot 3: 80-column (built-in)
  |           +-- Slot 4: MockingboardCard
  |           +-- Slot 5: ThunderclockCard
  |           +-- Slot 6: Disk2Card
  |
  +-- Video    (cycle callback -> CPU, switch callback -> MMU)
  |
  +-- Audio    (Mockingboard pointer for stereo mixing)
  |
  +-- Keyboard (key callback -> Emulator)
```

The CPU does not access memory directly. Instead, it calls lambda functions provided at construction time that route through the MMU:

```cpp
cpu_ = std::make_unique<CPU6502>(
    [this](uint16_t addr) { return cpuRead(addr); },
    [this](uint16_t addr, uint8_t val) { cpuWrite(addr, val); },
    CPUVariant::CMOS_65C02);
```

The MMU in turn delegates I/O space reads/writes to expansion cards based on the address range, and invokes speaker, keyboard, and button callbacks to communicate back to the Emulator.

---

## Audio-Driven Timing

The emulator uses the Web Audio API as its primary timing source rather than `requestAnimationFrame` or `setInterval`. This approach provides several advantages:

1. **Precise timing** -- The audio callback runs at exactly 48,000 Hz, providing consistent sample-level timing.
2. **Background tab support** -- Web Audio continues to fire in background tabs, keeping emulation running when the tab is not visible.
3. **Synchronized audio** -- Speaker clicks and Mockingboard output are generated in lockstep with CPU execution, preventing drift.

### How It Works

The timing chain flows as follows:

```
AudioWorklet (48kHz)
  --> ring buffer runs low, posts a refill request
    --> main thread forwards MSG_REQUEST_SAMPLES to the Worker
      --> Worker: _generateStereoAudioSamples(buffer, count)
        --> Emulator::runCycles(count * CYCLES_PER_SAMPLE * speedMultiplier)
          --> CPU executes instructions
          --> Video renders scanlines progressively
          --> Disk controller updates per instruction
          --> Mockingboard timers tick
        --> Audio::generateStereoSamples() produces speaker + Mockingboard output
      --> Worker writes samples straight into the shared audio ring
  --> AudioWorklet reads the ring directly and deinterleaves for output
```

The AudioWorklet processor (`audio-worklet.js`) runs on its own thread, processing 128 samples at a time. When its ring buffer runs low it requests a refill; the Worker generates the samples and writes them into a `SharedArrayBuffer` ring that the worklet reads directly.

**Sample data never crosses the main thread** -- only the small refill request does. That matters because audio paces the emulation: with a busy main thread in the audio path, the symptom is speed instability rather than mere crackle. Without `SharedArrayBuffer` (which needs the COOP/COEP headers) the Worker falls back to posting samples for the main thread to relay. That path still works and must keep working, but it is the slow one.

Each audio sample requires approximately 21.3 CPU cycles (`1,023,000 / 48,000`). The WASM function `generateStereoAudioSamples` runs the emulator for `sampleCount * CYCLES_PER_SAMPLE * speedMultiplier` cycles, then generates interleaved stereo samples (speaker centered on both channels, Mockingboard PSG1 on left, PSG2 on right).

### Fallback Timing

When Web Audio is unavailable (suspended by autoplay policy, no audio hardware), the `AudioDriver` falls back to a `setInterval` at 60 Hz, running approximately 17,050 cycles per tick. Audio resumes automatically on the first user interaction.

---

## Frame Synchronization

Frame boundaries are detected inside the emulator's main execution loop. After each instruction, the emulator checks whether `CYCLES_PER_FRAME` (17,030) cycles have elapsed since the last frame:

```cpp
if (currentCycle - lastFrameCycle_ >= CYCLES_PER_FRAME) {
    lastFrameCycle_ += CYCLES_PER_FRAME;  // Aligned increment, no drift
    video_->renderFrame();
    video_->beginNewFrame(lastFrameCycle_);
    frameReady_ = true;
}
```

The frame boundary is advanced by exactly `CYCLES_PER_FRAME` rather than set to the current cycle count. This prevents drift and keeps the VBL detection at `$C019` synchronized with raster effects.

On the JavaScript side, the Worker writes each completed frame into a double-buffered region of shared memory, writing whichever slot the renderer is not reading, then publishes the index and sets a ready flag. The main thread's `requestAnimationFrame` loop calls `pollSharedFrame()`, which claims the frame with `Atomics.exchange` so the same frame is never uploaded twice.

That replaced allocating a fresh 860KB array per frame. The `requestAnimationFrame` loop also drives debug window updates, the beam crosshair overlay, and forced re-renders when the CPU is paused.

The render loop is deliberately **synchronous**: it used to `await` a pause check before drawing, which pushed the actual texture upload and draw into a microtask after a Worker round-trip -- so a busy Worker, which is exactly the Worker running the emulator, pushed frames past their vsync deadline. Pause state is now pushed from the Worker and cached, and everything else in the loop is fire-and-forget.

---

## WASM Interface

The C++ core is exposed to JavaScript through a single global `Emulator` instance accessed via flat C functions in `wasm_interface.cpp`. A single static pointer `g_emulator` holds the instance:

```cpp
static a2e::Emulator *g_emulator = nullptr;
```

### Memory Management

The WASM heap lives in the Worker, so **direct `HEAPU8` / `HEAPF32` access from the main thread is forbidden**. Use the proxy's heap helpers instead, which marshal across the Worker boundary and return typed arrays:

```javascript
const bytes = await wasmProxy.heapRead(ptr, size);     // Uint8Array
const words = await wasmProxy.heapReadU32(ptr, count); // Uint32Array
await wasmProxy.heapWrite(ptr, data);
```

`_malloc()` must be awaited; `_free()` is fire-and-forget. `stringToUTF8()` and `UTF8ToString()` are async. For a `char*`-returning export, `wasmProxy.callString(fn, ...args)` decodes in the Worker so a string costs one round-trip instead of two.

Two rules follow from the Worker servicing RPCs on the same thread that runs the emulation, so every round-trip steals emulation time:

- **Batch reads.** `wasmProxy.batch([['_getPC'], ['_getA'], ...])` collapses many reads into one round-trip. `CPUDebuggerWindow.update()` is the reference example: a single 25-call batch.
- **Bulk work belongs in C++.** A loop that would make one RPC per iteration should become one export. `_disassembleRange` and `_getBasicHeatMapData` exist for exactly this reason.

See [[Worker-Architecture]] for the full contract.

### Exported Functions

All WASM exports are listed explicitly in `CMakeLists.txt` under `EXPORTED_FUNCTIONS`. To add a new export:

1. Define the `extern "C"` function in `wasm_interface.cpp` with `EMSCRIPTEN_KEEPALIVE`
2. Add the mangled name (prefixed with `_`) to the `EXPORTED_FUNCTIONS` list in `CMakeLists.txt`
3. Rebuild WASM with `npm run build:wasm`

The exported API covers:

| Category | Examples |
|----------|----------|
| Lifecycle | `_init`, `_reset`, `_warmReset` |
| Execution | `_runCycles`, `_generateStereoAudioSamples`, `_stepInstruction` |
| CPU State | `_getPC`, `_getA`, `_getX`, `_getY`, `_getSP`, `_getP`, `_getTotalCycles` |
| Memory | `_readMemory`, `_writeMemory`, `_peekMemory`, `_readMainRAM` |
| Video | `_getFramebuffer`, `_getFramebufferSize`, `_isFrameReady`, `_forceRenderFrame` |
| Audio | `_setAudioVolume`, `_setAudioMuted` |
| Disk | `_insertDisk`, `_ejectDisk`, `_getDiskData`, `_isDiskInserted` |
| Debug | `_addBreakpoint`, `_stepOver`, `_stepOut`, `_addWatchpoint`, `_setTraceEnabled` |
| Expansion | `_getSlotCard`, `_setSlotCard`, `_isSlotEmpty` |
| Filesystem | `_isDOS33Format`, `_isProDOSFormat`, `_getDOS33Catalog`, `_getProDOSCatalog` |

---

## Rendering Pipeline

The rendering pipeline has two stages:

**C++ Video Rendering** -- The `Video` class renders into a 560x384 RGBA framebuffer (280x192 doubled). Rendering is progressive: `renderUpToCycle()` is called after each CPU instruction to render scanlines up to the current beam position. At frame boundaries, `renderFrame()` finalizes the current frame using a change log that records video switch changes at specific cycles.

**WebGL Display** -- The JavaScript `WebGLRenderer` uploads the framebuffer as a texture and applies CRT shader effects (scanlines, curvature, bloom, phosphor glow). The display pipeline:

```
WASM Framebuffer (560x384 RGBA, written in the Worker)
  --> shared framebuffer slot + ready flag (SharedArrayBuffer)
  --> main thread pollSharedFrame() claims it with Atomics.exchange
  --> WebGLRenderer.updateTexture(framebuffer)
  --> Fragment shader applies CRT effects
  --> Canvas displays final output
```

While the machine is powered off the renderer uploads a generated no-signal frame instead, and ignores emulator frames until power returns -- see [[Display-Settings]].

---

## Build System

The project uses CMake for C++ compilation and Vite for JavaScript bundling.

### WASM Build

```bash
npm run build:wasm
```

This invokes Emscripten's `emcmake cmake` and `emmake make` to compile the C++ core to WebAssembly. Key Emscripten settings:

| Setting | Value | Purpose |
|---------|-------|---------|
| `WASM` | 1 | Output WebAssembly |
| `MODULARIZE` | 1 | Wrap in factory function |
| `EXPORT_NAME` | `createA2EModule` | Global factory name |
| `ALLOW_MEMORY_GROWTH` | 1 | Dynamic heap expansion |
| `INITIAL_MEMORY` | 32 MB | Starting heap size |
| `MAXIMUM_MEMORY` | 256 MB | Maximum heap size |
| `NO_EXIT_RUNTIME` | 1 | Keep runtime alive |
| `ASYNCIFY` | 0 | Disabled (not needed) |
| Optimization | `-O3 -flto` | Full optimization with LTO |

Output files (`a2e.js` and `a2e.wasm`) are copied to `public/` after compilation.

### ROM Embedding

ROM files are embedded at compile time. A shell script (`scripts/generate_roms.sh`) converts binary ROM files into C arrays in `generated/roms.cpp`, which is `#include`-ed by `emulator.cpp`. The embedded ROMs are:

| ROM File | Size | Purpose |
|----------|------|---------|
| `342-0349-B-C0-FF.bin` | 16 KB | System ROM ($C000-$FFFF) |
| `342-0273-A-US-UK.bin` | 4 KB | Character ROM (US/UK) |
| `341-0027.bin` | 256 bytes | Disk II controller ROM |
| `Thunderclock Plus ROM.bin` | 2 KB | Thunderclock card ROM |
| `Apple Mouse Interface Card ROM - 342-0270-C.bin` | 2 KB | Mouse card ROM |
| `Apple Parallel Interface Card ROM - 341-0057.bin` | 512 bytes | Parallel card ROM |
| `Super Serial Card ROM - 341-0065-A.bin` | 2 KB | Super Serial Card ROM |

The SmartPort card builds its own ROM at runtime rather than loading a dump.

### JavaScript Build

```bash
npm run dev      # Vite dev server at localhost:3000 with hot reload
npm run build    # Production build (WASM + Vite bundle) to dist/
```

The Vite build handles ES6 module bundling, CSS processing, and asset optimization. The audio worklet (`audio-worklet.js`) is loaded separately since worklets cannot be bundled.

### Native Build (Testing)

```bash
mkdir -p build-native && cd build-native
cmake ..
make -j$(sysctl -n hw.ncpu)
ctest --verbose
```

The native build compiles the Catch2 test suites. They cover the CPU (6502/65C02 including Klaus Dormann compliance), memory and MMU, video, audio, disk images (DSK/WOZ/GCR), every expansion card, the DOS 3.3 / ProDOS / Pascal filesystems, the BASIC tokenizer and detokenizer, the assembler, disassembler, keyboard, condition evaluator, and full emulator integration. The emulator itself has no native runtime target.

There is also a JavaScript test suite and a set of consistency checks:

```bash
npm test         # Vitest, tests/js/
npm run check    # export/purity/token guards + npm test
```

`npm run check` runs three guards, each verified to fail when violated: `EMSCRIPTEN_KEEPALIVE` functions against the `EXPORTED_FUNCTIONS` list in both directions, a check that `src/core/` has no host-platform dependencies, and a check that the generated JS BASIC token table matches the C++ one.

---

## Key Constants

Defined in `src/core/types.hpp`:

| Constant | Value | Description |
|----------|-------|-------------|
| `CPU_CLOCK_HZ` | 1,023,000 | CPU clock frequency (1.023 MHz) |
| `AUDIO_SAMPLE_RATE` | 48,000 | Audio output sample rate |
| `CYCLES_PER_SAMPLE` | ~21.3125 | CPU cycles per audio sample |
| `CYCLES_PER_SCANLINE` | 65 | CPU cycles per horizontal scanline |
| `SCANLINES_PER_FRAME` | 262 | Total scanlines per frame (192 visible + 70 VBL) |
| `CYCLES_PER_FRAME` | 17,030 | CPU cycles per video frame (65 x 262) |
| `SCREEN_WIDTH` | 560 | Framebuffer width (280 x 2) |
| `SCREEN_HEIGHT` | 384 | Framebuffer height (192 x 2) |
| `FRAMEBUFFER_SIZE` | 860,160 | Framebuffer byte size (560 x 384 x 4 RGBA) |
| `MAIN_RAM_SIZE` | 65,536 | Main RAM (64 KB) |
| `AUX_RAM_SIZE` | 65,536 | Auxiliary RAM (64 KB) |
| `ROM_SIZE` | 16,384 | System ROM (16 KB) |

---

## See Also

- [[Machines]] -- The machines modelled, and how a profile describes one
- [[CPU-Emulation]] -- 65C02 processor details
- [[Memory-System]] -- MMU, bank switching, soft switches
- [[Video-Rendering]] -- Per-scanline rendering and video modes
- [[Audio-System]] -- Speaker and Mockingboard audio
- [[Expansion-Slots]] -- Card architecture and slot memory map
- [[Save-States]] -- Binary state serialization format
- [[Worker-Architecture]] -- Worker isolation, RPC, and shared memory
- [[Agent-Integration]] -- MCP and AG-UI control surface
