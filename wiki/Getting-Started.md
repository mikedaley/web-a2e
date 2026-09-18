# Getting Started

This guide walks you through your first session with the Apple //e Emulator -- from opening the page to running software.

---

## Table of Contents

- [Requirements](#requirements)
- [Opening the Emulator](#opening-the-emulator)
- [Choosing a Machine](#choosing-a-machine)
- [Powering On](#powering-on)
- [The Toolbar](#the-toolbar)
- [Using the Keyboard](#using-the-keyboard)
- [Inserting a Disk](#inserting-a-disk)
- [Pasting Text](#pasting-text)
- [Copying Screen Text](#copying-screen-text)
- [Full-Page Mode](#full-page-mode)
- [Installing as a PWA](#installing-as-a-pwa)
- [Themes](#themes)
- [Getting Help](#getting-help)

---

## Requirements

The emulator runs in any modern browser that supports WebAssembly, WebGL, and the Web Audio API:

- Chrome / Edge (recommended)
- Firefox
- Safari

No downloads, plugins, or accounts are needed. The emulator loads entirely in the browser.

## Opening the Emulator

Navigate to the emulator URL. A loading spinner appears while the WebAssembly core initialises. Once ready, the screen shows the "no signal" pattern, indicating the virtual machine is powered off.

A floating reminder will point to the power button for first-time visitors.

## Choosing a Machine

The badge beside the Apple logo in the header names the machine you are running, and clicking it lets you change it. There are four:

| Machine | Notes |
|---|---|
| **Apple //e** (Enhanced) | The default |
| **Apple II Plus** | The machine most Apple II software was written on |
| **Apple //c** | No expansion sockets; a drive and two serial ports built in |
| **Apple IIgs** | 16-bit, Super Hi-Res, an Ensoniq, and GS/OS |

Switch before you start work: changing machine rebuilds the emulator, so anything in the drives or in memory is lost, just as it would be on a page reload. Your volume, character set and speed follow you across; your display settings, slot layout and save states are remembered per machine and come back when you return to it. Menu items for hardware the machine does not have are hidden rather than greyed out. See [[Machines]] for what differs between them, and [[Apple-IIgs]] for the one that is a different computer rather than a different set of numbers.

## Powering On

While the machine is off the screen shows a **NO SIGNAL** message. Click the **Power** button (the circle-with-a-line icon) in the toolbar -- the message on screen is part of the picture, not a control, so the toolbar button is the one that works. The button changes colour to indicate the machine is on, and the screen displays the Apple IIe boot sequence.

- If a **disk is inserted**, the Disk II controller will attempt to boot from it.
- If **no disk** is present, the screen shows a checksum test. A reminder will suggest pressing **Ctrl+Reset** to drop into Applesoft BASIC.

### Reset Options

| Button | Action |
|--------|--------|
| **Ctrl+Reset** | Warm reset -- preserves memory contents, re-enters the monitor or BASIC prompt |
| **Reboot** | Cold reset -- full power-cycle restart, clears all state |

Both buttons are in the toolbar next to the power button.

## The Toolbar

The toolbar along the top of the page provides access to all emulator functions:

| Control | Purpose |
|---------|---------|
| **Power** | Turn the emulated Apple //e on or off |
| **Ctrl+Reset** | Warm reset (preserves memory) |
| **Reboot** | Cold reset (full restart) |
| **File** | Auto-save toggle, save states manager |
| **View** | Theme, layout and auto-hide options, Disk Drives, SmartPort Drives, File Explorer, Display, Joystick/Paddles, Cursor Keys as Joystick, Expansion Slots, Serial Port, Printer, Print Browser |
| **Debug** | CPU Debugger, Soft Switches, Memory Map, Memory Browser, Heat Map, Stack Viewer, Zero Page Watch, Mockingboard, Mouse Card, Instruction Trace, BASIC Program |
| **Dev** | Applesoft BASIC editor, 6502 Assembler |
| **Full Page** | Expand the screen to fill the browser window |
| **Sound** | Volume slider, mute toggle, drive sounds toggle |
| **Help** | Documentation (F1), Release Notes, Check for Updates |

## Using the Keyboard

Click the emulator screen to give it keyboard focus. Once focused, key presses are sent directly to the emulated Apple //e. The C++ core translates browser key codes to Apple II key codes, handling Shift, Ctrl, and Caps Lock modifiers.

Key points:

- **Arrow keys** work as expected in software that supports them (the Apple //e has native arrow key support).
- **Backspace** sends the Apple II left-arrow (delete) key.
- **Tab** is captured by the emulator and not passed to the browser.
- **Escape** sends the Apple II ESC key.
- **Ctrl+key** combinations pass through to the emulator (e.g., Ctrl+C, Ctrl+D for DOS commands).
- **Browser shortcuts** like Ctrl+R (reload) are not intercepted and work normally.

For the full shortcut reference see [[Keyboard-Shortcuts]].

## Inserting a Disk

1. Open **View > Disk Drives** from the toolbar (or use the window switcher with Ctrl+\`).
2. The Disk Drives window shows Drive 1 and Drive 2.
3. **Drag and drop** a disk image file onto a drive slot, or click the drive's **Load** button to browse for a file.
4. Supported formats: **DSK**, **DO**, **PO**, **NIB**, and **WOZ**.
5. If the emulator is running, the disk will begin spinning and the Apple //e will detect it.

To boot from a newly inserted disk, press **Ctrl+Reset** or click **Reboot**.

See [[Disk-Drives]] for full details on disk management, write protection, surface visualisation, and drive sounds.

## Pasting Text

Press **Ctrl+V** while the emulator screen has focus to paste text from the clipboard. The emulator:

1. Reads the clipboard contents.
2. Converts each character to its Apple II key equivalent.
3. Feeds the characters into the emulated keyboard at 8x speed (the emulation temporarily accelerates to process paste quickly).
4. Automatically waits for the keyboard-ready flag between characters to avoid dropped input.

This is useful for entering BASIC programs or long commands. Paste is cancelled automatically if you press Ctrl+Reset or Reboot during the operation.

## Copying Screen Text

You can select and copy text directly from the Apple //e screen:

1. **Click and drag** on the emulator screen to highlight text.
2. The selection is shown as a coloured overlay.
3. Press **Ctrl+C** (or Cmd+C on Mac) to copy the selected text to the clipboard.
4. The selection works in both 40-column and 80-column text modes.

## Full-Page Mode

Click the **Full Page** button (expand icon) in the toolbar to fill the entire browser window with the emulator screen. This hides the toolbar and footer for a distraction-free experience.

While in full-page mode:

- Move the mouse to the **top of the screen** to reveal a floating toolbar with Power, Ctrl+Reset, Reboot, and Exit controls.
- Press **Ctrl+Escape** to exit full-page mode and return to the normal view.

## Installing as a PWA

The emulator can be installed as a Progressive Web App for a more native experience:

1. Open the emulator in Chrome or Edge.
2. Click the install icon in the browser address bar (or use the browser menu).
3. The emulator will open in its own window without browser chrome.

The PWA manifest provides the app title "Apple //e" with appropriate icons.

## Themes

The emulator supports three themes, selectable from **View > Theme**:

| Theme | Description |
|-------|-------------|
| **Light** | Light background with dark text for toolbar and windows |
| **Dark** | Dark background (default) suited for extended use |
| **System** | Automatically follows your operating system's light/dark preference |

The theme applies to the toolbar, all debug windows, dialogs, and settings panels. The emulator screen itself is unaffected -- it always renders authentic Apple II colours.

## Sharing a Link

The emulator can open with a disk already inserted, which makes a program shareable as a single URL:

```
?disk=https://example.com/demo.dsk
```

See [[URL-Parameters]] for every parameter and the rules around them.

## Getting Help

- Press **F1** at any time to open the built-in Documentation window.
- Open **Help > Release Notes** to see what has changed in the current version.
- Open **Help > Check for Updates** to see if a newer version is available.
- The footer bar at the bottom of the page also links to Release Notes and shows the F1 hint.
