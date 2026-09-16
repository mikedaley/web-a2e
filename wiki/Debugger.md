# Debugger

The emulator includes a comprehensive suite of debug windows accessible via the Debug menu. These tools provide deep visibility into the CPU state, memory contents, peripheral hardware, and BASIC program execution.

---

## Table of Contents

- [Overview](#overview)
- [CPU Debugger](#cpu-debugger)
  - [Toolbar and Execution Control](#toolbar-and-execution-control)
  - [Register Display](#register-display)
  - [Disassembly View](#disassembly-view)
  - [Breakpoints](#breakpoints)
  - [Watchpoints](#watchpoints)
  - [Watch Expressions](#watch-expressions)
  - [Beam Breakpoints](#beam-breakpoints)
  - [Bookmarks](#bookmarks)
  - [Symbol Resolution](#symbol-resolution)
  - [User Labels](#user-labels)
- [Memory Browser](#memory-browser)
- [Memory Heat Map](#memory-heat-map)
- [Memory Map](#memory-map)
- [Stack Viewer](#stack-viewer)
- [Zero Page Watch](#zero-page-watch)
- [Soft Switch Monitor](#soft-switch-monitor)
- [Mockingboard Debug](#mockingboard-debug)
- [Mouse Card Debug](#mouse-card-debug)
- [BASIC Program Window](#basic-program-window)
  - [Editor](#editor)
  - [BASIC Debugger](#basic-debugger)
  - [Variable Inspector](#variable-inspector)
  - [BASIC Breakpoints](#basic-breakpoints)
- [Rule Builder](#rule-builder)
- [Assembler Editor](#assembler-editor)
- [Instruction Trace](#instruction-trace)
- [Keyboard Shortcuts](#keyboard-shortcuts)
- [Source Files](#source-files)

---

## Overview

All debug windows extend the `BaseWindow` class from the window manager system. They share common behaviors: draggable/resizable chrome, persistence of position and size via localStorage, and automatic refresh when the emulator is paused or single-stepping.

Debug windows are organized into categories:

| Category | Windows |
|----------|---------|
| CPU & Memory | CPU Debugger, Memory Browser, Memory Heat Map, Memory Map, Stack Viewer, Zero Page Watch |
| Hardware | Soft Switch Monitor, Mockingboard, Mouse Card |
| Programming | BASIC Program Window, Assembler Editor |
| Tools | Rule Builder, Instruction Trace |

---

## CPU Debugger

The CPU Debugger (`cpu-debugger-window.js`) is the primary debug tool, providing register inspection, disassembly, breakpoint management, and stepping controls.

### Toolbar and Execution Control

The toolbar provides:

- **Run** (F5): Resume execution from the current PC
- **Pause**: Break execution at the current instruction
- **Step Into** (F11): Execute a single instruction
- **Step Over** (F10): Execute the current instruction; if it is a JSR, run until the subroutine returns
- **Step Out** (Shift+F11): Run until the current subroutine returns (RTS/RTI)
- **Status indicator**: Shows RUNNING or PAUSED state

### Register Display

The register section shows the current CPU state organized into groups:

**REGS section:**
- A, X, Y registers (8-bit hex)
- SP (stack pointer, 8-bit hex)
- PC (program counter, 16-bit hex)

**FLAGS section:**
- Individual status flags: N (Negative), V (Overflow), B (Break), D (Decimal), I (Interrupt Disable), Z (Zero), C (Carry)
- Active flags are visually highlighted

**TIMING section:**
- CYC: Total CPU cycles since reset
- Scanline and horizontal position (beam position)

Changed register values are highlighted to show what was modified by the last instruction.

### Disassembly View

The disassembly panel shows a scrollable view of decoded 65C02 instructions centered around the current PC. Features include:

- Addresses with symbol annotations from the built-in symbol table
- Operand values resolved to known symbol names (soft switches, ROM routines, zero page locations)
- Color-coded symbol categories (zero page, soft switches, disk, ROM, BASIC, vectors, I/O)
- Current instruction highlighted
- Breakpoint markers in the gutter (click to toggle)
- Address bookmarks for quick navigation
- Profile heat overlay (optional) showing instruction execution frequency

The view can be navigated by clicking addresses or using the address input to jump to any location. When a disassembly view address is set manually, it overrides the PC-centered default.

### Breakpoints

Execution breakpoints pause the emulator when the program counter reaches a specified address. The `BreakpointManager` provides:

| Feature | Description |
|---------|-------------|
| Address breakpoints | Break at a specific PC address |
| Range breakpoints | Break anywhere within an address range (start to end) |
| Conditional breakpoints | Break only when a C-style condition expression evaluates to true |
| Hit count targets | Break only after the Nth hit |
| Enable/disable | Toggle breakpoints without removing them |
| Temporary breakpoints | Used internally for Step Over / Step Out |

Breakpoints are persisted to localStorage (`a2e-breakpoints-v2`) and synchronized with the WASM module. Conditional breakpoints use the C++ condition evaluator (`src/core/debug/`) that supports register references (`A`, `X`, `Y`, `PC`, `SP`, `P`), memory reads (`[addr]`, `[addr,16]`), arithmetic operators, and logical combinators.

### Watchpoints

Watchpoints monitor memory access at specific addresses:

| Type | Trigger |
|------|---------|
| Read | Breaks when the address is read |
| Write | Breaks when the address is written |
| Read/Write | Breaks on either access type |

Watchpoints support the same conditional expressions as breakpoints. They are managed through the same `BreakpointManager` with a `type` field distinguishing them from execution breakpoints.

### Watch Expressions

The Watch panel allows arbitrary C-style expressions to be evaluated on each pause/step. Expressions can reference:

- CPU registers: `A`, `X`, `Y`, `SP`, `PC`, `P`
- Memory reads: `[address]` for 8-bit, `[address,16]` for 16-bit
- Arithmetic: `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `~`, `<<`, `>>`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`

Watch values are displayed with change highlighting when values differ from the previous evaluation.

### Beam Breakpoints

Beam breakpoints pause execution at a specific video scanline and optional horizontal position. This is useful for debugging video timing-sensitive code. Each beam breakpoint has:

- Scanline number (0-261)
- Horizontal position (optional)
- Enable/disable toggle
- Mode selection

### Bookmarks

Address bookmarks provide quick navigation to frequently visited code locations. Bookmarks are shown as markers in the disassembly gutter and stored in localStorage.

### Symbol Resolution

The built-in symbol table (`symbols.js`) provides names and descriptions for hundreds of Apple IIe addresses across categories:

| Category | Examples |
|----------|---------|
| Zero Page | LOMEM, WNDLFT, CH, CV, BASL, TXTTAB, VARTAB |
| Soft Switches | TEXT, MIXED, PAGE2, HIRES, 80COL, DHIRES |
| ROM Routines | COUT, RDKEY, HOME, PRBYTE, INIT |
| Disk | RWTS, IOB, DOS entry points |
| BASIC | Token handler addresses |
| Vectors | RESET, IRQ, NMI, BRK |
| I/O | Keyboard, speaker, game I/O |

Symbols are color-coded by category in the disassembly view.

### User Labels

The `LabelManager` allows users to define custom labels and inline comments for any address. User labels override or supplement the built-in symbol table. Imported symbol files (from assemblers) are also supported. Labels persist to localStorage (`a2e-user-labels`).

---

## Memory Browser

The Memory Browser (`memory-browser-window.js`) provides a scrollable hex/ASCII view of the full 64 KB address space.

**Features:**
- 16 bytes per row with address, hex values, and ASCII representation
- Quick-jump buttons for common regions: Zero Page, Stack, Text Pages, HiRes Pages, I/O, ROM, DOS, Vectors
- Address input for direct navigation
- Hex byte search (finds the next occurrence of a byte sequence)
- Region indicator showing which memory area is currently visible
- Change highlighting: modified bytes flash briefly after they change
- Scroll wheel navigation

**Memory regions recognized:**

| Region | Address Range |
|--------|--------------|
| Zero Page | `$0000-$00FF` |
| Stack | `$0100-$01FF` |
| Input Buffer | `$0200-$02FF` |
| Text Page 1 | `$0400-$07FF` |
| Text Page 2 | `$0800-$0BFF` |
| HiRes Page 1 | `$2000-$3FFF` |
| HiRes Page 2 | `$4000-$5FFF` |
| DOS 3.3 | `$9600-$BFFF` |
| I/O Space | `$C000-$C0FF` |
| Slot ROMs | `$C100-$CFFF` |
| ROM / LC RAM | `$D000-$FFFF` |

---

## Memory Heat Map

The Memory Heat Map (`memory-heat-map-window.js`) provides a real-time visualization of memory access patterns using canvas-based rendering.

**Features:**
- Dual-panel display: Main memory (64 KB) and Auxiliary memory (64 KB)
- Three view modes: Combined (reads + writes), Reads Only, Writes Only
- Optional decay mode: hot spots fade over time instead of accumulating
- Start/Stop/Clear controls for tracking
- Hover tooltip showing the address and region under the cursor
- Click to jump to address in Memory Browser (when connected)

Each pixel in the heat map represents a small block of addresses. Color intensity indicates access frequency: brighter colors indicate more frequent access.

**Region labels are shown for both main and auxiliary memory**, with standard Apple IIe memory regions (Zero Page, Stack, Text Pages, HiRes Pages, etc.) and auxiliary equivalents.

---

## Memory Map

The Memory Map (`memory-map-window.js`) shows the current memory bank configuration at a glance. It displays a visual representation of which memory banks (Main vs Auxiliary) are active for each address range, driven by the current soft switch states.

The map shows bank switching for:
- Zero Page / Stack (`$0000-$01FF`)
- Text Page 1 (`$0400-$07FF`)
- HiRes Page 1 (`$2000-$3FFF`)
- HiRes Page 2 (`$4000-$5FFF`)
- RAM / ROM regions (`$C000-$FFFF`)
- Language Card bank configuration

Active banks are highlighted while inactive banks are dimmed.

---

## Stack Viewer

The Stack Viewer (`stack-viewer-window.js`) displays the current contents of the 6502 stack (page `$01xx`).

**Features:**
- Stack pointer value and depth indicator with visual fill bar
- Call stack reconstruction: identifies JSR return addresses on the stack (6502 pushes PC+2-1, so return addresses are adjusted by +1)
- Per-entry display: address, hex value, and analysis (e.g., "RTS to $XXXX" for detected return addresses)
- Previous SP tracking for highlighting stack changes
- Scroll to current stack pointer position

---

## Zero Page Watch

The Zero Page Watch (`zero-page-watch-window.js`) monitors specific zero page locations with predefined and custom watch groups.

**Predefined watch groups:**

| Group | Locations |
|-------|-----------|
| BASIC Pointers | TXTTAB, VARTAB, ARYTAB, STREND, FRETOP, MEMSIZ, CURLIN, TXTPTR |
| Screen/Window | WNDLFT, WNDWDTH, WNDTOP, WNDBTM, CH, CV, BASL, BAS2L |
| Graphics | GBASL, COLOR, HCOLOR1, HGRX, HGRY |
| DOS 3.3 | DOSSLOT, DOSDRIVE, FILTYP |
| System | LOC0, LOC2, CSWL, KSWL, A1L, A2L, A4L, ACC, XREG, YREG, STATUS |

**Features:**
- Collapsible groups with expand/collapse state persistence
- 8-bit and 16-bit value display
- Custom watches: add any zero page address with a custom label
- Change highlighting with fade-out animation
- Description tooltips for each location

---

## Soft Switch Monitor

The Soft Switch Monitor (`soft-switch-window.js`) displays the state of all Apple IIe soft switches organized by function.

**Switch groups:**

| Group | Switches |
|-------|----------|
| Display Mode | TEXT, MIXED, PAGE2, HIRES, 80COL, ALTCHAR, DHIRES |
| Memory Banking | RAMRD, RAMWRT, ALTZP, 80STORE, INTCXROM, SLOTC3ROM |
| Language Card | LCRAM, LCWRT, LCBNK2 |

Each switch shows:
- Name and current state (ON/OFF)
- Toggle address(es) (e.g., `$C050/51`)
- Brief description
- Active state highlighted with color

The display updates in real-time as switches change during emulation.

---

## Mockingboard Debug

The Mockingboard window (`mockingboard-window.js`) provides a unified channel-centric view of both AY-3-8910 PSGs and their VIA 6522 controllers.

**Channel cards** (6 total: PSG1 A/B/C, PSG2 A/B/C):
- Tone period and computed frequency with musical note name
- Volume level (0-15) with visual level meter
- Mixer state (tone enable, noise enable)
- Envelope mode indicator with shape waveform SVG
- Per-channel mute toggle
- Real-time inline waveform display

**VIA detail section:**
- Timer 1 counter, latch, and mode (one-shot / free-running)
- Timer 2 counter
- ACR, IFR, IER register values
- IRQ active state
- Port A/B data and direction registers

**Additional features:**
- Frequency-to-note conversion (A4 = 440 Hz reference)
- Envelope shape SVGs for all 16 shape values
- Color-coded channel badges (A = blue, B = green, C = red)
- Dirty checking for efficient DOM updates

---

## Mouse Card Debug

The Mouse Card window (`mouse-card-window.js`) displays the state of the Apple Mouse Interface Card.

**Sections:**
- **Status:** Installation state and slot number
- **Mouse State:** X/Y position, button state, interrupt flags
- **Mode:** Enabled, movement tracking, button tracking, VBL interrupt
- **PIA Registers:** MC6821 register values for the mouse protocol
- **Protocol Activity:** Last command (SET, READ, SERV, CLEAR, POS, INIT, CLAMP, HOME), timing

---

## BASIC Program Window

The BASIC Program Window (`basic-program-window.js`) combines a program editor with an integrated Applesoft BASIC debugger.

### Editor

- Syntax-highlighted source code editor with Applesoft BASIC token recognition
- Autocomplete for BASIC keywords
- Line number gutter with breakpoint markers
- Direct loading of programs into emulator memory using tokenization
- Import/export of BASIC source files

### BASIC Debugger

The debugger toolbar provides:
- **Run:** Tokenize and load the program, then execute it
- **Pause:** Break at the next BASIC line
- **Step:** Execute one BASIC statement

The debugger tracks execution state through zero page pointers:
- `CURLIN` (`$75-$76`): Current BASIC line number being executed
- `TXTPTR` (`$7A-$7B`): Pointer to current position in program text

When paused, the currently executing line is highlighted in the editor. Statement-level stepping allows stepping through individual statements within a multi-statement line (statements separated by colons).

### Variable Inspector

The `BasicVariableInspector` reads BASIC variables directly from emulator memory:

| Memory Region | Pointer | Contents |
|---------------|---------|----------|
| Simple variables | VARTAB (`$69`) to ARYTAB (`$6B`) | 7-byte entries (2-byte name + 5-byte value) |
| Arrays | ARYTAB (`$6B`) to STREND (`$6D`) | Variable-length array entries |

**Variable types detected:**
- **Real:** 5-byte Applesoft floating point
- **Integer:** 2-byte signed 16-bit (high byte first)
- **String:** 1-byte length + 2-byte pointer to string data

Variables display with change highlighting. Arrays can be expanded to show individual elements. Auto-refresh mode updates variables while the program is running.

### BASIC Breakpoints

The `BasicBreakpointManager` manages line-level breakpoints for BASIC programs. Breakpoints are set on BASIC line numbers (not memory addresses). The manager supports:

- Line breakpoints with hit counters
- Statement-level stepping mode
- Synchronization with the WASM breakpoint interface
- Persistence to localStorage (`a2e-basic-breakpoints`)

The `BasicProgramParser` reads the tokenized program from memory starting at TXTTAB, parsing the linked list of lines (each line: 2-byte next pointer, 2-byte line number, tokenized text, null terminator).

**Conditional BASIC breakpoints.** A breakpoint can carry a condition expressed in terms of the program's own variables rather than machine state. The condition evaluator understands three BASIC subjects:

| Form | Meaning |
|------|---------|
| `BV(name)` | Read a BASIC variable |
| `BA(name, index)` | Read a one-dimensional array element |
| `BA2(name, i1, i2)` | Read a two-dimensional array element |

So a breakpoint can be `BV(X) > 100`, stopping when the program's `X` exceeds 100 regardless of which line is executing. Rules can also be **condition-only** -- no line number at all -- which turns them into a watch on the program's data.

Values are read through the Applesoft variable model in `src/core/basic/` (`applesoft_vars`), which decodes MBF floats, variable name and type encoding, and walks VARTAB and ARYTAB.

**Line heat map.** The BASIC window can shade each line by how often it has executed, which finds the hot loop in an unfamiliar listing quickly. The data comes from a single `_getBasicHeatMapData` export rather than per-line queries, for the reason described in [[Worker-Architecture]].

---

## Rule Builder

The Rule Builder (`rule-builder-window.js`) provides a visual interface for composing complex breakpoint conditions without writing condition expressions manually.

**Features:**
- Tree-based rule composition with AND/OR groups
- Nested groups for complex logic
- Operand types: registers (A, X, Y, SP, PC, P), memory reads, constants, and BASIC variables and array elements (`BV`, `BA`, `BA2`)
- Comparison operators: equals, not equals, less than, greater than, less/greater or equal
- Bitwise operators for flag testing
- Live preview of the generated condition expression
- Apply/Cancel workflow that integrates with the CPU Debugger's breakpoint system

The rule builder is opened from the CPU Debugger when editing a breakpoint's condition. The generated expression string is passed to the C++ condition evaluator for runtime evaluation.

---

## Assembler Editor

The Assembler Editor (`assembler-editor-window.js`) provides a 65C02 assembly language editor with Merlin-style syntax support.

**Editor features:**
- Merlin-compatible column layout (label, opcode, operand, comment)
- Syntax highlighting for 65C02 mnemonics, directives, labels, and comments
- Real-time syntax validation with error markers in the gutter
- Line numbers, cycle counts per instruction, and assembled byte display in the gutter
- Expression evaluator for operands (arithmetic, symbols, `*` for current PC)
- Breakpoint toggles per assembled line (F9)
- Cursor position indicator showing line and column

**Toolbar:**
- New / Open / Save file operations
- Assemble: assemble source to machine code with error reporting
- Load: inject assembled code into emulator memory at the origin address
- Example program loader
- ROM Routines reference browser (searchable, categorized)

**Assembly output:**
- Per-line hex bytes and addresses
- Symbol table from labels
- Error display with line numbers
- Status bar showing assembled size and origin

---

## Instruction Trace

The Instruction Trace (`trace-panel.js`) displays a scrollable history of recently executed CPU instructions from a ring buffer maintained in WASM.

**Features:**
- Virtual scrolling for performance with large trace buffers
- Each entry shows: address, opcode bytes, mnemonic, operand, symbol annotations
- Cached opcode mnemonic table from the WASM disassembler
- Auto-scroll to most recent instruction on pause

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| F5 | Run / Continue execution |
| F10 | Step Over |
| F11 | Step Into |
| Shift+F11 | Step Out |

See [[Keyboard Shortcuts]] for the complete shortcut reference.

---

## Debugging Any Machine

**Every debug question is asked once, at the widest shape, and a machine answers as much of it as it has.** A 6502's answer is a 65816's with the high halves zero and no banks, so addresses are 24 bits throughout the debug layer and A/X/Y/SP are 16. What a machine does not have — a program bank, a data bank, a direct page, a second mode — reads as zero rather than as an error, because "this machine has none" is the answer. The alternative was a second set of exports and a second set of windows to keep in step.

`MachineDebug` is the shared mechanism: breakpoints (with the temporary one behind step over and step out), watchpoints, the trace ring, and beam breakpoints. None of them is about an instruction set, so none belongs to a machine.

Two things are deliberately **not** shared:

- **Cycle profiling** is a counter per address — 256KB for a 6502 and 64MB for a 65816 — so it stays //e-only, and the heat overlay switches itself off elsewhere.
- **The call-stack summary** is built by the //e's run loop as it executes JSRs; the IIgs machine keeps no such list and reports none rather than showing a //e's.

Other differences worth knowing:

- **A watchpoint on a IIgs is checked on the processor's bus**, not inside the memory — the difference between the program touching an address and *anything* touching it. The Mega II's video reads the text page on every one of 192 lines, and a watchpoint there that fired for the scanner would stop the machine before a program had run.
- **Disassembly is chosen by the core**, because only something holding the live processor can walk a 65816's code stream: its instruction lengths depend on the M and X flags. `_disassembleRange` emits three tab-separated fields — address, bytes, text — rather than one fixed-width string, which stopped working the moment an address needed six digits.
- **The trace's rows are formatted in the core**, one round trip for the visible window instead of one heap read per row.
- **The profile describes the processor** — address bits, register bits, whether there are banks, a direct page and modes, and the two sets of flag names a 65816 has — and the host builds its panels from it. Every window writes an address through the same formatter, so it is four digits or a bank and a slash as the machine's own monitor writes it.
- **What only covers part of a IIgs says so.** The heat map tracks the Mega II's MMU — the side where the video, the firmware's workspace and Applesoft live — and its titles name the banks and note that fast RAM is not covered, rather than letting a sparse map read as an idle machine. The zero page watch shows the direct page register and marks it when it has moved.

## Source Files

### Core Debug Windows

| File | Description |
|------|-------------|
| `src/js/debug/cpu-debugger-window.js` | CPU registers, disassembly, breakpoints, stepping |
| `src/js/debug/memory-browser-window.js` | Hex/ASCII memory viewer with search |
| `src/js/debug/memory-heat-map-window.js` | Real-time memory access visualization |
| `src/js/debug/memory-map-window.js` | Memory bank configuration overview |
| `src/js/debug/stack-viewer-window.js` | Stack contents with call stack reconstruction |
| `src/js/debug/zero-page-watch-window.js` | Zero page location monitoring |
| `src/js/debug/soft-switch-window.js` | Soft switch state display |

### Hardware Debug Windows

| File | Description |
|------|-------------|
| `src/js/debug/mockingboard-window.js` | AY-3-8910 and VIA 6522 state display |
| `src/js/debug/mouse-card-window.js` | Mouse card PIA and protocol state |

### BASIC and Assembly

| File | Description |
|------|-------------|
| `src/js/debug/basic-program-window.js` | BASIC editor with integrated debugger |
| `src/js/debug/basic-breakpoint-manager.js` | BASIC line breakpoint management |
| `src/js/debug/basic-variable-inspector.js` | BASIC variable memory reader |
| `src/js/debug/basic-program-parser.js` | Tokenized BASIC program parser |
| `src/js/debug/assembler-editor-window.js` | 65C02 assembler with Merlin syntax |

### Supporting Modules

| File | Description |
|------|-------------|
| `src/js/debug/breakpoint-manager.js` | CPU breakpoint and watchpoint management |
| `src/js/debug/rule-builder-window.js` | Visual condition expression builder |
| `src/js/debug/symbols.js` | Apple IIe address symbol table |
| `src/js/debug/label-manager.js` | User-defined labels and imported symbols |
| `src/js/debug/trace-panel.js` | CPU instruction execution trace |
| `src/js/debug/index.js` | Debug subsystem module exports |

### C++ Support

| File | Description |
|------|-------------|
| `src/core/debug/` | Condition evaluator for breakpoint expressions |
| `src/core/disassembler/` | 65C02 instruction disassembler |

---

See also: [[CPU Emulation]] | [[Memory System]] | [[Audio System]] | [[Keyboard Shortcuts]]
