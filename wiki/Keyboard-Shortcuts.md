# Keyboard Shortcuts

Complete keyboard shortcut reference for the Apple //e Emulator.

---

## Table of Contents

- [Apple IIe Key Mapping](#apple-iie-key-mapping)
- [Special Keys](#special-keys)
- [Apple II Control Key Combinations](#apple-ii-control-key-combinations)
- [Emulator Shortcuts](#emulator-shortcuts)
- [Debugger Shortcuts](#debugger-shortcuts)
- [Window Management](#window-management)
- [Assembler Editor](#assembler-editor)
- [Mouse Capture](#mouse-capture)
- [Text Selection and Copy](#text-selection-and-copy)
- [Notes](#notes)

---

## Apple IIe Key Mapping

The emulator translates modern keyboard input to Apple II key codes. Standard alphanumeric keys and symbols map directly (US layout); the table is the same on every machine except where a row says otherwise.

| Your Keyboard | Apple Key | Notes |
|---------------|-----------|-------|
| Enter | Return | Confirm input, run commands |
| Backspace | Left arrow ($08) | Deletes to the left in Applesoft, which is what its line editor expects |
| Delete (forward delete) | Delete ($7F) | The key marked DELETE on a //e, //c and IIgs; ProDOS editors and GS/OS use it |
| Escape | ESC | Cancel, exit menus |
| Tab | Tab | Tab character |
| Arrow keys | Arrow keys | Cursor movement, game controls |
| Space | Space | Space character |
| Numeric keypad | Digits, `* + - . /` and Return | Types what the number row types; a IIgs also flags the key as a keypad key in `$C025` |

## Special Keys

| Your Keyboard | Apple Key | Notes |
|---------------|-----------|-------|
| Left Alt / Option | Open Apple | Also joystick button 0 (`$C061`). On a II+, which has no Apple keys, it is simply pushbutton 0 |
| Right Alt / Option | Closed Apple (Solid Apple) | Also joystick button 1 (`$C062`). On a II+, pushbutton 1 |
| ⌘ | Open Apple, **on the IIgs** | See below. On the other machines ⌘ and the Windows key are left to the browser and press nothing |
| Ctrl | Control | Control key modifier |
| Shift | Shift | Shift modifier. On a //c it also pulls PB2 (`$C063`) low, as the real machine's keyboard does |
| Caps Lock | Caps Lock | Tracked and sent to the emulator core; a IIgs reports it in `$C025` |
| Ctrl+Pause/Break | Ctrl+Reset | Warm reset, for keyboards that have the key; the toolbar button does the same |

### Which key is Open Apple depends on the machine

On the 8-bit machines the two Option keys are the Apple keys — left Open, right Closed — and ⌘ is left to the browser.

A IIgs's keyboard is a Mac's. ⌘ *is* its Open Apple and Option its Closed Apple, and GS/OS drives its menus with ⌘-letter, so on that machine the emulator takes ⌘ while it has the keyboard.

**View > ⌘ as Open Apple** is the switch. It is remembered per machine and is on by default for the IIgs only. With it on, every ⌘ combination is intercepted — though a browser still keeps ⌘W, ⌘Q and a few others for itself, which is why this is a choice rather than a rule. Because macOS delivers no key-up for a key released while ⌘ is held, keys pressed under ⌘ are released when ⌘ is.

The Apple keys deliberately do not assert "any key down": they are separate lines on real hardware, not keys in the matrix.

### What differs per machine

- **Apple II Plus:** the keyboard has no lower case, so letters arrive as capitals whatever Shift or Caps Lock say — Applesoft on a II+ rejects a lower-case keyword. It has no Apple keys either: the Alt keys still work the two game port pushbuttons, which is all `$C061` and `$C062` are on that machine.
- **Apple //e:** as the table above. The Enhanced //e modelled here does not have the shift-key modification, so Shift does not reach `$C063`.
- **Apple //c:** as the //e, plus Shift on `$C063`, low when held, sharing the line with the mouse button.
- **Apple IIgs:** ⌘ is Open Apple by default, the modifier keys are reported in `$C025` (Shift, Control, Caps Lock, Open Apple, Option, and whether the key was on the keypad), and Control-Open-Apple-Escape reaches the machine, though the Control Panel does not yet answer it.

## Apple II Control Key Combinations

These are Apple IIe keyboard combinations, processed by the emulated machine, not the browser:

| Combination | Function |
|-------------|----------|
| Ctrl+C | Break -- stop a running BASIC program |
| Ctrl+S | Pause screen output (Ctrl+Q to resume) |
| Ctrl+Q | Resume output after Ctrl+S pause |
| Ctrl+G | Bell (beep sound) |
| Ctrl+Reset | Warm reset -- preserves memory, returns to BASIC or monitor. The toolbar button, or Ctrl+Pause/Break |
| Ctrl+[ , Ctrl+2 etc. | Control with punctuation gives the codes below $20: Ctrl+[ is Escape, Ctrl+\\ is $1C, Ctrl+] is $1D. As on a //e, Ctrl+2 is Ctrl+@ (NUL), Ctrl+6 is Ctrl+^ ($1E) and Ctrl+- is Ctrl+_ ($1F), with or without Shift |
| Ctrl+Open Apple+Reset | Cold reset on real hardware (use the **Reboot** button instead) |

## Emulator Shortcuts

These shortcuts are handled by the emulator's JavaScript layer, not the Apple IIe:

| Shortcut | Action |
|----------|--------|
| F1 | Open / close the Help & Documentation window |
| Ctrl+Escape | Exit full-page mode and return to the normal view |
| Ctrl+V | Paste clipboard text into the emulator at accelerated speed |
| Ctrl+\` | Open the Window Switcher overlay |
| Option+Tab | Cycle focus to the next open window |
| Option+Shift+Tab | Cycle focus to the previous open window |

The arrow keys can additionally drive the joystick -- see the Cursor Keys toggle in [[Input-Devices]]. They keep working as keys while it is on.

## Debugger Shortcuts

These shortcuts control the CPU Debugger when the emulator is paused at a breakpoint or in single-step mode:

| Shortcut | Action | Description |
|----------|--------|-------------|
| F5 | Run / Continue | Resume execution until the next breakpoint |
| F10 | Step Over | Execute one instruction, stepping over JSR subroutine calls |
| F11 | Step Into | Execute a single instruction, following into subroutines |
| Shift+F11 | Step Out | Continue execution until the current subroutine returns (RTS/RTI) |

These shortcuts work globally -- you do not need to have the CPU Debugger window focused.

## Window Management

| Shortcut | Action |
|----------|--------|
| Ctrl+\` | Open the Window Switcher (shows all available windows organised by category) |
| Option+Tab | Cycle to the next visible window and bring it to focus |
| Option+Shift+Tab | Cycle to the previous visible window |

The Window Switcher displays windows in five categories:

- **System** -- Screen, Disk Drives, Save States
- **Hardware** -- Display Settings, Joystick, Expansion Slots, Mockingboard, Mouse Card
- **Debug** -- CPU Debugger, Rule Builder, Soft Switches, Memory Browser, Memory Heat Map, Memory Map, Stack Viewer, Zero Page Watch
- **Dev** -- Applesoft BASIC, Assembler
- **Help** -- Documentation, Release Notes

Click a window name in the switcher or use Tab cycling to navigate between open windows.

## Assembler Editor

These shortcuts are active when editing assembly source in the Assembler Editor window.

### File Operations

| Shortcut | Action |
|----------|--------|
| Ctrl+N (Cmd+N) | New file |
| Ctrl+O (Cmd+O) | Open file |
| Ctrl+S (Cmd+S) | Save file |

### Editing

| Shortcut | Action |
|----------|--------|
| Ctrl+Enter (Cmd+Enter) | Assemble the current source |
| Ctrl+/ (Cmd+/) | Toggle comment on the current line |
| Ctrl+D (Cmd+D) | Duplicate the current line |
| Tab | Move cursor to the next column (label / opcode / operand / comment) |
| Shift+Tab | Move cursor to the previous column |
| Enter | Smart indent (auto-aligns to the appropriate column on new line) |

### Breakpoints and Panels

| Shortcut | Action |
|----------|--------|
| F9 | Toggle breakpoint on the current line |
| F2 | Toggle the ROM reference panel |

## Mouse Capture

| Shortcut | Action |
|----------|--------|
| Alt+Click on screen | Engage pointer lock for Apple Mouse Card input |
| Escape | Release pointer lock (standard browser behavior) |

Mouse capture requires the Apple Mouse Card to be installed in an expansion slot. See [[Expansion-Slots]] for card configuration.

## Text Selection and Copy

| Action | How |
|--------|-----|
| Select text | Click and drag on the emulator screen |
| Copy selected text | Ctrl+C (Cmd+C on Mac) while text is selected |
| Paste text | Ctrl+V (Cmd+V on Mac) when the screen has focus |

Text selection works in both 40-column and 80-column text modes. The selection overlay appears directly on the canvas.

## Notes

- **Browser shortcuts are preserved** -- Ctrl+R (reload), Ctrl+T (new tab), and other standard browser shortcuts are not intercepted by the emulator and work normally.
- **Focus matters** -- Keyboard input is only sent to the Apple IIe when the emulator screen canvas has focus. Click the screen to focus it. If a debug window or text input field is focused, keys go to that element instead.
- **Mobile devices** -- On touch devices, tapping the screen opens the on-screen keyboard via a hidden input element.
- **Key codes are sent raw** -- The JavaScript layer sends raw key codes to the C++ core, which handles the full Apple IIe keyboard translation including modifier state.

For general usage see [[Getting-Started]]. For debugger details see [[Debugger]].
