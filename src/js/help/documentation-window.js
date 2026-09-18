/*
 * documentation-window.js - Help and documentation window
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/**
 * DocumentationWindow - Moveable help & documentation window
 * Extends BaseWindow for drag/resize functionality
 */

import { BaseWindow } from "../windows/base-window.js";

export class DocumentationWindow extends BaseWindow {
  constructor() {
    super({
      id: "documentation-window",
      title: "Help & Documentation",
      minWidth: 500,
      minHeight: 400,
      defaultWidth: 750,
      defaultHeight: 550,
    });

    this.navButtons = null;
    this.sections = null;
  }

  /**
   * Override to add custom class for documentation styling
   */
  create() {
    super.create();
    this.element.classList.add("documentation-window");

    // Set up F1 keyboard shortcut
    document.addEventListener("keydown", (e) => {
      if (e.key === "F1") {
        e.preventDefault();
        this.toggle();
      }
    });

    // Set up help button (inside help menu dropdown)
    const helpButton = document.getElementById("btn-help");
    if (helpButton) {
      helpButton.addEventListener("click", () => {
        this.toggle();
        // Close the help menu dropdown
        const menuContainer = helpButton.closest(".header-menu-container");
        if (menuContainer) menuContainer.classList.remove("open");
      });
    }
  }

  /**
   * Render the documentation content
   */
  renderContent() {
    return `
      <div class="documentation-layout">
        <nav class="documentation-nav">
          <button data-section="getting-started" class="active">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>
              <polyline points="9 22 9 12 15 12 15 22"/>
            </svg>
            Getting Started
          </button>
          <button data-section="install">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
              <polyline points="7 10 12 15 17 10"/>
              <line x1="12" y1="15" x2="12" y2="3"/>
            </svg>
            Install App
          </button>
          <button data-section="machines">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="3" y="3" width="18" height="13" rx="2"/>
              <line x1="7" y1="20" x2="17" y2="20"/>
              <line x1="12" y1="16" x2="12" y2="20"/>
            </svg>
            Machines
          </button>
          <button data-section="keyboard">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="2" y="4" width="20" height="16" rx="2"/>
              <line x1="6" y1="8" x2="6.01" y2="8"/>
              <line x1="10" y1="8" x2="10.01" y2="8"/>
              <line x1="14" y1="8" x2="14.01" y2="8"/>
              <line x1="18" y1="8" x2="18.01" y2="8"/>
              <line x1="8" y1="12" x2="8.01" y2="12"/>
              <line x1="12" y1="12" x2="12.01" y2="12"/>
              <line x1="16" y1="12" x2="16.01" y2="12"/>
              <line x1="7" y1="16" x2="17" y2="16"/>
            </svg>
            Keyboard
          </button>
          <button data-section="display">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="2" y="3" width="20" height="14" rx="2"/>
              <line x1="8" y1="21" x2="16" y2="21"/>
              <line x1="12" y1="17" x2="12" y2="21"/>
            </svg>
            Display
          </button>
          <button data-section="workspace">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="3" y="3" width="18" height="18" rx="2"/>
              <line x1="3" y1="9" x2="21" y2="9"/>
              <line x1="9" y1="21" x2="9" y2="9"/>
            </svg>
            Workspace
          </button>
          <button data-section="disks">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="2" y="6" width="20" height="12" rx="2"/>
              <rect x="5" y="9" width="10" height="1.5" rx="0.5"/>
              <circle cx="18" cy="12" r="1.5"/>
            </svg>
            Disk Drives
          </button>
          <button data-section="smartport">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="4" y="2" width="16" height="20" rx="2"/>
              <line x1="8" y1="6" x2="16" y2="6"/>
              <circle cx="12" cy="14" r="3"/>
            </svg>
            SmartPort
          </button>
          <button data-section="expansion">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect x="3" y="4" width="18" height="12" rx="1"/>
              <line x1="7" y1="20" x2="7" y2="16"/>
              <line x1="12" y1="20" x2="12" y2="16"/>
              <line x1="17" y1="20" x2="17" y2="16"/>
              <line x1="7" y1="8" x2="7" y2="12"/>
              <line x1="12" y1="8" x2="12" y2="12"/>
            </svg>
            Expansion
          </button>
          <button data-section="file-explorer">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>
            </svg>
            File Explorer
          </button>
          <button data-section="state">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/>
              <polyline points="17 21 17 13 7 13 7 21"/>
              <polyline points="7 3 7 8 15 8"/>
            </svg>
            State
          </button>
          <button data-section="sound">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M11 5L6 9H2v6h4l5 4V5z"/>
              <path d="M19.07 4.93a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07"/>
            </svg>
            Sound
          </button>
          <button data-section="printer">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <polyline points="6 9 6 2 18 2 18 9"/>
              <path d="M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2"/>
              <rect x="6" y="14" width="12" height="8"/>
            </svg>
            Printer
          </button>
          <button data-section="debug">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <circle cx="12" cy="12" r="10"/>
              <path d="M12 6v2M12 16v2M6 12h2M16 12h2"/>
              <circle cx="12" cy="12" r="3"/>
            </svg>
            Debug Tools
          </button>
          <button data-section="dev">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <polyline points="16 18 22 12 16 6"/>
              <polyline points="8 6 2 12 8 18"/>
            </svg>
            Dev Tools
          </button>
          <button data-section="agent">
            <svg viewBox="0 0 24 24" fill="currentColor">
              <path d="M12 2l1.5 4.5L18 8l-4.5 1.5L12 14l-1.5-4.5L6 8l4.5-1.5L12 2zM6 16l.75 2.25L9 19l-2.25.75L6 22l-.75-2.25L3 19l2.25-.75L6 16zM18 16l.75 2.25L21 19l-2.25.75L18 22l-.75-2.25L15 19l2.25-.75L18 16z"/>
            </svg>
            AI Agent
          </button>
          <button data-section="tips">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M9.663 17h4.673M12 3v1m6.364 1.636l-.707.707M21 12h-1M4 12H3m3.343-5.657l-.707-.707m2.828 9.9a5 5 0 117.072 0l-.548.547A3.374 3.374 0 0014 18.469V19a2 2 0 11-4 0v-.531c0-.895-.356-1.754-.988-2.386l-.548-.547z"/>
            </svg>
            Tips
          </button>
        </nav>
        <div class="documentation-body">
          ${this.renderSections()}
        </div>
      </div>
    `;
  }

