# Save States

The emulator provides a complete save state system that captures the entire machine state and stores it in the browser's IndexedDB. States can be saved and restored automatically or manually, and can be downloaded as files for backup or sharing.

## Table of Contents

- [Overview](#overview)
- [Autosave](#autosave)
- [Save States Window](#save-states-window)
- [Manual Save Slots](#manual-save-slots)
- [Loading a State](#loading-a-state)
- [Clearing a Slot](#clearing-a-slot)
- [Downloading and Importing State Files](#downloading-and-importing-state-files)
- [States and Machines](#states-and-machines)
- [What Is Included in a Save State](#what-is-included-in-a-save-state)
- [Screenshots and Previews](#screenshots-and-previews)
- [Storage](#storage)

## Overview

The save state system consists of:

- **Autosave slot** -- Automatically saves the emulator state at regular intervals
- **5 manual slots** -- User-triggered save/load with individual screenshots
- **File import/export** -- Download states as `.a2state` files or load them back

## Autosave

The autosave system runs in the background while the emulator is powered on. It captures the full machine state periodically so that your session is preserved if you close the browser tab or navigate away.

### Autosave Behavior

- Saves every **5 seconds** while the emulator is running and the tab is visible
- Saves immediately when the browser tab becomes hidden (tab switch, minimize)
- Saves on page unload (closing the tab or navigating away)
- The autosave indicator briefly flashes in the menu bar each time a save occurs
- Autosave is enabled by default and can be toggled from the **File** menu

### Toggling Autosave

The autosave toggle is in the **File** menu. The setting persists across sessions via localStorage.

### Restoring from Autosave

When the emulator starts, if an autosave state exists for the machine you are running, it is automatically restored. You can also manually load the autosave from the Save States window.

**The autosave is kept per machine.** A state only restores into the machine that wrote it, so one shared autosave slot would come back to nothing for every machine but the last one you used. Each machine has its own record, keyed by machine (`autosave:<key>`); the record from before there was more than one machine is the //e's.

## Save States Window

Open the Save States window from **File > Save States...**. The window shows all available save slots arranged vertically:

- **Autosave row (A)** -- Shows the most recent autosave with a thumbnail, timestamp, and Load/Download buttons
- **Slots 1-5** -- Manual save slots, each with Save, Load, Clear, and Download buttons
- **Load from File** button -- At the bottom, allows importing a previously downloaded `.a2state` file

Each slot displays:

- A **slot number** (A for autosave, 1-5 for manual slots)
- A **thumbnail screenshot** of the emulator display at the time of the save
- A **status label** ("Saved", "Empty", or "Autosave")
- A **timestamp** showing when the state was saved (e.g. "just now", "5m ago", "2h ago", or a date)

Hovering over a slot's thumbnail shows a larger preview image.

## Manual Save Slots

There are 5 manual save slots. Each slot supports four actions:

| Action | Description |
|--------|-------------|
| **Save** | Captures the current emulator state and screenshot into the slot. The emulator must be powered on. |
| **Load** | Restores the emulator to the saved state. Power-cycles the emulator first for a clean restore. |
| **Clear** | Removes the saved state from the slot, freeing storage space. |
| **DL** (Download) | Downloads the saved state as an `.a2state` file. |

The autosave slot only has Load and Download buttons -- it cannot be manually saved to or cleared.

## Loading a State

When a state is loaded (from any slot or from a file):

1. The emulator is stopped (power-cycled)
2. A fresh emulator session is started
3. The saved state data is imported into the WASM core
4. Disk drive state is synchronized with the restored state
5. Debugger breakpoints and watchpoints are re-synced
6. The emulator continues running from the exact point where it was saved

## Clearing a Slot

Clicking **Clear** on a manual slot removes the saved state data and screenshot from IndexedDB. The slot returns to the "Empty" state. The autosave slot cannot be manually cleared.

## Downloading and Importing State Files

### Downloading

Click the **DL** button on any populated slot to download the state as a file. The file is saved with the `.a2state` extension:

- Autosave downloads as `apple2e-autosave.a2state`
- Manual slots download as `apple2e-slot-N.a2state` (where N is the slot number)

If the browser supports the File System Access API, a native save dialog appears. Otherwise, the file downloads directly.

### Importing from File

Click **Load from File...** at the bottom of the Save States window to import a previously downloaded `.a2state` file. The file is validated by checking for the `A2ES` magic bytes in the header before attempting to restore the state.

## States and Machines

**Every machine writes the same twelve bytes first** — a magic number, a format version, and the id of the machine that wrote it. The host reads those without knowing the rest of the layout, which is how it can tell a //e's state from a IIgs's *before* handing either to the core.

Everything after the header is laid out to the saving machine's shape, so a state restored into a different machine would be read as garbage rather than fail cleanly. The id is what prevents that.

Asked to load a state saved on another machine, the emulator **switches to that machine first** rather than letting the core refuse — a save is a save of a whole computer, and loading one is asking for that computer back. It says so before it does it, because the switch rebuilds the machine and throws away whatever was running. A state whose machine the build cannot run (missing ROMs) is declined with a note saying so.

The Save States window labels any slot that belongs to another machine.

Two layouts exist, and they move independently:

| Family | File | Version |
|---|---|---|
| Apple II (//e, II Plus, //c) | `emulator_state.cpp` | 9 |
| Apple IIgs | `iigs_state.cpp` | 1 |

They share nothing after the header, so there is no reason for the two to move together.

## What Is Included in a Save State

A save state captures the complete machine state as a binary blob with a versioned header. The following data is serialized:

### Header
- Magic bytes (`A2ES`)
- State format version number
- The id of the machine that wrote it

### CPU State
- Registers: A, X, Y, Stack Pointer, Status (P), Program Counter
- Total cycle count

### Memory
- **Main RAM** -- Full 64 KB
- **Auxiliary RAM** -- Full 64 KB
- **Language Card RAM (Main)** -- 16 KB (Bank 1 + Bank 2 + High RAM)
- **Language Card RAM (Aux)** -- 16 KB (Bank 1 + Bank 2 + High RAM)

### Soft Switches
All Apple IIe soft switch states are packed into a single 32-bit word, including:
- Video switches: TEXT, MIXED, PAGE2, HIRES, 80COL, ALTCHARSET
- Memory switches: STORE80, RAMRD, RAMWRT, ALTZP
- ROM switches: INTCXROM, SLOTC3ROM, INTC8ROM
- Language Card: LCRAM, LCRAM2, LCWRITE, LCPREWRITE
- Annunciators: AN0-AN3
- I/O: IOUDIS

### Input State
- Keyboard latch and key-down flag
- Button states (3 buttons)

### Timing
- Last frame cycle count
- Audio samples generated count

### Disk Controller
- Motor state, selected drive, Q6/Q7 latches
- Phase states, data latch, sequencer state, bus data, LSS clock
- Per-drive: quarter-track position, full disk image data (with any in-memory modifications), and filename

### Audio
- Speaker toggle state

### Expansion Cards

**Every slot is recorded by card id with that card's own state**, so a state refits the cards it was saved with rather than assuming a fixed list: a Mockingboard, a Thunderclock, a Mouse Card, a Super Serial Card, a parallel card, a SoftCard, a //c's built-in ports and its IWM's mode register all come back.

A card's state is sized with 32 bits, because **a SmartPort card's state is its hard drive images** — which is why a state with two 32MB volumes attached is tens of megabytes rather than hundreds of kilobytes. Those bytes are written straight into the state rather than through a scratch buffer; copying them cost enough memory to stop the emulator outright.

### A IIgs's state

A IIgs's layout is its own from the header onwards:

- The **65816** — mode first, then the flags, then the registers, because setting the mode and the flags each force the widths the mode requires
- The **fast RAM**, refused on restore if a different amount is fitted
- The **Mega II's** RAM, language cards, switches, and every register of the memory controller
- The **devices**, each serializing itself — ADB (queues included), the clock chip (battery RAM, seconds, a transaction in flight), the SCC's two channels, and the Ensoniq (RAM, oscillators, registers, but not its output ring, which is the host's backlog)
- The machine's own counters, the **IWM**, the floppies, and the **SmartPort** with its images

## Screenshots and Previews

Each save state includes two screenshots captured at the time of saving:

- **Thumbnail** -- 140 x 96 pixels, displayed in the slot list
- **Preview** -- 560 x 384 pixels (full emulator resolution), shown on hover

Screenshots are stored as PNG data URLs alongside the state data in IndexedDB.

## Storage

All save state data is stored in the browser's **IndexedDB** under the database name `a2e-state-persistence`. The `emulatorState` object store holds the autosave and all 5 manual slots.

State data size varies depending on disk images. A typical save state with one disk inserted is approximately 200-400 KB. States with two disks or larger disk images will be correspondingly larger.

The autosave toggle setting is stored separately in **localStorage** under the key `a2e-autosave-state`.

See also: [[Getting-Started]], [[Disk-Drives]]
