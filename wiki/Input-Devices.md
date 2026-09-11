# Input Devices

The emulator supports keyboard, joystick/paddle or Joyport, and mouse input, matching the input capabilities of a real Apple II. It also provides text selection and clipboard paste features that bridge the gap between the host system and the emulated machine.

## Table of Contents

- [Keyboard](#keyboard)
- [Text Paste](#text-paste)
- [Text Selection and Copy](#text-selection-and-copy)
- [The Game Port](#the-game-port)
- [Joystick and Paddles](#joystick-and-paddles)
- [Sirius Joyport](#sirius-joyport)
- [Mouse](#mouse)
- [Mobile Input](#mobile-input)

## Keyboard

### Key Mapping

The emulator translates browser keycodes to Apple II ASCII codes in real time. All key translation is handled by the C++ core, supporting the full US keyboard layout.

**Modifier keys:**

| Host Key | Apple II Function |
|----------|-------------------|
| Alt (Option) | Open Apple button |
| Meta (Cmd/Win) | Closed Apple button |
| Shift | Shift (uppercase, shifted symbols) |
| Ctrl | Control (generates control characters Ctrl+A through Ctrl+Z) |
| Caps Lock | Uppercase letters (matches Apple II behavior) |

**Special keys:**

| Host Key | Apple II Key |
|----------|-------------|
| Enter | Return ($0D) |
| Backspace | Delete / Left arrow ($08) |
| Escape | Escape ($1B) |
| Tab | Tab ($09) |
| Space | Space ($20) |
| Left Arrow | Left arrow ($08) |
| Right Arrow | Right arrow ($15) |
| Up Arrow | Up arrow ($0B) |
| Down Arrow | Down arrow ($0A) |

**Letters and numbers:**

- Letters A-Z are translated to lowercase by default and converted to uppercase when Shift or Caps Lock is active
- Number keys 0-9 map directly to their ASCII equivalents
- All standard US punctuation keys are supported, including their shifted variants

### Focus

The emulator captures keyboard input when the screen canvas has focus. Click on the emulator screen to give it focus. When focus is on other UI elements (debug windows, menus, etc.), keyboard input goes to those elements instead of the emulator.

### Browser Shortcut Passthrough

The emulator prevents default browser behavior for keys that would interfere with the emulation (Backspace, Tab, Space, arrow keys) when the canvas has focus. Standard browser shortcuts like Ctrl+R (refresh) are allowed through.

## Text Paste

You can paste text from the clipboard into the emulator using **Ctrl+V** (or **Cmd+V** on macOS).

### How Paste Works

Pasted text goes into a **type-ahead buffer inside the emulated machine**, not a queue in the browser. The whole text crosses in one go; the machine then pulls characters out of the buffer at its own pace, so a character can never be overwritten before the program has read it. The emulation speed is boosted to 8x for the duration and handed back to your chosen speed afterwards.

Two details are load-bearing:

- A character never enters the keyboard latch while the strobe is still set, which is what makes the paste lossless.
- A character becomes available a short interval (~15ms of emulated time) after the previous one was taken, and a longer one (~150ms) after a carriage return.

That gap is not cosmetic. Clearing the keyboard strobe twice is how an Apple II **flushes** the keyboard, and the ROM, DOS and countless programs do it before settling down to wait for input. A person typing leaves nothing pending for a flush to eat; an emulator that refilled the latch the instant the strobe cleared handed each flush a fresh character to throw away, which showed up as a paste arriving with roughly every second character missing — in some programs and not others. The carriage-return gap is longer because the machine has a line to digest afterwards: Applesoft tokenises it, and DOS or BASIC.SYSTEM run their command parsers, with flushes at the end of that work.

Because the gaps are measured in emulated time, the 8x boost shortens the real wait proportionally — a 2400-character program pastes in around seven seconds.

### Programmatic Text Input

The same buffer is used by the BASIC and Assembler windows, by mobile typing (an on-screen keyboard can deliver several characters in one event), and by the agent's `typeKeyboard` tool. `{token}` sequences such as `{ctrl-c}`, `{left}` and `{chr:4}` are resolved before the text is pushed and sent as single key codes.

### Canceling a Paste

A paste in progress can be cancelled. The buffer is discarded and the emulation speed restored immediately. A reset also discards it — the buffer is host state, and is deliberately not written into save states.

## Text Selection and Copy

The emulator supports selecting and copying text directly from the Apple II screen when it is in text mode (40-column or 80-column).

### Selecting Text

1. Click and drag on the emulator screen to select a range of characters
2. The selection is highlighted with a semi-transparent overlay
3. Selection works correctly with CRT shader effects (curvature, overscan, margins) -- the mouse position is mapped through the same transforms as the display

### Copying Text

- Press **Ctrl+C** (or **Cmd+C** on macOS) to copy the selected text to the clipboard
- A brief green flash confirms the copy
- Right-click on a selection to open a context menu with **Copy** and **Select All** options

### Select All

Use the right-click context menu's **Select All** option to select the entire 24-line screen. This works in both 40-column and 80-column modes.

### Clearing a Selection

- Press **Escape** to clear the current selection
- Click without dragging to clear the selection
- Selections are only available in text mode -- switching to a graphics mode clears any active selection

### How Text is Read

The C++ core handles reading screen memory and converting Apple II character codes to Unicode text. The selection coordinates (row/column) are passed to the WASM function `_readScreenText()`, which reads the appropriate page of screen memory (main or aux for 80-column mode) and decodes the characters.

## The Game Port

The Apple II's 16-pin game connector takes **one** device, and the **Game port** selector at the top of the Joystick window chooses which:

| Device | What it is |
|--------|-----------|
| **Apple Joystick** | The resistive joystick and paddles the machine shipped with — two potentiometers the machine times an RC discharge against, plus two buttons. The default. |
| **Sirius Joyport** | Sirius Software's 1981 adapter: two Atari CX40-style digital sticks read through the annunciators. |

They cannot coexist, because the Joyport drives the same pushbutton inputs the Apple keys do and drives them inverted — so this is a choice, exactly as it was on the desk. The selection is remembered, and survives a reset and a change of machine.

## Joystick and Paddles

The Apple IIe supports two analog paddles (or one joystick with two axes) and up to three buttons. The emulator provides a virtual joystick window for mouse-based control.

### Virtual Joystick Window

Open the **Joystick** window from **View > Joystick / Paddles**. The window contains:

- **Joystick area** -- A square pad with a draggable knob. Drag the knob to set the X and Y axis values (0-255 range, with 128 at center).
- **X/Y values** -- Numeric readout of the current paddle values
- **Button 0 and Button 1** -- Click and hold to press the corresponding Apple II button
- **Center button** -- Resets the knob to the center position (128, 128)

### Knob Behavior

- Click and drag the knob to move it
- Click anywhere in the joystick area to jump the knob to that position
- When you release the mouse button, the knob snaps back to center
- Paddle values update in real time as you drag

### Paddle Values

The joystick knob position maps to Apple II paddle values:

| Position | Paddle Value |
|----------|-------------|
| Top-left | X=0, Y=0 |
| Center | X=128, Y=128 |
| Bottom-right | X=255, Y=255 |

The paddle values are sent to the emulator via the `_setPaddleValue()` WASM function and are read by Apple II software through the standard paddle I/O addresses (`$C064`-`$C067`).

### Game Controllers

Physical controllers are supported through the Gamepad API. The left stick maps to the two paddle axes, and buttons A and B map to Apple II buttons 0 and 1. A configurable **deadzone** (default 0.1) stops a drifting stick from registering as constant deflection; it is stored under `gamepad-deadzone`.

### Cursor Keys as Joystick

The arrow keys can drive the joystick, giving full deflection (0 or 255 per axis) in each direction -- enough for the many games that only test for hard left/right/up/down.

Crucially, **the arrows keep reaching the Apple's keyboard as normal**. ProDOS selectors, catalog menus and BASIC line editing all keep working while the toggle is on, because the toggle decides whether the arrows *also* move the joystick, not whether they still work as keys.

There are two places to turn it on, and they stay in step with each other:

- the **JOY** switch in the Monitor window's title bar, and
- **View > Cursor Keys as Joystick** in the menu.

The menu item exists because the Monitor title bar is not on screen in the Play, Code and Debug layouts -- without it the setting would be unreachable there. When the mode is active a **CURSOR KEYS** chip appears in the Monitor title bar. The setting persists under `joystick-cursor-keys`.

## Sirius Joyport

Choose **Sirius Joyport** from the Game port selector and the Joystick window swaps its paddle knob for a pair of digital sticks, each with a D-pad and a fire button you can hold with the mouse. A readout beside each stick names what is currently closed.

Most games from Sirius Software — and a number of others — offer **"Apple Joystick"** or **"Joyport"** on their title screen. Pick the one that matches this selector.

### How the Joyport is read

A digital joystick has five switches and the game connector has three pushbutton inputs, so the Joyport multiplexes. Annunciator 0 selects the stick, annunciator 1 selects the axis pair:

| AN0 | AN1 | PB0 (`$C061`) | PB1 (`$C062`) | PB2 (`$C063`) |
|-----|-----|---------------|---------------|---------------|
| off | off | fire 1 | left 1 | right 1 |
| off | on | fire 1 | up 1 | down 1 |
| on | off | fire 2 | left 2 | right 2 |
| on | on | fire 2 | up 2 | down 2 |

**The switches are active low.** A line reads *high* while nothing is pressed — the opposite of a pushbutton — which is why the Joyport cannot share PB0 and PB1 with the Open and Closed Apple keys, and why it is a device choice rather than an addition.

### Game controllers

With the Joyport selected, either the D-pad or the left stick closes the four direction switches, and A or B is fire. The emulator tracks **every** connected controller rather than only the first:

- Two controllers connected: one drives stick 1, the other stick 2.
- One controller connected: it drives **both** sticks, so a one-player game that happens to read stick 2 still plays.

An impossible pair — left and right at once, or up and down — is dropped rather than sent, because a real gate cannot close both.

The **Cursor Keys as Joystick** toggle drives stick 1 when the Joyport is selected.

### One deliberate departure from the hardware

A Joyport idles PB0 and PB1 high, and on a //e those two lines **are** the Open and Closed Apple keys. The //e's reset routine reads them to decide what to do — Open Apple asks for a cold boot, Closed Apple runs the self test — so a real //e with a Joyport fitted ran the self test on every reset and never reached a prompt. That is faithful, and it is why the Joyport belongs to the II and II+ era.

The emulator has the Joyport let go of those two lines for about 50ms after a reset: long enough for the ROM to look, far too short for a game to have asked about the stick. PB2 is never released, because no Apple key is wired to it, and a *closed* switch still reads low inside the window, so a fire button held through a reset is not lost.

## Mouse

The emulator provides mouse input to compatible software using the browser's Pointer Lock API for relative movement tracking. How that reaches the machine depends on which machine it is.

### Enabling the Mouse

On a **//e** or a **II Plus**, the Apple Mouse Interface Card must be installed in an expansion slot (typically slot 4). See [[Expansion-Slots]] for configuration.

A **//c** has a mouse already: the connector is on the back panel and the mouse is wired into the IOU rather than into a card, so there is nothing to install and nothing to remove. See [[Machines]].

### Engaging Mouse Capture

To start using the mouse with the emulator:

1. Hold **Alt** (Option) and **click** on the emulator screen
2. The browser enters pointer lock mode and mouse movement is captured
3. Mouse movement deltas are sent to the emulated mouse card
4. Left mouse button clicks are forwarded as Apple mouse button presses

### Releasing Mouse Capture

Press **Escape** to exit pointer lock mode. This is standard browser behavior for the Pointer Lock API.

### Mouse Movement

While pointer lock is active, the browser sends relative movement deltas (not absolute positions). These deltas are forwarded to the WASM emulator via `_mouseMove(dx, dy)`, and the mouse firmware translates them into Apple II mouse coordinates through the standard screen-hole protocol.

The two machines get there by different routes. A card is told a delta and works out the rest for itself. A //c's IOU counts nothing: every unit of travel is one interrupt, and its firmware reads which way the mouse went and adds one to a position it keeps in the screen holes — so ten units of movement is ten interrupts, delivered one at a time as the handler services them.

## Mobile Input

On mobile and touch devices, the emulator provides a modified input experience:

### Mobile Keyboard

When a mobile device is detected (touch capability + mobile user agent or small screen), the emulator creates a hidden text input field. Tapping the emulator screen focuses this hidden input, which triggers the on-screen keyboard.

- Regular character input is captured from the hidden input field and forwarded to the emulator
- Special keys (Backspace, Enter, Escape, Tab) are handled through keydown events
- Autocomplete, autocapitalize, autocorrect, and spellcheck are all disabled to prevent interference

### Mobile Detection

The emulator detects mobile devices using a combination of:
- Touch capability (`ontouchstart` or `maxTouchPoints`)
- Mobile user agent string matching
- Small screen width (800px or less)

See also: [[Keyboard-Shortcuts]], [[Expansion-Slots]], [[Machines]]