  /**
   * Render all documentation sections
   */
  renderSections() {
    return `
      <!-- Getting Started Section -->
      <section id="doc-getting-started" class="documentation-section active">
        <h3>Getting Started</h3>
        <p>Welcome to the Apple //e Emulator! This web-based emulator faithfully recreates the Apple //e Enhanced computer from 1983, allowing you to run classic Apple II software directly in your browser.</p>

        <h4>Quick Start</h4>
        <ol class="quick-start-list">
          <li>Click the <strong>Power</strong> button to turn on the emulator</li>
          <li>Click on the screen to give it keyboard focus</li>
          <li>Insert a disk image using the <strong>Insert</strong> button on either drive</li>
          <li>Type <kbd>PR#6</kbd> and press <kbd>Return</kbd> to boot from drive 1</li>
        </ol>

        <h4>What is the Apple //e?</h4>
        <p>The Apple //e (Enhanced) was Apple's most popular Apple II model, released in 1983. It featured 128KB of RAM with auxiliary memory, 80-column text display, double hi-res graphics (560x192), and ran thousands of educational, productivity, and entertainment programs.</p>
        <p>It is not the only machine here. The badge in the header names the machine you are running, and clicking it lets you switch to an <strong>Apple II Plus</strong> &mdash; see <strong>Machines</strong> for what differs and what the II+ needs before it will start.</p>

        <h4>Emulated Hardware</h4>
        <ul>
          <li><strong>CPU:</strong> 65C02 processor at 1.023 MHz (cycle-accurate)</li>
          <li><strong>Memory:</strong> 128KB RAM (64KB main + 64KB auxiliary)</li>
          <li><strong>Video:</strong> All Apple //e display modes including Double Hi-Res</li>
          <li><strong>Storage:</strong> Two Disk II floppy drives, SmartPort hard drives</li>
          <li><strong>Audio:</strong> Speaker with accurate timing, Mockingboard (dual AY-3-8910)</li>
          <li><strong>Expansion:</strong> Disk II, Mockingboard, Mouse Card, Thunderclock Plus, Super Serial Card, Parallel Card, Microsoft Z-80 SoftCard, SmartPort, and a No-Slot Clock</li>
          <li><strong>Peripherals:</strong> Virtual dot-matrix printer, joystick / paddles or a Sirius Joyport, and physical game controllers</li>
          <li><strong>ROM:</strong> Apple //e Enhanced ROM set (the Apple II Plus supplies its own &mdash; see Machines)</li>
        </ul>

        <div class="info-box tip">
          <p><strong>Tip:</strong> Press <kbd>F1</kbd> at any time to open this help window. All windows can be moved and resized.</p>
        </div>
      </section>

      <!-- Install App Section -->
      <section id="doc-install" class="documentation-section">
        <h3>Install as App</h3>
        <p>This emulator is a Progressive Web App (PWA) that can be installed on your device for offline use and a native app-like experience.</p>

        <h4>Chrome / Edge (Desktop)</h4>
        <ol class="quick-start-list">
          <li>Click the <strong>install icon</strong> in the address bar (right side)</li>
          <li>Or click the <strong>three dots menu</strong> (⋮) and select "Install Apple //e Emulator"</li>
          <li>Click <strong>Install</strong> in the dialog</li>
          <li>The app will open in its own window and appear in your applications</li>
        </ol>

        <h4>Chrome (Android)</h4>
        <ol class="quick-start-list">
          <li>Tap the <strong>three dots menu</strong> (⋮)</li>
          <li>Select <strong>"Add to Home screen"</strong> or <strong>"Install app"</strong></li>
          <li>Tap <strong>Install</strong> to confirm</li>
          <li>The app icon will appear on your home screen</li>
        </ol>

        <h4>Safari (iOS / macOS)</h4>
        <ol class="quick-start-list">
          <li>Tap the <strong>Share button</strong> (square with arrow)</li>
          <li>Scroll down and tap <strong>"Add to Home Screen"</strong></li>
          <li>Tap <strong>Add</strong> to confirm</li>
          <li>The app will appear on your home screen</li>
        </ol>

        <h4>Firefox</h4>
        <p>Firefox supports PWAs on Android. On desktop, you can bookmark the page for quick access, though full PWA installation is not yet supported.</p>

        <h4>Benefits of Installing</h4>
        <ul>
          <li><strong>Offline Use:</strong> Run the emulator without an internet connection</li>
          <li><strong>Own Window:</strong> Opens in a dedicated window without browser UI</li>
          <li><strong>Quick Access:</strong> Launch from your taskbar, dock, or home screen</li>
          <li><strong>Auto Updates:</strong> Automatically receives updates when online</li>
          <li><strong>Full Screen:</strong> Better fullscreen experience</li>
        </ul>

        <h4>Automatic Updates</h4>
        <p>The emulator automatically checks for updates when you open it while connected to the internet. When a new version is available, you'll see a brief notification and the page will refresh with the latest version.</p>
        <p>You can also manually check for updates using the <strong>refresh button</strong> in the toolbar, which clears the cache and reloads the latest version.</p>

        <div class="info-box info">
          <p><strong>Note:</strong> Your saved state, disk images, and settings are preserved across updates.</p>
        </div>
      </section>

      <!-- Machines Section -->
      <section id="doc-machines" class="documentation-section">
        <h3>Machines</h3>
        <p>The emulator runs one machine at a time, and the badge in the header names it. Click the badge to choose a different one.</p>

        <h4>Apple //e</h4>
        <p>The 1983 Enhanced //e: a 65C02 at 1.023 MHz, 128KB of RAM as 64KB main plus 64KB auxiliary, 80-column text, double hi-res, and a built-in 80-column card locked into slot 3. This is the default and needs nothing extra.</p>

        <h4>Apple II Plus</h4>
        <p>The 1979 machine most of the software you remember was written on. Its differences from the //e are real, not cosmetic:</p>
        <ul>
          <li><strong>An NMOS 6502</strong> rather than a 65C02, so the 65C02-only instructions are not there.</li>
          <li><strong>No auxiliary bank.</strong> The //e's memory and display switches manage nothing on a II+, which is what makes 80-column text and every double-resolution mode genuinely unavailable rather than merely switched off.</li>
          <li><strong>Its own character generator, and only one character set.</strong> There is no second bank to switch to, so the alternate set is not offered.</li>
          <li><strong>It never switches off the colour burst.</strong> A //e kills the burst on text lines and shows crisp white text; a II+ sends a reference on every line, so its text fringes green and violet in every mode &mdash; exactly as the real machine did.</li>
          <li><strong>Slot 0 exists</strong> and holds the language card, which is how a 48K machine becomes the 64K one nearly all II+ software expects. Slot 3 is free for anything, since there is no built-in 80-column card to occupy it.</li>
        </ul>

        <h4>Apple //c</h4>
        <p>The 1984 portable: a //e folded into a slab. The same 65C02, the same 128KB, the same timing &mdash; what differs is the back of it.</p>
        <ul>
          <li><strong>No expansion sockets at all</strong>, but it decodes all seven slot addresses, because the firmware and everything written for a //e depend on them. Each one answers to a part soldered to the board.</li>
          <li><strong>Two serial ports</strong> in slots 1 and 2, so <code>PR#1</code> prints and <code>IN#2</code> listens.</li>
          <li><strong>A disk drive in its case</strong>, hanging off an Integrated Woz Machine rather than a Disk II card.</li>
          <li><strong>A mouse that is not a card</strong> &mdash; two quadrature lines into the IOU, counted one interrupt per unit of travel. There is nothing to install and nothing to remove.</li>
        </ul>
        <p>Because there is nowhere to put a card, the Expansion Slots window is hidden on a //c rather than greyed out.</p>

        <h4>Apple IIgs</h4>
        <p>The 1986 machine that took the Apple II 16-bit. It boots GS/OS System 6.0.4 to the Finder, with a working mouse and sound.</p>
        <ul>
          <li><strong>A 65C816 at 2.8 MHz</strong> on a 24-bit bus, with 16-bit registers and two operating modes. It drops to 1.023 MHz whenever a drive is turning, exactly as the real machine does.</li>
          <li><strong>256K to 8M of fast RAM</strong>, which you choose from the Machine menu and which is remembered.</li>
          <li><strong>Super Hi-Res</strong> &mdash; 320 or 640 pixels wide with a palette per scanline &mdash; alongside every mode a //e has, because a IIgs contains a Mega II and a Mega II is a //e.</li>
          <li><strong>An Ensoniq</strong> with thirty-two oscillators, and a speaker as well.</li>
          <li><strong>A battery-backed clock</strong> whose Control Panel settings survive a reload.</li>
          <li><strong>A SmartPort in slot 5</strong>, part of the machine rather than a card you fit, serving hard drive images to GS/OS.</li>
        </ul>

        <h4>Switching machines</h4>
        <p>Switching rebuilds the machine from scratch, so anything in the drives or in memory is lost &mdash; exactly as it would be on a page reload. The menu warns you before it does it. What follows you across is what was your choice rather than the machine's: volume, character set and CPU speed. What is remembered <em>per machine</em> is its expansion slot layout, its display settings, its save states, and whether &#8984; acts as Open Apple &mdash; so returning to a machine brings back the way you had it. The machine you last chose is restored the next time you open the emulator.</p>

        <p>Menu items for hardware the running machine does not have are hidden rather than disabled. A greyed-out &ldquo;Expansion Slots&rdquo; on a //c would only raise the question of how to enable it, and the answer is a different computer.</p>
      </section>

      <!-- Keyboard Reference Section -->
      <section id="doc-keyboard" class="documentation-section">
        <h3>Keyboard Reference</h3>
        <p>The Apple //e keyboard is mapped to your modern keyboard. Some keys have special mappings to match the original layout.</p>

        <h4>Basic Keys</h4>
        <table class="key-table">
          <thead>
            <tr><th>Your Keyboard</th><th>Apple //e Key</th><th>Notes</th></tr>
          </thead>
          <tbody>
            <tr><td><kbd>Enter</kbd></td><td>Return</td><td>Confirm input, run commands</td></tr>
            <tr><td><kbd>Backspace</kbd></td><td>Delete</td><td>Delete character left</td></tr>
            <tr><td><kbd>Esc</kbd></td><td>Escape</td><td>Cancel, exit menus</td></tr>
            <tr><td><kbd>Tab</kbd></td><td>Tab</td><td>Tab character</td></tr>
            <tr><td><kbd>&#8592;</kbd> <kbd>&#8594;</kbd> <kbd>&#8593;</kbd> <kbd>&#8595;</kbd></td><td>Arrow Keys</td><td>Cursor movement, game controls</td></tr>
          </tbody>
        </table>

        <h4>Special Keys</h4>
        <table class="key-table">
          <thead>
            <tr><th>Your Keyboard</th><th>Apple //e Key</th><th>Notes</th></tr>
          </thead>
          <tbody>
            <tr><td><kbd>Alt</kbd> / <kbd>Option</kbd> (Left)</td><td>Open Apple (&#63743;)</td><td>Modifier key, joystick button 0</td></tr>
            <tr><td><kbd>Alt</kbd> / <kbd>Option</kbd> (Right)</td><td>Closed Apple</td><td>Modifier key, joystick button 1</td></tr>
            <tr><td><kbd>&#8984;</kbd></td><td>Open Apple, on a IIgs</td><td>See below</td></tr>
            <tr><td><kbd>Ctrl</kbd></td><td>Control</td><td>Control key modifier</td></tr>
            <tr><td><kbd>Ctrl</kbd>+<kbd>Pause/Break</kbd></td><td>Reset</td><td>Warm reset (Ctrl+Reset)</td></tr>
          </tbody>
        </table>

        <h4>Which key is Open Apple depends on the machine</h4>
        <p>On the 8-bit machines the two Option keys are the Apple keys &mdash; left Open, right Closed &mdash; and &#8984; is left to the browser. A IIgs's keyboard is a Mac's: &#8984; <em>is</em> its Open Apple and Option its Closed Apple, and GS/OS drives its menus with &#8984;-letter, so on that machine the emulator takes &#8984; while it has the keyboard.</p>
        <p><strong>View &gt; &#8984; as Open Apple</strong> is the switch. It is remembered per machine and is on by default for the IIgs only. Your browser still keeps &#8984;W, &#8984;Q and a few others for itself, which is why this is a choice rather than a rule.</p>

        <h4>Control Key Combinations</h4>
        <table class="key-table">
          <thead>
            <tr><th>Combination</th><th>Function</th></tr>
          </thead>
          <tbody>
            <tr><td><kbd>Ctrl</kbd>+<kbd>C</kbd></td><td>Break - stop running program</td></tr>
            <tr><td><kbd>Ctrl</kbd>+<kbd>S</kbd></td><td>Pause output (Ctrl+Q to resume)</td></tr>
            <tr><td><kbd>Ctrl</kbd>+<kbd>G</kbd></td><td>Bell (beep)</td></tr>
            <tr><td><kbd>Ctrl</kbd>+<kbd>Reset</kbd></td><td>Warm reset (keeps memory)</td></tr>
          </tbody>
        </table>

        <h4>Emulator Shortcuts</h4>
        <table class="key-table">
          <thead>
            <tr><th>Shortcut</th><th>Function</th></tr>
          </thead>
          <tbody>
            <tr><td><kbd>F1</kbd></td><td>Open/close this Help window</td></tr>
            <tr><td><kbd>Ctrl</kbd>+<kbd>Escape</kbd></td><td>Exit full page mode</td></tr>
            <tr><td><kbd>Ctrl</kbd>+<kbd>V</kbd></td><td>Paste text into emulator</td></tr>
            <tr><td><kbd>Ctrl</kbd>+<kbd>\`</kbd></td><td>Open window switcher</td></tr>
            <tr><td><kbd>Option</kbd>+<kbd>Tab</kbd></td><td>Cycle to next window</td></tr>
            <tr><td><kbd>Option</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd></td><td>Cycle to previous window</td></tr>
          </tbody>
        </table>

        <h4>Debugger Shortcuts</h4>
        <table class="key-table">
          <thead>
            <tr><th>Shortcut</th><th>Function</th></tr>
          </thead>
          <tbody>
            <tr><td><kbd>F5</kbd></td><td>Run / Continue execution</td></tr>
            <tr><td><kbd>F10</kbd></td><td>Step Over (skip subroutine calls)</td></tr>
            <tr><td><kbd>F11</kbd></td><td>Step Into (single instruction)</td></tr>
            <tr><td><kbd>Shift</kbd>+<kbd>F11</kbd></td><td>Step Out (run until current subroutine returns)</td></tr>
          </tbody>
        </table>

        <h4>Text Selection & Copy</h4>
        <p>You can select and copy text directly from the emulator screen:</p>
        <ul>
          <li>Click and drag on the screen to select text</li>
          <li>Selected text is automatically copied when you release the mouse</li>
          <li>Use <kbd>Ctrl</kbd>+<kbd>C</kbd> (or <kbd>Cmd</kbd>+<kbd>C</kbd> on Mac) while selecting</li>
        </ul>

        <h4>Paste Support</h4>
        <p>You can paste text into the emulator using <kbd>Ctrl</kbd>+<kbd>V</kbd>. The emulator will type the text character by character at the appropriate speed. This is useful for entering BASIC programs.</p>

        <h4>Joystick, Paddles &amp; Game Controllers</h4>
        <p>Open <strong>View &gt; Joystick / Paddles</strong> for an on-screen controller with a draggable pad, PDL0/PDL1 gauges, and button 0/1 (Open/Closed Apple) indicators.</p>
        <ul>
          <li><strong>Game port:</strong> The selector at the top of the window chooses what is plugged into the machine's game connector &mdash; an <strong>Apple Joystick</strong> (the usual analog paddles) or a <strong>Sirius Joyport</strong>. The Joyport was Sirius Software's 1981 adapter, and it put two Atari-style digital sticks on the same connector: switches rather than potentiometers, so nothing has to wait for a capacitor, and two players rather than one. Most Sirius games offer &quot;Apple Joystick&quot; or &quot;Joyport&quot; as a choice on their title screen &mdash; pick the one that matches this selector. Choosing the Joyport swaps the paddle knob for a pair of digital sticks, each with a D-pad and a fire button you can hold with the mouse. The choice is remembered, and survives a reset and a change of machine. (A real Joyport idles the two pushbutton lines high, and on a //e those lines <em>are</em> the Open and Closed Apple keys &mdash; so a //e with one fitted ran its self test at every reset instead of booting. Here the Joyport lets go of them for an instant after a reset, so the machine starts normally.)</li>
          <li><strong>Physical controllers:</strong> Enable the <strong>Gamepad</strong> toggle to use a connected game controller via the browser Gamepad API. On an Apple Joystick the left stick maps to the paddles and the A/B buttons to Apple buttons 0/1, with an adjustable deadzone. On a Joyport either the D-pad or the left stick closes the four direction switches and A/B is fire; connect two controllers and each drives one stick, or leave one connected and it drives both so a game that reads the second stick still plays.</li>
          <li><strong>Cursor keys as joystick:</strong> A <strong>JOY</strong> toggle in the screen window's title bar makes the arrow keys drive full-deflection joystick input for games that expect a joystick. The arrows still reach the emulator as ordinary keys, so ProDOS and BASIC navigation keeps working while it is on. The label highlights green while active, and the setting is remembered between sessions.</li>
        </ul>
      </section>

      <!-- Display Settings Section -->
      <section id="doc-display" class="documentation-section">
        <h3>Display Settings</h3>
        <p>Open from <strong>View &gt; Display</strong> to access extensive CRT simulation options.</p>

        <h4>Display Modes</h4>
        <ul>
          <li><strong>Color:</strong> Full NTSC artifact color rendering</li>
          <li><strong>Green:</strong> Classic green phosphor monochrome</li>
          <li><strong>Amber:</strong> Amber phosphor monochrome</li>
          <li><strong>White:</strong> White phosphor monochrome</li>
        </ul>

        <h4>CRT Effects</h4>
        <ul>
          <li><strong>Screen Curvature:</strong> Simulate curved CRT glass</li>
          <li><strong>Overscan:</strong> Add border/overscan area</li>
          <li><strong>Scanlines:</strong> Horizontal CRT scanline effect</li>
          <li><strong>Shadow Mask:</strong> RGB phosphor dot pattern</li>
          <li><strong>Phosphor Glow:</strong> Bloom/glow around bright pixels</li>
          <li><strong>Vignette:</strong> Darker corners effect</li>
          <li><strong>RGB Offset:</strong> Chromatic aberration</li>
          <li><strong>Flicker:</strong> CRT refresh flicker simulation</li>
        </ul>

        <h4>Analog Effects</h4>
        <ul>
          <li><strong>Static:</strong> Random noise/grain</li>
          <li><strong>Jitter:</strong> Random pixel displacement</li>
          <li><strong>H-Sync:</strong> Horizontal sync distortion</li>
          <li><strong>Scan Beam:</strong> Moving scan line effect</li>
          <li><strong>Ambient:</strong> Screen surface reflection</li>
          <li><strong>Burn-in:</strong> Phosphor persistence</li>
        </ul>

        <h4>Image Quality</h4>
        <ul>
          <li><strong>Brightness:</strong> Overall brightness level</li>
          <li><strong>Contrast:</strong> Contrast adjustment</li>
          <li><strong>Saturation:</strong> Color saturation (color mode only)</li>
        </ul>

        <h4>Rendering Options</h4>
        <ul>
          <li><strong>Sharp Pixels:</strong> Nearest-neighbor scaling (crisp pixels)</li>
          <li><strong>NTSC Fringing:</strong> Color fringing on hi-res graphics edges</li>
        </ul>

        <h4>Resizing the Display</h4>
        <p>Drag any corner of the monitor frame to resize. The 4:3 aspect ratio is maintained. A lock icon appears when using custom sizing - click it to return to auto-fit mode.</p>

        <h4>Full Page Mode</h4>
        <p>Click the <strong>fullscreen button</strong> for an immersive experience. Press <kbd>Ctrl</kbd>+<kbd>Escape</kbd> to exit.</p>

        <h4>Character Set</h4>
        <p>Toggle between US and UK character sets using the switch in the screen window header. The UK set replaces some symbols with British variants.</p>
      </section>

      <!-- Workspace Section -->
      <section id="doc-workspace" class="documentation-section">
        <h3>Workspace &amp; Windows</h3>
        <p>Every tool &mdash; the screen, disk drives, debuggers, editors &mdash; lives in a moveable, resizable window. The workspace can either float those windows freely or dock them into a tiled layout, controlled from the <strong>View</strong> menu.</p>

        <h4>Layout Modes</h4>
        <p>Choose a layout from the <strong>Layout</strong> row in the View menu. Each layout remembers its own window arrangement:</p>
        <ul>
          <li><strong>Window:</strong> Free-floating windows you can move and overlap anywhere.</li>
          <li><strong>Play:</strong> A clean arrangement focused on the screen and drives &mdash; the default for new users.</li>
          <li><strong>Code:</strong> A layout tuned for the BASIC and assembler editors.</li>
          <li><strong>Debug:</strong> A layout that surfaces the CPU debugger and memory tools.</li>
        </ul>

        <h4>Docking</h4>
        <p>In the tiled layouts, drag a window by its title bar over another window to reveal <strong>drop zones</strong>. Drop it against an edge to split the space, or onto the centre to stack windows as tabs. Drag the dividers between panes to resize them. This binary-tree docking works like a modern IDE, so you can build whatever arrangement suits your task.</p>

        <h4>Window Switcher &amp; Cycling</h4>
        <ul>
          <li><kbd>Ctrl</kbd>+<kbd>\`</kbd> &mdash; open the window switcher overlay.</li>
          <li><kbd>Option</kbd>+<kbd>Tab</kbd> / <kbd>Option</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> &mdash; cycle forward / backward through open windows.</li>
        </ul>
        <p>Window positions, sizes, and stacking order are saved between sessions, so your workspace reopens exactly as you left it.</p>

        <h4>Full-Page &amp; Fullscreen Modes</h4>
        <p>Click the <strong>Full Page</strong> button for an immersive, chrome-free view. The header bar auto-hides to maximise screen space (you can also force this with <strong>View &gt; Auto-hide Header</strong>), and a compact toolbar with Power, Ctrl+Reset, Reboot, and Exit appears when you move the pointer to the top. Press <kbd>Ctrl</kbd>+<kbd>Escape</kbd> to exit.</p>

        <h4>Slide-Out Drive Popouts</h4>
        <p>In full-page and fullscreen modes the disk drives slide out as popout panels from the edge of the screen. Use <strong>View &gt; Popout Side</strong> to place them on the left or right.</p>

        <h4>Reset Defaults</h4>
        <p>To start fresh, use <strong>File &gt; Reset Defaults&hellip;</strong> to restore the default window layout and settings.</p>

        <div class="info-box tip">
          <p><strong>Tip:</strong> All windows can be moved and resized in any layout mode. If a window disappears off-screen, reopen it from its menu &mdash; it will re-centre in the viewport.</p>
        </div>
      </section>

      <!-- Disk Drives Section -->
      <section id="doc-disks" class="documentation-section">
        <h3>Disk Drives</h3>
        <p>The emulator includes two Disk II floppy drives, just like a real Apple //e system. Open from <strong>View &gt; Disk Drives</strong>.</p>

        <h4>Supported Formats</h4>
        <div class="format-list">
          <div class="format-item"><code>.DSK</code><span>DOS 3.3 sector order (140KB)</span></div>
          <div class="format-item"><code>.DO</code><span>DOS order (same as .DSK)</span></div>
          <div class="format-item"><code>.PO</code><span>ProDOS sector order (140KB)</span></div>
          <div class="format-item"><code>.NIB</code><span>Nibble image (raw GCR)</span></div>
          <div class="format-item"><code>.WOZ</code><span>WOZ format with copy protection</span></div>
        </div>

        <h4>Drive Controls</h4>
        <ul>
          <li><strong>Insert:</strong> Load a disk image from your computer</li>
          <li><strong>Recent:</strong> Quick access to recently used disks (per drive). The dropdown also has a built-in <strong>Library</strong> section for one-click loading of bundled disk images (cached locally for instant reuse)</li>
          <li><strong>Blank:</strong> Create a new formatted blank disk</li>
          <li><strong>Eject:</strong> Remove the disk (prompts to save if modified)</li>
          <li><strong>Browse:</strong> Open the file explorer to view disk contents</li>
        </ul>

        <h4>Drive Information</h4>
        <ul>
          <li><strong>Filename:</strong> Shown on the drive (scrolls if long)</li>
          <li><strong>Track:</strong> Current head position (T00-T34)</li>
          <li><strong>LED:</strong> Glows when drive is active</li>
        </ul>

        <h4>Drag and Drop</h4>
        <p>You can drag disk image files directly onto a drive to insert them.</p>

        <h4>Booting from Disk</h4>
        <ul>
          <li>Type <kbd>PR#6</kbd> and press <kbd>Return</kbd> to boot from Drive 1</li>
          <li>Or use the <strong>Reboot</strong> button for a cold boot</li>
          <li>Many games auto-boot when inserted and the machine is reset</li>
        </ul>

        <h4>Saving Modified Disks</h4>
        <p>When you eject a disk that has been modified, you'll be prompted to save it. You can also use the File Explorer to export disks.</p>
        <p>The save dialog asks which format to write:</p>
        <ul>
          <li><strong>DOS 3.3 order</strong> (<code>.dsk</code>) — sectors in DOS 3.3 order, the most widely accepted format</li>
          <li><strong>ProDOS order</strong> (<code>.po</code>) — the same sectors laid out the way ProDOS numbers them</li>
          <li><strong>WOZ</strong> (<code>.woz</code>) — the track bit stream a drive actually reads</li>
        </ul>
        <p>The format the disk came in is selected for you, so saving without thinking about it never re-encodes the disk, and the filename extension follows whatever you pick. Note that the two sector orders describe how the file is laid out, not which filesystem is on the disk — a ProDOS volume is commonly distributed as a DOS-ordered <code>.dsk</code>. A copy-protected WOZ has no sector representation at all, so the two sector formats are greyed out for one.</p>

        <h4>Disk Persistence</h4>
        <p>Disk contents are automatically saved in your browser's storage. When you return to the emulator, your disks will be exactly as you left them.</p>

        <h4>Sharing a Link</h4>
        <p>Add a disk image URL to the address to hand someone a link that opens with the disk already in the drive:</p>
        <ul>
          <li><code>?disk=</code> &mdash; Drive 1 (<code>?disk1=</code> is the same thing)</li>
          <li><code>?disk2=</code> &mdash; Drive 2</li>
          <li><code>?hd=</code> and <code>?hd2=</code> &mdash; the two SmartPort devices</li>
          <li><code>?name=</code> &mdash; the filename to use when the URL has none, e.g. a download link ending in <code>?id=…</code>. Needed for <code>.nib</code> and <code>.2mg</code>, which cannot be identified from their contents</li>
        </ul>
        <p>Example: <code>?disk=https://example.com/demo.dsk</code></p>
        <p>A path on your own machine, such as <code>/Users/you/Downloads/demo.dsk</code>, will not work &mdash; a web page cannot read local files. Use <strong>Insert</strong> or drag the file onto a drive for those.</p>
        <p>Disks loaded this way are <strong>not</strong> saved to your browser storage or your Recent list, so a link someone sends you never replaces the disks in your own drives &mdash; open the plain address again and everything is back as it was. Autosave pauses for the session for the same reason.</p>

        <div class="info-box tip">
          <p><strong>Tip:</strong> The Recent disks list is maintained separately for each drive, making it easy to quickly swap disks for multi-disk software.</p>
        </div>

        <div class="info-box">
          <p><strong>Note:</strong> The file's host must allow other sites to read it (an <code>Access-Control-Allow-Origin</code> header). This is the host's decision, not the emulator's &mdash; a browser cannot fetch a file the server declines to share, even one that downloads perfectly in another tab.</p>
          <p>GitHub, Google Drive, Dropbox and images hosted alongside the emulator work. Most classic archive mirrors &mdash; Asimov among them &mdash; do not. Re-host the image somewhere that allows it and link that instead.</p>
        </div>
      </section>

      <!-- SmartPort Drives Section -->
      <section id="doc-smartport" class="documentation-section">
        <h3>SmartPort Drives</h3>
        <p>The emulator supports SmartPort hard drive emulation, providing high-capacity storage. Open from <strong>View &gt; SmartPort Drives</strong>.</p>

        <h4>Supported Formats</h4>
        <div class="format-list">
          <div class="format-item"><code>.HDV</code><span>Hard disk volume image</span></div>
          <div class="format-item"><code>.PO</code><span>ProDOS order image</span></div>
          <div class="format-item"><code>.2MG</code><span>Universal disk image (2IMG)</span></div>
        </div>

        <h4>Device Controls</h4>
        <ul>
          <li><strong>Insert:</strong> Load a SmartPort image from your computer</li>
          <li><strong>Recent:</strong> Quick access to recently used images (per device)</li>
          <li><strong>Eject:</strong> Remove the image (prompts to save if modified)</li>
          <li><strong>Browse:</strong> Open the file explorer to view image contents</li>
        </ul>

        <h4>Setup</h4>
        <p>The SmartPort card must be installed in an expansion slot before images can be loaded. Configure this in <strong>View &gt; Expansion Slots</strong>.</p>

        <h4>Activity LED</h4>
        <p>Each device has an LED indicator that glows green when the drive is being accessed.</p>

        <div class="info-box tip">
          <p><strong>Tip:</strong> SmartPort drives provide much larger storage than floppy disks and are commonly used with ProDOS.</p>
        </div>
      </section>

      <!-- Expansion Cards Section -->
      <section id="doc-expansion" class="documentation-section">
        <h3>Expansion Slots &amp; Cards</h3>
        <p>Like a real Apple //e, the emulator has seven expansion slots you can populate with peripheral cards. Open <strong>View &gt; Expansion Slots</strong> to configure them.</p>

        <h4>The Slots Window</h4>
        <p>The window shows a tray of <strong>available cards</strong> above a <strong>motherboard</strong> with slots 1&ndash;7. <strong>Drag a card</strong> from the tray into a slot to install it, or drag it out to remove it. Each slot notes any restrictions (some cards only fit certain slots). Slot&nbsp;6 is fixed to the Disk II controller, and slot&nbsp;3 is the built-in 80-column card.</p>
        <p>Card changes reconfigure the hardware, so click <strong>Apply &amp; Reset</strong> to restart the machine with the new configuration.</p>

        <h4>Available Cards</h4>
        <ul>
          <li><strong>Disk II:</strong> Floppy drive controller (slot 6 by default).</li>
          <li><strong>Mockingboard:</strong> Dual AY-3-8910 sound chips for rich stereo music (slot 4 by default).</li>
          <li><strong>Thunderclock Plus:</strong> ProDOS-compatible real-time clock.</li>
          <li><strong>Mouse Card:</strong> Apple Mouse Interface Card.</li>
          <li><strong>SmartPort:</strong> Hard-drive controller for two ProDOS block devices.</li>
          <li><strong>Super Serial Card (SSC):</strong> Serial port with an ACIA 6551 &mdash; drives the ImageWriter printers and provides modem/serial connectivity (slots 1&ndash;2).</li>
          <li><strong>Parallel Card:</strong> Centronics parallel port &mdash; drives the Epson FX-80 and Apple DMP printers (slots 1&ndash;2).</li>
          <li><strong>Z-80 SoftCard:</strong> Microsoft Z-80 SoftCard with a full Z80 CPU for running CP/M software.</li>
        </ul>

        <h4>No-Slot Clock (DS1215)</h4>
        <p>The <strong>Other Hardware</strong> area of the Slots window has a toggle for the DS1215 No-Slot Clock &mdash; a ProDOS-compatible real-time clock that piggybacks on the 80-column firmware ROM at $C300 without occupying an expansion slot. Enable it for automatic date/time stamping in ProDOS.</p>

        <h4>Serial Port &amp; Modem</h4>
        <p>With a Super Serial Card installed, open <strong>View &gt; Serial Port&hellip;</strong> to connect the Apple //e to the outside world. A built-in Hayes-compatible modem emulation (AT command set) runs over the card's ACIA 6551 and a WebSocket-to-TCP proxy, letting classic terminal and BBS software dial remote services.</p>
        <ul>
          <li>Enter a <strong>Host</strong> and <strong>Port</strong> (Telnet defaults to 23).</li>
          <li>Click <strong>Connect</strong> to open the link; the status indicator shows the connection state.</li>
          <li>Run a terminal program on the //e and dial with standard <code>ATDT</code> commands.</li>
        </ul>

        <h4>Running CP/M (Z-80 SoftCard)</h4>
        <p>Install the <strong>Z-80 SoftCard</strong> to run CP/M software. The card adds a real Z80 CPU with the same address translation as the original hardware, switching between the 6502 and Z80 via I/O and memory access. Boot a CP/M disk (the built-in Disk Library includes a Microsoft CP/M SoftCard disk) to start CP/M.</p>

        <div class="info-box info">
          <p><strong>Note:</strong> After changing slot assignments you must click <strong>Apply &amp; Reset</strong>. Because this resets the machine, apply your slot changes before loading disks or running programs.</p>
        </div>
      </section>

      <!-- File Explorer Section -->
      <section id="doc-file-explorer" class="documentation-section">
        <h3>File Explorer</h3>
        <p>The File Explorer lets you browse the contents of disk images and view files without running programs.</p>

        <h4>Opening the File Explorer</h4>
        <p>Open from <strong>View &gt; File Explorer</strong> or click the <strong>folder icon</strong> in the toolbar. Select which drive to browse using the drive selector at the top.</p>

        <h4>Supported Disk Formats</h4>
        <ul>
          <li><strong>DOS 3.3:</strong> Standard Apple II DOS catalog browsing</li>
          <li><strong>ProDOS:</strong> Full directory navigation with subdirectories</li>
          <li><strong>WOZ:</strong> Catalog extraction from WOZ format disks</li>
        </ul>

        <h4>File Types</h4>
        <ul>
          <li><strong>A (Applesoft BASIC):</strong> Displayed with full detokenization, indentation, and syntax highlighting</li>
          <li><strong>I (Integer BASIC):</strong> Detokenized and formatted</li>
          <li><strong>B (Binary):</strong> Disassembled as 6502 machine code with:
            <ul>
              <li>Recursive descent flow analysis</li>
              <li>Clickable jump/branch targets</li>
              <li>Symbol tooltips (ROM routines, zero page, I/O)</li>
              <li>Operand highlighting</li>
            </ul>
          </li>
          <li><strong>T (Text):</strong> Plain text display</li>
          <li><strong>Other:</strong> Hex dump view</li>
        </ul>

        <h4>Navigation</h4>
        <ul>
          <li>Click file/folder names to open them</li>
          <li>Use the breadcrumb path for ProDOS directory navigation</li>
          <li>Click addresses in disassembly to jump to targets</li>
          <li>Use the back button to return to the catalog</li>
        </ul>

        <h4>Disk Information</h4>
        <p>The header shows disk format, volume name, and free space (for ProDOS disks).</p>

        <div class="info-box tip">
          <p><strong>Tip:</strong> The File Explorer uses virtual scrolling for large files, so even massive disassemblies load instantly.</p>
        </div>
      </section>

      <!-- State Management Section -->
      <section id="doc-state" class="documentation-section">
        <h3>State Management</h3>
        <p>The emulator automatically saves your session so you can pick up exactly where you left off. You also have 5 manual save slots for organizing different states.</p>

        <h4>What Gets Saved</h4>
        <ul>
          <li><strong>CPU State:</strong> All registers (A, X, Y, SP, PC) and flags</li>
          <li><strong>Memory:</strong> Full 128KB RAM (main + auxiliary), and a IIgs's fast RAM as well</li>
          <li><strong>Language Card:</strong> 16KB Language Card RAM</li>
          <li><strong>Soft Switches:</strong> All memory banking and display modes</li>
          <li><strong>Expansion cards:</strong> Every slot, by card, with that card's own state</li>
          <li><strong>Disk Drives:</strong> Complete disk images with modifications, hard drives included</li>
          <li><strong>Settings:</strong> Display, sound, and window positions</li>
        </ul>

        <div class="info-box">
          <p><strong>A state knows which machine wrote it.</strong> Every machine begins its state with the same header, so the emulator can tell a //e's from a IIgs's before loading either. Load a state saved on another machine and it offers to switch to that machine first &mdash; a save is a save of a whole computer, and asking for it back is asking for that computer back. Save slots and the autosave are kept per machine for the same reason.</p>
        </div>

        <p>A state that includes hard drive images can be tens of megabytes, because a SmartPort card's saved state <em>is</em> its images.</p>

        <h4>Auto-Save</h4>
        <p>When enabled (default), state is saved every 5 seconds while the emulator is running. Auto-save also triggers when:</p>
        <ul>
          <li>You switch to another tab or window</li>
          <li>You close the browser</li>
          <li>You power off the emulator</li>
        </ul>
        <p>Toggle auto-save on or off from the <strong>File</strong> menu.</p>

        <h4>Save States Window</h4>
        <p>Open the Save States window from <strong>File &gt; Save States...</strong> to manage all your saved states in one place.</p>

        <h4>Autosave Slot</h4>
        <p>The top row shows the current autosave with a screenshot thumbnail and timestamp. Use the <strong>Load</strong> button to restore it, or <strong>DL</strong> to download it as a file. This slot updates automatically while the window is open.</p>

        <h4>Manual Slots (1&ndash;5)</h4>
        <p>Below the autosave are 5 numbered slots for manual saves. Each slot has:</p>
        <ul>
          <li><strong>Save:</strong> Capture the current emulator state with a screenshot thumbnail</li>
          <li><strong>Load:</strong> Restore the emulator to this saved state</li>
          <li><strong>Clear:</strong> Delete the saved state from this slot</li>
          <li><strong>DL:</strong> Download the state as an <code>.a2state</code> file</li>
        </ul>

        <h4>Load from File</h4>
        <p>Click <strong>Load from File...</strong> at the bottom of the Save States window to restore a previously downloaded <code>.a2state</code> file. The file is validated before loading.</p>

        <h4>How Restore Works</h4>
        <p>Restoring any state (autosave, slot, or file) performs a complete power cycle and then loads the saved state. This ensures a clean restoration with no leftover state from the current session.</p>

        <div class="info-box tip">
          <p><strong>Tip:</strong> Use slots to save before difficult parts of a game, or to keep multiple program states. Download slots to back up important states or transfer them to another device.</p>
        </div>
      </section>

      <!-- Sound Section -->
      <section id="doc-sound" class="documentation-section">
        <h3>Sound Settings</h3>
        <p>Click the <strong>speaker icon</strong> in the toolbar to access audio controls.</p>

        <h4>Audio Controls</h4>
        <ul>
          <li><strong>Volume Slider:</strong> Adjust master volume (0-100%)</li>
          <li><strong>Mute Toggle:</strong> Quickly mute/unmute all sound</li>
          <li><strong>Drive Sounds:</strong> Enable/disable disk drive sound effects</li>
          <li><strong>Printer Sounds:</strong> Enable/disable the printer's impact and carriage sound effects</li>
        </ul>

        <h4>Sound Sources</h4>
        <ul>
          <li><strong>Speaker:</strong> The Apple II's built-in speaker for music and sound effects</li>
          <li><strong>Mockingboard:</strong> Dual AY-3-8910 sound chips for rich stereo music and sound</li>
          <li><strong>Disk Seek:</strong> Stepper motor sounds when the drive head moves</li>
        </ul>

        <h4>Audio Technology</h4>
        <p>The emulator uses the Web Audio API with an AudioWorklet for real-time audio synthesis. Audio timing drives the emulator's frame rate, ensuring accurate 1.023 MHz CPU timing.</p>

        <div class="info-box info">
          <p><strong>Note:</strong> Some browsers require a user interaction (click) before audio can play. Click anywhere on the page if you don't hear sound initially.</p>
        </div>
      </section>

      <!-- Printer Section -->
      <section id="doc-printer" class="documentation-section">
        <h3>Virtual Printer</h3>
        <p>The emulator includes a faithful dot-matrix printer that catches output from your Apple II programs and renders it to an on-screen sheet of fanfold paper, dot by dot, in true carriage travel order. Open it from <strong>View &gt; Printer...</strong> in the toolbar.</p>

        <h4>Printer Models</h4>
        <p>Pick a model from the <strong>model</strong> selector in the printer toolbar. Four period-correct models are emulated, each reproducing its real protocol, fonts, character sets, and timing.</p>
        <ul>
          <li><strong>ImageWriter II:</strong> Apple's colour-capable 9-pin printer &mdash; the only model with a four-band colour ribbon. Draft, correspondence, and NLQ (near-letter-quality) fonts, proportional NLQ, half-height and super/subscript text, and MouseText.</li>
          <li><strong>ImageWriter I:</strong> The earlier black-only ImageWriter. Single correspondence font plus a proportional face; no NLQ, colour, or MouseText.</li>
          <li><strong>Epson FX-80:</strong> The classic ESC/P 9-pin printer used by countless non-Apple programs. Black-only, Roman and Italic faces, Pica/Elite/Compressed pitches, emphasised/double-strike, and eight international character sets.</li>
          <li><strong>Apple DMP:</strong> Apple's Dot Matrix Printer (a rebadged C.&nbsp;Itoh 8510). Black-only, single print face, Pica- and Elite-proportional modes; no NLQ or MouseText.</li>
        </ul>

        <h4>Connecting From the Apple II</h4>
        <p>The printer attaches through an interface card, just like real hardware. Neither interface card is installed by default &mdash; add one in the <strong>Expansion Slots</strong> window (slot&nbsp;1 or&nbsp;2). The emulator enforces the same card-to-printer pairing as real hardware:</p>
        <ul>
          <li><strong>Super Serial Card (SSC):</strong> drives the <strong>ImageWriter I</strong> and <strong>ImageWriter II</strong>.</li>
          <li><strong>Parallel (Centronics) Card:</strong> drives the <strong>Epson FX-80</strong> and <strong>Apple DMP</strong>.</li>
        </ul>
        <p>Only the models supported by the installed card can be selected; the others are disabled until you fit the matching card. Then send output to that slot from your program &mdash; for example <code>PR#1</code> from Applesoft (or <code>PRINT CHR$(4)"PR#1"</code> under ProDOS / BASIC.SYSTEM) to route printing to slot&nbsp;1. The cards are generic byte transports; the printer model alone interprets the data stream.</p>

        <h4>Toolbar Controls</h4>
        <ul>
          <li><strong>Power:</strong> Printer mains power. When off, incoming bytes are ignored and the head parks &mdash; already-printed paper is kept.</li>
          <li><strong>Model:</strong> Switch the emulated printer (clears the current sheet).</li>
          <li><strong>Ribbon:</strong> Choose B/W or Colour. The colour option only appears for the ImageWriter II.</li>
          <li><strong>Paper size:</strong> Choose from standard presets (11&Prime;, 12&Prime;, Legal, A4, and more) or drag the paper edges to set a custom size. The dropdown updates to show the current dimensions when a custom size is in use.</li>
          <li><strong>PNG:</strong> Export the printed output as an image &mdash; a single PNG for one sheet, or a ZIP of per-page PNGs (plus a joined <code>full.png</code>) for multi-page output.</li>
          <li><strong>PDF:</strong> Print or save the output as a PDF (one sheet per page).</li>
        </ul>

        <h4>Operator Panel</h4>
        <p>Click the panel tab (&#9776;) on the paper to reveal the operator controls, mirroring a real printer's front panel. You can also <strong>drag the print-head marker</strong> directly to roll the paper (it snaps to line spacing).</p>
        <table class="key-table">
          <thead>
            <tr><th>Button</th><th>Function</th></tr>
          </thead>
          <tbody>
            <tr><td>Fit</td><td>Toggle fit-to-width versus actual size (1:1)</td></tr>
            <tr><td>Rulers</td><td>Show or hide the inch rulers</td></tr>
            <tr><td>TOP</td><td>Reseat the head at the top of the first page</td></tr>
            <tr><td>FF</td><td>Form feed to the next page top</td></tr>
            <tr><td>LF&#9650; / LF&#9660;</td><td>Line feed up (reverse) / down (advance) one line</td></tr>
            <tr><td>Settings</td><td>Per-model DIP switches (see below)</td></tr>
            <tr><td>Dump Screen</td><td>Print the current //e screen as a graphics bit-image dump. <strong>Long-press</strong> for a reverse-video (inverted) dump; on the ImageWriter II with a colour ribbon the dump prints in colour</td></tr>
            <tr><td>Clear</td><td>Clear the printed output (the current sheet is saved to Print History first)</td></tr>
          </tbody>
        </table>

        <h4>DIP Switch Settings</h4>
        <p>The <strong>Settings</strong> block in the operator panel exposes the DIP switches for the selected model, matching the real printer's power-on configuration:</p>
        <ul>
          <li><strong>ImageWriter I / II:</strong> Auto LF only.</li>
          <li><strong>Epson FX-80:</strong> Auto LF, <em>Slashed zero</em> (print 0 as &Oslash;), and power-on <em>Pitch</em> (Pica 10&nbsp;cpi, Elite 12&nbsp;cpi, or Compressed 17&nbsp;cpi).</li>
          <li><strong>Apple DMP:</strong> Auto LF and a <em>Proportional</em> toggle (power on in Elite-proportional mode).</li>
        </ul>

        <h4>Auto Line Feed</h4>
        <p>The <strong>Auto LF</strong> toggle decides what a carriage return does, exactly like the real DIP switch:</p>
        <ul>
          <li><strong>On:</strong> A CR also feeds the paper one line. Use this for plain text and Applesoft listings, which send CR only.</li>
          <li><strong>Off:</strong> A CR returns the head without feeding, so colour graphics passes overprint in register on the same band &mdash; needed by titles like DazzleDraw and Print Shop colour.</li>
        </ul>

        <h4>Paper Size &amp; Visual Resizing</h4>
        <p>The paper size can be changed at any time without losing printed output. Use the <strong>paper size</strong> dropdown for common presets, or resize the paper visually:</p>
        <ul>
          <li><strong>Height (form length):</strong> Drag the <strong>page-break handle</strong> &mdash; the arrow marker on the right edge of the paper at the bottom of each page &mdash; up or down to set a custom form length. A guide line shows the current position as you drag.</li>
          <li><strong>Width:</strong> Drag the <strong>right edge</strong> of the paper left or right to narrow or widen the sheet within the carriage limits of the selected printer model.</li>
        </ul>
        <p>The dropdown shows the active preset name, or displays the exact dimensions (e.g. <em>8.5&Prime;&times;11&Prime;</em>) when a custom size is in use. Programs can also set the form length via printer control codes; the page-break handle tracks those changes automatically.</p>

        <h4>Rulers</h4>
        <p>Inch rulers run along the <strong>top</strong> and <strong>left</strong> edges of the paper. Toggle them with the <strong>Rulers</strong> button in the operator panel; they also appear automatically during a resize drag even when hidden, so you can read the exact position while adjusting the paper. The origin (0) is at the inner edge of the printable body; the sprocket tractor strips are shown dimmed outside that range, and the left ruler renumbers from 0 on each page so it lines up with the perforations.</p>

        <h4>Standard Paper Sizes</h4>
        <p>The <strong>paper size</strong> dropdown offers six common continuous-stationery presets (dimensions are the printable body, tractor strips excluded):</p>
        <table class="key-table">
          <thead>
            <tr><th>Preset</th><th>Width</th><th>Height</th></tr>
          </thead>
          <tbody>
            <tr><td>Standard (default)</td><td>8.5&Prime;</td><td>11&Prime;</td></tr>
            <tr><td>Legal</td><td>8.5&Prime;</td><td>14&Prime;</td></tr>
            <tr><td>Narrow</td><td>4&Prime;</td><td>11&Prime;</td></tr>
            <tr><td>Half</td><td>5.5&Prime;</td><td>8.5&Prime;</td></tr>
            <tr><td>Index</td><td>3.5&Prime;</td><td>5&Prime;</td></tr>
            <tr><td>Card</td><td>3.5&Prime;</td><td>2&Prime;</td></tr>
          </tbody>
        </table>
        <p>Custom widths and form lengths set by dragging the paper edges are kept within the carriage limits of the selected model (for example the ImageWriter II accepts a 3&Prime;&ndash;9&Prime; body and a 1&Prime;&ndash;69&Prime; form length).</p>

        <h4>Editing the printer fonts</h4>
        <p>The glyph banks the models render from can be authored in a standalone editor at <code>/printers/rom-editor.html</code>. It draws characters dot by dot, handles the alternate-language code points each printer swapped in per locale, imports and exports either as a ROM module or as ASCII dot art, and can trace over a scan of a manual's character chart so a font can be rebuilt from the page it was printed on. It is one plain page with no build step, so it opens straight off disk as readily as from the emulator.</p>

        <h4>Print History &amp; Print Browser</h4>
        <p>Every page is captured automatically as it exits the printer and stored in your browser, so output survives closing the window or reloading the page. Open <strong>View &gt; Print Browser...</strong> to review your full print history:</p>
        <ul>
          <li>Browse jobs and individual pages as thumbnails, grouped by job (model, timestamp, and page count).</li>
          <li><strong>Send a whole job back to the printer</strong> to re-preview it on the live paper &mdash; the model, ribbon, paper size, and head position are all restored so you can continue printing.</li>
          <li>Export a single page as a PNG, or a whole job as a ZIP archive (per-page PNGs plus a joined <code>full.png</code> strip).</li>
          <li>Print a page or an entire job to PDF (or a physical printer).</li>
          <li>Delete individual pages or whole jobs to free storage.</li>
        </ul>

        <div class="info-box info">
          <p><strong>Tip:</strong> Leave the Printer window closed and output is still captured in the background &mdash; reopen it any time to see what your program printed.</p>
        </div>
      </section>

      <!-- Debug Tools Section -->
      <section id="doc-debug" class="documentation-section">
        <h3>Debug Tools</h3>
        <p>Professional debugging tools for software development, reverse engineering, and exploration. Access via the <strong>Debug</strong> menu in the toolbar.</p>

        <h4>CPU Debugger Overview</h4>
        <p>The CPU Debugger provides full control over 65C02 execution with registers, disassembly, breakpoints, watch expressions, and beam position breakpoints. Open it from <strong>Debug &gt; CPU Debugger</strong>.</p>

        <h4>Execution Controls</h4>
        <table class="key-table">
          <thead>
            <tr><th>Button</th><th>Shortcut</th><th>Function</th></tr>
          </thead>
          <tbody>
            <tr><td>Run</td><td><kbd>F5</kbd></td><td>Resume execution (or continue from breakpoint)</td></tr>
            <tr><td>Pause</td><td></td><td>Pause execution immediately</td></tr>
            <tr><td>Step</td><td><kbd>F11</kbd></td><td>Execute one instruction, stepping into subroutines</td></tr>
            <tr><td>Step Over</td><td><kbd>F10</kbd></td><td>Execute one instruction, skipping over JSR calls</td></tr>
            <tr><td>Step Out</td><td><kbd>Shift</kbd>+<kbd>F11</kbd></td><td>Run until the current subroutine returns (RTS/RTI)</td></tr>
          </tbody>
        </table>

        <h4>Registers &amp; Flags</h4>
        <p>The top panel displays all CPU registers and status flags in real time.</p>
        <ul>
          <li><strong>Registers:</strong> A, X, Y (accumulator and index), SP (stack pointer), PC (program counter) &mdash; all shown in hexadecimal</li>
          <li><strong>Flags:</strong> N (negative), V (overflow), B (break), D (decimal), I (interrupt disable), Z (zero), C (carry) &mdash; active flags are highlighted</li>
          <li><strong>Editing:</strong> Double-click any register value while paused to enter a new hex value</li>
        </ul>

        <h4>Cycle &amp; Beam Position</h4>
        <ul>
          <li><strong>CYC:</strong> Total CPU cycle count since power-on</li>
          <li><strong>IRQ / NMI / EDGE:</strong> Indicators for pending interrupt requests</li>
          <li><strong>SCAN:</strong> Current scanline (0&ndash;261), <strong>H:</strong> horizontal position, <strong>COL:</strong> column (0&ndash;39)</li>
          <li><strong>FCYC:</strong> Cycle within the current frame</li>
          <li>A badge shows the beam region: <strong>VISIBLE</strong>, <strong>HBLANK</strong>, or <strong>VBL</strong></li>
        </ul>

        <h4>Disassembly View</h4>
        <p>The scrollable disassembly view shows decoded 65C02 instructions around the current PC.</p>
        <ul>
          <li><strong>Go to Address:</strong> Enter a hex address or symbol name in the input field and click <strong>Go</strong> to jump the disassembly view</li>
          <li><strong>Follow PC:</strong> Click <strong>Follow PC</strong> to re-center the view on the current program counter. When the CPU is running, the view automatically follows PC</li>
          <li><strong>Click a line:</strong> Toggle an execution breakpoint at that address</li>
          <li><strong>Ctrl+Click</strong> (or <strong>Cmd+Click</strong>): Toggle a bookmark on that line (highlighted in yellow)</li>
          <li><strong>Double-click a line:</strong> Add or edit an inline comment that appears next to the instruction</li>
          <li><strong>Right-click a line:</strong> Context menu with <em>Run to Cursor</em>, <em>Go to Address</em>, and <em>Toggle Breakpoint</em></li>
        </ul>
        <p>Branch and jump instructions are color-coded. When symbols are loaded, known addresses are annotated with their symbol names.</p>

        <h4>Symbol Import</h4>
        <p>Click <strong>Import Symbols</strong> in the disassembly toolbar to load a symbol file. Supported formats:</p>
        <ul>
          <li><code>.dbg</code> &mdash; cc65 debug info files</li>
          <li><code>.sym</code> &mdash; Symbol table files (label = address)</li>
          <li><code>.labels</code> &mdash; Label files (address label)</li>
          <li><code>.map</code> &mdash; Map files</li>
          <li><code>.txt</code> &mdash; Plain text symbol lists</li>
        </ul>
        <p>Once imported, symbols appear in the disassembly as annotations and can be used in the address input field.</p>

        <h4>Breakpoints Tab</h4>
        <p>The Breakpoints tab lets you manage all breakpoints. Click <strong>Add</strong> to create a new breakpoint.</p>
        <ul>
          <li><strong>Type:</strong> Choose from <em>Exec</em> (execution), <em>Read</em> (memory read), <em>Write</em> (memory write), or <em>R/W</em> (read or write)</li>
          <li><strong>Address:</strong> Enter a hex address (e.g., <code>FF69</code>) or a symbol name if symbols are loaded</li>
          <li><strong>Conditions:</strong> Optionally add a condition expression. Click the condition cell to open the Rule Builder, or type expressions directly:
            <ul>
              <li><code>A==#$FF</code> &mdash; break when accumulator equals $FF</li>
              <li><code>X&gt;#$10</code> &mdash; break when X register exceeds $10</li>
              <li><code>PEEK($00)==#$42</code> &mdash; break when zero page location $00 equals $42</li>
            </ul>
          </li>
          <li><strong>Hit Count:</strong> Set a hit count target &mdash; the breakpoint only fires after being hit that many times</li>
          <li><strong>Enable/Disable:</strong> Use the checkbox to temporarily disable a breakpoint without deleting it</li>
          <li><strong>Remove:</strong> Click the &times; button to delete a breakpoint</li>
        </ul>
        <p>Breakpoints are persisted to localStorage and survive page reloads.</p>

        <h4>Watch Tab</h4>
        <p>The Watch tab monitors values in real time, highlighting changes. Click <strong>Add Watch</strong> and choose a source:</p>
        <ul>
          <li><strong>Register:</strong> Watch A, X, Y, SP, PC, or P (status byte)</li>
          <li><strong>Flag:</strong> Watch individual status flags (N, V, B, D, I, Z, C)</li>
          <li><strong>Byte:</strong> Watch a memory byte &mdash; displays as <code>PEEK($addr)</code></li>
          <li><strong>Word:</strong> Watch a 16-bit value (little-endian) &mdash; displays as <code>DEEK($addr)</code></li>
        </ul>
        <p>When a watched value changes, it briefly highlights to draw attention. Watch entries are persisted between sessions.</p>

        <h4>Beam Breakpoints Tab</h4>
        <p>Beam breakpoints pause execution based on the CRT beam position rather than the program counter. This is useful for debugging display timing and raster effects.</p>
        <ul>
          <li><strong>VBL Start:</strong> Break at the start of vertical blanking (scanline 192)</li>
          <li><strong>HBLANK:</strong> Break at the start of each horizontal blanking period</li>
          <li><strong>Scanline:</strong> Break when the beam reaches a specific scanline (0&ndash;261)</li>
          <li><strong>Column:</strong> Break when the beam reaches a specific column (0&ndash;39)</li>
          <li><strong>Scan+Col:</strong> Break at a specific scanline <em>and</em> column combination</li>
        </ul>
        <p>Use the <strong>Enable</strong> checkbox to activate or deactivate beam breakpoints. When hit, the breakpoint row highlights briefly.</p>

        <h4>Memory Browser</h4>
        <ul>
          <li>Full 64KB hex dump with ASCII column</li>
          <li>Quick jump buttons for key memory regions</li>
          <li>Direct address entry for navigation</li>
          <li>Changed bytes highlighted with fade animation</li>
          <li>Search for hex byte sequences</li>
          <li>Click any byte to edit its value</li>
        </ul>

        <h4>Memory Heat Map</h4>
        <ul>
          <li>256&times;256 visualization of memory access</li>
          <li>Left panel: Main RAM + ROM</li>
          <li>Right panel: Auxiliary RAM</li>
          <li>View modes: Combined, Reads only, Writes only</li>
          <li>Click to jump to address in Memory Browser</li>
        </ul>

        <h4>Memory Map</h4>
        <ul>
          <li>Visual representation of memory bank configuration</li>
          <li>Shows which banks are active for each region</li>
          <li>Displays read/write bank status</li>
          <li>Color-coded legend</li>
        </ul>

        <h4>Soft Switches</h4>
        <ul>
          <li><strong>Display:</strong> TEXT, MIXED, PAGE2, HIRES, 80COL, ALTCHAR, DHIRES</li>
          <li><strong>Memory:</strong> 80STORE, RAMRD, RAMWRT, INTCXROM, ALTZP, SLOTC3ROM</li>
          <li><strong>Language Card:</strong> LCRAM, LCBANK2, LCWRITE, LCPREWRT</li>
          <li><strong>I/O:</strong> Annunciators, buttons, cassette</li>
        </ul>

        <h4>Stack Viewer</h4>
        <p>Visual representation of the 6502 stack showing return addresses and saved values.</p>

        <h4>Zero Page Watch</h4>
        <ul>
          <li>Predefined watch groups: BASIC, Screen, Graphics, DOS, System</li>
          <li>Add custom watch addresses</li>
          <li>Live value updates</li>
        </ul>

        <h4>Mockingboard Monitor</h4>
        <p>Open from <strong>Debug &gt; Mockingboard</strong> to inspect the dual AY-3-8910 sound chips:</p>
        <ul>
          <li>Channel-centric view with inline waveforms</li>
          <li>AY-3-8910 and VIA 6522 register states</li>
          <li>Level meters for each channel</li>
          <li>Per-channel mute controls</li>
        </ul>

        <h4>Mouse Card Monitor</h4>
        <p>Open from <strong>Debug &gt; Mouse Card</strong> to inspect the Apple Mouse Interface Card:</p>
        <ul>
          <li>PIA registers and protocol activity</li>
          <li>Position, mode, and interrupt state</li>
        </ul>

        <h4>Instruction Trace</h4>
        <p>Open from <strong>Debug &gt; Instruction Trace</strong> to record a live, disassembled log of every instruction the CPU executes. The view auto-scrolls, aligns columns for readability, and can be cleared at any time &mdash; useful for following a routine step by step or capturing exactly what ran up to a crash.</p>

        <h4>Rule Builder</h4>
        <p>The <strong>Condition Rule Builder</strong> opens when you edit a breakpoint's condition (in the CPU Debugger's Breakpoints tab, or from a BASIC breakpoint). It provides a visual, C-style expression editor for complex conditional breakpoints:</p>
        <ul>
          <li>Build conditions from <strong>CPU registers and memory</strong> (e.g. <code>A==#$FF</code>, <code>PEEK($06)&gt;#$10</code>).</li>
          <li>Build conditions from <strong>BASIC variables and arrays</strong> &mdash; simple variables (<code>SCORE&gt;=1000</code>), 1D arrays (<code>A(5)==42</code>), and 2D arrays (<code>G(2,3)==23</code>) &mdash; evaluated natively at every BASIC statement boundary.</li>
          <li>Combine subjects and comparisons without hand-writing the whole expression.</li>
        </ul>

        <div class="info-box tip">
          <p><strong>Tip:</strong> All debug windows can be moved and resized. Their positions and settings are saved between sessions.</p>
        </div>
      </section>

      <!-- Dev Tools Section -->
      <section id="doc-dev" class="documentation-section">
        <h3>Dev Tools</h3>
        <p>Development tools for writing and testing software. Access via the <strong>Dev</strong> menu in the toolbar.</p>

        <h4>Applesoft BASIC Window</h4>
        <p>Write, edit, debug, and load Applesoft BASIC programs. Open from <strong>Dev &gt; Applesoft BASIC</strong>.</p>

        <h5>Editor Features</h5>
        <ul>
          <li><strong>New:</strong> Clear the editor and start a new program</li>
          <li><strong>Syntax Highlighting:</strong> BASIC keywords, line numbers, strings, and comments are color-coded</li>
          <li><strong>Autocomplete:</strong> Type to see suggestions for BASIC commands</li>
        </ul>

        <h5>Debugger Controls</h5>
        <ul>
          <li><strong>Run:</strong> Execute the BASIC program</li>
          <li><strong>Pause:</strong> Pause execution</li>
          <li><strong>Step:</strong> Step through one BASIC line at a time</li>
          <li><strong>Stop:</strong> Send Ctrl+C to break a running program (also unpauses the emulator so the keystroke is processed)</li>
        </ul>

        <h5>BASIC Debugging Features</h5>
        <ul>
          <li><strong>Breakpoints:</strong> Click the gutter next to a line to set a statement-level breakpoint. Add <strong>conditional</strong> breakpoints on BASIC variables and arrays via the Rule Builder, or condition-only rules (the <em>if&hellip;</em> button) that break wherever a condition becomes true. A triggered breakpoint pulses red.</li>
          <li><strong>Variable Inspector:</strong> View the live values of the program's variables and arrays while it runs or is paused.</li>
          <li><strong>Heat map:</strong> The <strong>Heat</strong> toggle colours the gutter (blue&rarr;red) to show how often each line executes, with smooth decay as activity moves on.</li>
          <li><strong>Trace:</strong> The <strong>Trace</strong> toggle enables or disables current-line highlighting while the program runs, reducing visual noise for long programs.</li>
        </ul>

        <h5>Program Operations</h5>
        <ul>
          <li><strong>Read:</strong> Read the current BASIC program from emulator memory into the editor</li>
          <li><strong>Write:</strong> Type the program into the running emulator</li>
          <li><strong>Format:</strong> Auto-format the program text</li>
          <li><strong>Renum:</strong> Renumber BASIC line numbers</li>
        </ul>

        <h5>File Operations</h5>
        <ul>
          <li><strong>New:</strong> Start a new program</li>
          <li><strong>Open:</strong> Open a BASIC program file from your computer</li>
          <li><strong>Save:</strong> Save the current program to a file</li>
        </ul>

        <div class="info-box warning">
          <p><strong>Note:</strong> The Read and Write buttons require the emulator to be powered on.</p>
        </div>

        <h4>Assembler</h4>
        <p>Write 65C02 assembly code using Merlin-style syntax. Open from <strong>Dev &gt; Assembler</strong>.</p>

        <h5>Editor Features</h5>
        <ul>
          <li><strong>Syntax Highlighting:</strong> Opcodes, directives, labels, operands, and comments</li>
          <li><strong>Column Guides:</strong> Visual guides for Merlin's column-based format (Label, Opcode, Operand, Comment)</li>
          <li><strong>Tab Navigation:</strong> Press Tab to jump between columns</li>
          <li><strong>Live Validation:</strong> Syntax errors shown as you type</li>
          <li><strong>Breakpoints:</strong> Click the gutter or press <kbd>F9</kbd> to toggle breakpoints</li>
        </ul>

        <h5>File Operations</h5>
        <table class="key-table">
          <thead>
            <tr><th>Button</th><th>Shortcut</th><th>Function</th></tr>
          </thead>
          <tbody>
            <tr><td>New</td><td><kbd>Ctrl/⌘</kbd>+<kbd>N</kbd></td><td>Start a new file</td></tr>
            <tr><td>Open</td><td><kbd>Ctrl/⌘</kbd>+<kbd>O</kbd></td><td>Open a .s, .asm, or .a65 file</td></tr>
            <tr><td>Save</td><td><kbd>Ctrl/⌘</kbd>+<kbd>S</kbd></td><td>Save current file</td></tr>
          </tbody>
        </table>

        <h5>Assembly &amp; Loading</h5>
        <ul>
          <li><strong>Assemble:</strong> Click or press <kbd>Ctrl/⌘</kbd>+<kbd>Enter</kbd> to assemble the code</li>
          <li><strong>Write:</strong> After successful assembly, click Write to copy the machine code into emulator memory (requires emulator to be powered on)</li>
          <li><strong>ORG Directive:</strong> Your code must include an <code>ORG</code> directive before any instructions</li>
          <li><strong>DSK / SAV Directives:</strong> <code>DSK FILENAME</code> writes the assembled object straight onto the disk in drive 1 as a binary file, the way Merlin does. Add <code>,D2</code> to target drive 2, and <code>TYP</code> to set the ProDOS file type. DOS 3.3 and ProDOS disks are both written; WOZ images are not, since they store a flux-level bit stream rather than sectors. An existing file of the same name is replaced, and a locked or write-protected one is refused</li>
        </ul>

        <h5>Merlin Compatibility</h5>
        <p>The assembler follows Merlin rather than generic 65C02 assembler convention, which shows up in three places worth knowing about.</p>
        <ul>
          <li><strong>Expressions run left to right with no precedence.</strong> <code>1+2*3</code> is 9, not 7. The operators are <code>+ - * /</code> and Merlin's logicals <code>.</code> (or), <code>!</code> (exclusive or) and <code>&amp;</code> (and). <code>&lt;</code>, <code>&gt;</code> and <code>^</code> select the low, high and bank byte of the <em>whole</em> expression, so <code>#&gt;LABEL+1</code> is the high byte of LABEL+1</li>
          <li><strong>A comment needs no semicolon.</strong> A line is four whitespace-separated fields — label, opcode, operand, comment — so everything after the operand is a comment. That is also why an operand may not contain a space unless it is inside a string</li>
          <li><strong>A delimiter chooses the high bit.</strong> <code>ASC 'TEXT'</code> stores plain ASCII, <code>ASC "TEXT"</code> sets the high bit. Any character may delimit, and the same rule decides: below an apostrophe in ASCII sets the bit, at or above it does not</li>
        </ul>

        <h5>Directives</h5>
        <table class="key-table">
          <thead>
            <tr><th>Group</th><th>Directives</th></tr>
          </thead>
          <tbody>
            <tr><td>Layout</td><td><code>ORG</code> <code>OBJ</code> <code>EQU</code> <code>=</code> <code>VAR</code> <code>DS</code> <code>DUM</code> <code>DEND</code></td></tr>
            <tr><td>Data</td><td><code>DFB</code> <code>DB</code> <code>DW</code> <code>DA</code> <code>DDB</code> <code>ADR</code> <code>ADRL</code> <code>HEX</code> <code>CHK</code></td></tr>
            <tr><td>Strings</td><td><code>ASC</code> <code>DCI</code> <code>INV</code> <code>FLS</code> <code>REV</code> <code>STR</code> <code>STRL</code></td></tr>
            <tr><td>Conditionals</td><td><code>DO</code> <code>IF</code> <code>ELSE</code> <code>FIN</code></td></tr>
            <tr><td>Loops</td><td><code>LUP</code> <code>--^</code> <code>ELUP</code></td></tr>
            <tr><td>Macros</td><td><code>MAC</code> <code>EOM</code> <code>&lt;&lt;&lt;</code> <code>&gt;&gt;&gt;</code> <code>PMC</code></td></tr>
            <tr><td>Source files</td><td><code>PUT</code> <code>USE</code> <code>CHN</code></td></tr>
            <tr><td>Object file</td><td><code>DSK</code> <code>SAV</code> <code>TYP</code></td></tr>
            <tr><td>Control</td><td><code>END</code> <code>ERR</code> <code>SW</code> <code>XC</code></td></tr>
            <tr><td>Listing</td><td><code>LST</code> <code>CYC</code> <code>EXP</code> <code>PAG</code> <code>TTL</code> <code>SKP</code> <code>PAU</code> <code>DAT</code> <code>TR</code></td></tr>
          </tbody>
        </table>
        <p><code>REL</code>, <code>ENT</code>, <code>EXT</code> and <code>LNK</code> produce relocatable object code for a linker, which this assembler does not have, so they are reported as errors rather than quietly ignored. <code>KBD</code> prompted the operator during assembly and cannot here, so it takes its operand's value and reports a warning. <code>MX</code> and the second <code>XC</code> ask for the 65816, which a //e cannot run.</p>

        <h5>Macros, Variables and Local Labels</h5>
        <ul>
          <li><strong>Macros:</strong> <code>NAME MAC</code> opens a definition and <code>EOM</code> or <code>&lt;&lt;&lt;</code> closes it. Call one by its name, or with <code>&gt;&gt;&gt; NAME;P1;P2</code> or <code>PMC NAME;P1;P2</code>. Inside the body <code>]1</code> to <code>]8</code> stand for the parameters, substituted as text</li>
          <li><strong>Local labels:</strong> a label beginning <code>:</code> belongs to the global label above it, so the same <code>:LOOP</code> can appear throughout a source. Inside a macro they are scoped to a single expansion, so a macro called twice does not collide with itself</li>
          <li><strong>Variables:</strong> a label beginning <code>]</code> may be reassigned as often as you like. <code>VAR 3;4</code> loads <code>]1</code> and <code>]2</code> in one line</li>
          <li><strong>Testing a parameter:</strong> <code>IF "",]1</code> is true when the parameter was omitted, because <code>IF</code> compares the first character of what it was given</li>
        </ul>

        <h5>PUT and USE</h5>
        <p><code>PUT NAME</code> and <code>USE NAME</code> read included source off the disk in drive 1, then drive 2, from either a DOS 3.3 or a ProDOS disk. Merlin's habit of naming source files <code>T.SOMETHING</code> is honoured, so <code>PUT MACROS</code> finds a file saved as <code>T.MACROS</code>. <code>CHN</code> hands assembly over to another file and does not come back.</p>

        <h5>Sweet-16</h5>
        <p><code>SW</code> switches the opcode field over to Sweet-16, Woz's 16-bit interpreter — <code>SET</code>, <code>LD</code>, <code>ST</code>, <code>LDD</code>, <code>STD</code>, <code>POP</code>, <code>POPD</code>, <code>STP</code>, <code>ADD</code>, <code>SUB</code>, <code>CPR</code>, <code>INR</code>, <code>DCR</code> against registers <code>R0</code>&ndash;<code>R15</code> (with <code>@</code> for the indirect forms), plus <code>BR</code>, <code>BNC</code>, <code>BC</code>, <code>BP</code>, <code>BM</code>, <code>BZ</code>, <code>BNZ</code>, <code>BM1</code>, <code>BNM1</code>, <code>BS</code>, <code>RTN</code>, <code>RS</code> and <code>BK</code>. <code>SW OFF</code> hands the mnemonics back to the 65C02.</p>

        <h5>ROM Routines Reference</h5>
        <p>Press <kbd>F2</kbd> or click <strong>ROM</strong> to open the ROM routines panel:</p>
        <ul>
          <li>Search and browse Apple II ROM routines</li>
          <li>View input/output requirements and examples</li>
          <li>Insert EQU definitions or JSR calls directly into your code</li>
        </ul>

        <h5>Output Panels</h5>
        <ul>
          <li><strong>Symbols:</strong> Lists all defined labels and their addresses</li>
          <li><strong>Hex Output:</strong> Shows assembled machine code bytes</li>
          <li><strong>Problems:</strong> Every error and warning from the last assembly in one list, most serious first. Click a row to jump to the line it is about. Warnings are yellow and do not stop the assembly &mdash; they name something Merlin could do that this assembler cannot</li>
        </ul>
        <p>The gutter shows each line's address, cycle count and object bytes as the assembler itself reported them, which is why they appear after an assembly rather than as you type: what a line produces depends on the source around it once macros, <code>LUP</code> blocks and conditional assembly are involved. A macro call shows the bytes its expansion produced.</p>
      </section>

      <!-- AI Agent Section -->
      <section id="doc-agent" class="documentation-section">
        <h3>AI Agent</h3>
        <p>The AI Agent integration allows LLMs like Claude to control the emulator through natural language commands. The agent can show/hide windows, manage disks, read/write BASIC programs, and inspect emulator state in real time using the AG-UI protocol over an MCP server.</p>

        <h4>Connection Status</h4>
        <p>The agent connection status is shown by a sparkle icon in the toolbar header:</p>
        <table class="key-table">
          <thead>
            <tr><th>Icon</th><th>Status</th><th>Description</th></tr>
          </thead>
          <tbody>
            <tr>
              <td><svg viewBox="0 0 24 24" width="20" height="20" fill="#6e7681"><path d="M12 2l1.5 4.5L18 8l-4.5 1.5L12 14l-1.5-4.5L6 8l4.5-1.5L12 2zM6 16l.75 2.25L9 19l-2.25.75L6 22l-.75-2.25L3 19l2.25-.75L6 16zM18 16l.75 2.25L21 19l-2.25.75L18 22l-.75-2.25L15 19l2.25-.75L18 16z"/></svg></td>
              <td>Disconnected</td>
              <td>MCP server is not running or not reachable</td>
            </tr>
            <tr>
              <td><svg viewBox="0 0 24 24" width="20" height="20" fill="#FDBE34"><path d="M12 2l1.5 4.5L18 8l-4.5 1.5L12 14l-1.5-4.5L6 8l4.5-1.5L12 2zM6 16l.75 2.25L9 19l-2.25.75L6 22l-.75-2.25L3 19l2.25-.75L6 16zM18 16l.75 2.25L21 19l-2.25.75L18 22l-.75-2.25L15 19l2.25-.75L18 16z"/></svg></td>
              <td>Connected</td>
              <td>Agent is connected and ready to receive commands</td>
            </tr>
            <tr>
              <td><svg viewBox="0 0 24 24" width="20" height="20" fill="#E5504F"><path d="M12 2l1.5 4.5L18 8l-4.5 1.5L12 14l-1.5-4.5L6 8l4.5-1.5L12 2zM6 16l.75 2.25L9 19l-2.25.75L6 22l-.75-2.25L3 19l2.25-.75L6 16zM18 16l.75 2.25L21 19l-2.25.75L18 22l-.75-2.25L15 19l2.25-.75L18 16z"/></svg></td>
              <td>Interrupted</td>
              <td>Connection error or server unavailable</td>
            </tr>
          </tbody>
        </table>
        <p>Click the sparkle icon to open the agent connection panel and view detailed status information.</p>

        <h4>Connection Names &amp; Multiple Emulators</h4>
        <p>Every browser tab that connects is assigned a <strong>unique name</strong> from a name pool — short, memorable words like <em>Bingo</em>, <em>Wozulator</em>, or <em>Pixel</em>. The name appears in the sparkle button label so you always know which tab is which. Names persist across server restarts within the same browser session.</p>
        <p><strong>To rename:</strong> Double-click the name label on the sparkle button (connected state only). Type a new name and press <kbd>Enter</kbd> to confirm, or <kbd>Escape</kbd> to cancel. Valid characters: Unicode letters, hyphens, underscores — no numbers or spaces.</p>
        <p><strong>Multiple tabs:</strong> More than one browser tab can connect at once. Claude routes commands based on context:</p>
        <ul>
          <li><strong>One connected</strong> — routes to it automatically</li>
          <li><strong>Multiple, one is default</strong> — routes to the default</li>
          <li><strong>Named target</strong> — "Take a screenshot of Bingo"</li>
          <li><strong>Broadcast</strong> — "Reboot all connected emulators"</li>
        </ul>

        <h4>Setting Up the MCP Server</h4>
        <p>The AI Agent uses the Model Context Protocol (MCP) to communicate with LLM clients like Claude Code. Add the following to your MCP configuration file (e.g., <code>.mcp.json</code> in your project or <code>~/.claude/mcp.json</code> globally):</p>
        <p><strong>Using bunx (recommended):</strong></p>
        <pre><code>{
  "mcpServers": {
    "appleii-agent": {
      "type": "stdio",
      "command": "bunx",
      "args": ["-y", "@retrotech71/appleii-agent"],
      "env": {
        "APPLEII_AGENT_SANDBOX": "/path/to/sandbox.config"
      }
    }
  }
}</code></pre>
        <p>No installation required — the package downloads automatically. <a href="https://bun.sh" target="_blank">Bun</a> is recommended; replace <code>bunx</code> with <code>npx</code> if you prefer Node.js. The server listens on <code>http://localhost:3033</code> by default.</p>

        <h4>Sandbox Configuration</h4>
        <p>The sandbox controls which directories the agent can access on your filesystem. Without it the server starts but all file operations are blocked.</p>
        <p><strong>1. Create the config file</strong> (<code>~/.appleii/sandbox.config</code>):</p>
        <pre><code># Lines starting with # are comments
# Format: [key]@/path/to/directory

[disks]@~/Documents/Apple2/Disks
[games]@~/Documents/Apple2/Games
[basic]@~/Documents/Apple2/BASIC</code></pre>
        <ul>
          <li><strong>Key:</strong> alphanumeric, underscores, hyphens — used as <code>[key]</code> in requests</li>
          <li><strong>Path:</strong> absolute or <code>~</code>-prefixed home-relative directory</li>
          <li>Empty lines and <code>#</code> comments are ignored</li>
        </ul>
        <p><strong>2. Set <code>APPLEII_AGENT_SANDBOX</code></strong> in the <code>env</code> block of your <code>.mcp.json</code> (shown above).</p>
        <p><strong>3. Use sandbox paths</strong> in requests using <code>[key]/relative/path</code> syntax:</p>
        <ul>
          <li>"Load <code>[disks]/ProDOS.dsk</code> into drive 1"</li>
          <li>"Save the BASIC program to <code>[basic]/hello.bas</code>"</li>
          <li>"Load <code>[games]/Zork/zork1.dsk</code> into drive 2"</li>
        </ul>
        <p><strong>4. After editing the config</strong>, ask the agent to "reload the sandbox configuration" — no restart needed.</p>
        <div class="info-box tip">
          <p><strong>Tip:</strong> Full <code>~/</code> paths also work as long as they fall inside a configured sandbox directory. Path traversal and out-of-sandbox access are blocked automatically.</p>
        </div>

        <h4>Port Conflict Management</h4>
        <p>The MCP server includes graceful port conflict handling when multiple instances attempt to use port 3033:</p>
        <ul>
          <li><strong>Automatic Detection:</strong> When port 3033 is already in use, the MCP server stays alive without failing</li>
          <li><strong>Status Reporting:</strong> The status tool reports port conflicts and provides clear guidance</li>
          <li><strong>Port Reclamation:</strong> Any instance can take over the port using a two-step process:
            <ol class="quick-start-list">
              <li>Ask the agent to "shutdown the remote server on port 3033"</li>
              <li>Ask the agent to "start this server"</li>
            </ol>
          </li>
        </ul>
        <p>This allows multiple Claude Code sessions or MCP instances to coordinate gracefully without manual process management.</p>

        <h4>Example Prompts</h4>

        <h5>Multi-Emulator</h5>
        <ul>
          <li><strong>List connected emulators:</strong> "Show me all connected emulators"</li>
          <li><strong>Set the default:</strong> "Set Bingo as the default emulator"</li>
          <li><strong>Target by name:</strong> "Take a screenshot of Wozulator" or "Reboot Bingo"</li>
          <li><strong>Broadcast:</strong> "Reboot all connected emulators"</li>
          <li><strong>Send BASIC to specific tab:</strong> "Write this program to Bingo: 10 PRINT &quot;HELLO&quot;"</li>
        </ul>

        <h5>Window Management</h5>
        <ul>
          <li><strong>Show a window:</strong> "Show the CPU debugger window"</li>
          <li><strong>Hide a window:</strong> "Hide the disk drives window"</li>
          <li><strong>Focus a window:</strong> "Bring the BASIC program window to the front"</li>
        </ul>

        <h5>Disk Management</h5>
        <ul>
          <li><strong>Insert from filesystem:</strong> "Load ~/Documents/Apple_II/ProDOS_2_4_2.dsk into drive 1"</li>
          <li><strong>List recent disks:</strong> "What disks are in the recent list for drive 1?"</li>
          <li><strong>Load from recent:</strong> "Insert the disk named Zork_1.dsk from recent disks into drive 2"</li>
          <li><strong>Eject a disk:</strong> "Eject the disk from drive 1"</li>
        </ul>

        <h5>BASIC Programs</h5>
        <ul>
          <li><strong>Read from memory:</strong> "Load the BASIC program from memory and show it in the editor"</li>
          <li><strong>Write to memory:</strong> "Write this BASIC program to emulator memory: 10 PRINT \"HELLO\" 20 GOTO 10"</li>
          <li><strong>Get listing:</strong> "What BASIC program is currently in memory?"</li>
          <li><strong>Save to file:</strong> "Save the BASIC program from the editor to ~/Documents/myprogram.bas"</li>
        </ul>

        <h5>Assembly Programs</h5>
        <ul>
          <li><strong>Get status:</strong> "What's the status of the assembler?" or "Get the assembly origin address"</li>
          <li><strong>Execute program:</strong> "Run the assembled program" or "Execute the code at the origin"</li>
          <li><strong>Execute at address:</strong> "Execute the code at $0800" or "Run code at address 2048"</li>
          <li><strong>Execute with return:</strong> "Execute $0800 and return to BASIC" or "Run $0800 and return to monitor"</li>
          <li><strong>Set PC without executing:</strong> "Set PC to $0800 but don't execute yet"</li>
        </ul>

        <h5>Memory Operations</h5>
        <ul>
          <li><strong>Load binary to memory:</strong> "Load the file ~/program.bin into memory at address $2000"</li>
          <li><strong>Save memory range:</strong> "Save 256 bytes from memory address $0800 to ~/output.bin"</li>
          <li><strong>Save memory region:</strong> "Read 1024 bytes starting at $4000 and save them to ~/dump.bin"</li>
        </ul>

        <h5>Screen Capture</h5>
        <ul>
          <li><strong>Capture screenshot:</strong> "Take a screenshot of the current screen"</li>
          <li><strong>Save screenshot to file:</strong> "Capture the screen and save it as ~/screenshot.png"</li>
          <li><strong>Read screen text:</strong> "What text is currently displayed on the screen?"</li>
          <li><strong>Read specific region:</strong> "Read the text from rows 5 to 15 on the screen"</li>
          <li><strong>Read CATALOG output:</strong> "Read the text from the screen after running CATALOG"</li>
        </ul>

        <h5>SmartPort Hard Drives</h5>
        <ul>
          <li><strong>Insert image:</strong> "Load ~/Images/Total_Replay.hdv into SmartPort device 1"</li>
          <li><strong>List recent images:</strong> "What images are in the recent list for SmartPort device 1?"</li>
          <li><strong>Load from recent:</strong> "Insert Apple Pascal from recent SmartPort images"</li>
          <li><strong>Clear recent:</strong> "Clear the recent images list for SmartPort device 1"</li>
        </ul>

        <h5>Slot Configuration</h5>
        <ul>
          <li><strong>List all slots:</strong> "Show me the current expansion slot configuration"</li>
          <li><strong>Install a card:</strong> "Install the Mockingboard in slot 4"</li>
          <li><strong>Remove a card:</strong> "Remove the card from slot 5"</li>
          <li><strong>Move a card:</strong> "Move the SmartPort card from slot 7 to slot 5"</li>
        </ul>

        <h5>Emulator Control</h5>
        <ul>
          <li><strong>Power on:</strong> "Turn on the emulator" or "Power on the Apple //e"</li>
          <li><strong>Power off:</strong> "Turn off the emulator" or "Power off"</li>
          <li><strong>Reboot:</strong> "Reboot the emulator" or "Do a cold reset"</li>
          <li><strong>Warm reset:</strong> "Send Ctrl+Reset to the emulator" or "Press Ctrl+Reset"</li>
          <li><strong>Break program:</strong> "Send Ctrl+C to the emulator" or "Stop the running program"</li>
        </ul>

        <div class="info-box info">
          <p><strong>Note:</strong> The MCP server must be running for the agent to connect. The server starts automatically when your MCP client connects.</p>
        </div>
      </section>

      <!-- Tips Section -->
      <section id="doc-tips" class="documentation-section">
        <h3>Tips & Troubleshooting</h3>

        <h4>Getting Software</h4>
        <p>Search for "Apple II disk images" to find archives of classic software. Popular archives include:</p>
        <ul>
          <li>Asimov Apple II Archive</li>
          <li>What Is The Apple IIGS?</li>
          <li>Internet Archive Apple II Library</li>
        </ul>

        <h4>Common BASIC Commands</h4>
        <div class="info-box tip">
          <p>
            <code>CATALOG</code> - List files on disk<br>
            <code>RUN filename</code> - Run a BASIC program<br>
            <code>LOAD filename</code> - Load a program into memory<br>
            <code>LIST</code> - Show program listing<br>
            <code>NEW</code> - Clear current program<br>
            <code>PR#6</code> - Boot from disk in slot 6
          </p>
        </div>

        <h4>Keyboard Not Working?</h4>
        <p>Click directly on the monitor screen to give it keyboard focus. The emulator needs focus to receive keyboard input.</p>

        <h4>No Sound?</h4>
        <ul>
          <li>Check that the volume is turned up in Sound Settings</li>
          <li>Check that your system volume is not muted</li>
          <li>Click anywhere on the page - browsers require user interaction before playing audio</li>
        </ul>

        <h4>Disk Won't Boot?</h4>
        <ul>
          <li>Make sure the emulator is powered on</li>
          <li>Try typing <kbd>PR#6</kbd> and pressing Return</li>
          <li>Try the Reboot button for a cold start</li>
          <li>Check that the disk is a bootable system disk</li>
        </ul>

        <h4>Performance Issues?</h4>
        <ul>
          <li>Disable some CRT effects in Display Settings</li>
          <li>Close unused debug windows</li>
          <li>Try a different browser (Chrome recommended)</li>
        </ul>

        <h4>Saving Your Work</h4>
        <ul>
          <li>State auto-saves every 5 seconds by default</li>
          <li>Use <strong>File &gt; Save States...</strong> to save to manual slots or download states</li>
          <li>Modified disks are saved when ejected</li>
          <li>Export disks via File Explorer for backup</li>
        </ul>

        <h4>Release Notes</h4>
        <p>Click <strong>"Release Notes"</strong> in the footer to see the version history and recent changes.</p>

        <div class="info-box info">
          <p><strong>Need more help?</strong> The Apple II has extensive documentation available online. Search for "Apple II Reference Manual" or "Applesoft BASIC Programming Guide" for detailed information.</p>
        </div>
      </section>
    `;
  }

  /**
   * Called after content is rendered - set up nav button handlers
   */
  onContentRendered() {
    this.navButtons = this.contentElement.querySelectorAll(
      ".documentation-nav button"
    );
    this.sections = this.contentElement.querySelectorAll(
      ".documentation-section"
    );

    // Navigation button clicks
    this.navButtons.forEach((btn) => {
      btn.addEventListener("click", () => {
        const sectionId = btn.dataset.section;
        this.showSection(sectionId);
      });
    });
  }

  /**
   * Show a specific section by ID
   * @param {string} sectionId - The section ID to show (without 'doc-' prefix)
   */
  showSection(sectionId) {
    // Update nav button active states
    this.navButtons.forEach((btn) => {
      btn.classList.toggle("active", btn.dataset.section === sectionId);
    });

    // Show/hide sections
    this.sections.forEach((section) => {
      const isTarget = section.id === `doc-${sectionId}`;
      section.classList.toggle("active", isTarget);
    });
  }
}
