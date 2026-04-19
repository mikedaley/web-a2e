/*
 * release-notes.js - Release notes content data
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

/**
 * Curated release notes organized by week
 */

export const RELEASE_NOTES = [
  {
    week: "April 13, 2026",
    improvements: [
      {
        title: "No-Slot Clock precision",
        description:
          "The DS1215 No-Slot Clock now reports centisecond (1/100s) accuracy instead of whole-second resolution, enabling precise benchmark timing against real hardware.",
      },
    ],
    fixes: [
      {
        title: "Safari agent polling",
        description:
          "Disabled agent server polling in Safari on remote origins to prevent repeated failed connection attempts.",
      },
    ],
  },
  {
    week: "March 23, 2026",
    features: [
      {
        title: "Binary-tree docking system",
        description:
          "Replaced the CSS Grid workspace with an ImGui-style binary-tree docking layout, allowing windows to be docked, split, and resized by dragging into drop zones.",
      },
      {
        title: "Window layout mode",
        description:
          "Added a \"Window\" layout mode for free-floating windows without docking, alongside the existing docked layouts.",
      },
      {
        title: "Slide-out drive popouts",
        description:
          "Disk drives now slide out as popout panels in full-page and fullscreen modes, with a configurable side selector for left or right placement.",
      },
      {
        title: "Layout presets",
        description:
          "Added preset-based layouts (including a default play preset for first-time users) with per-preset state persistence, so each preset remembers its own window arrangement.",
      },
    ],
    fixes: [
      {
        title: "Safari agent connection",
        description:
          "Fixed the site failing to load in Safari when served from a remote origin, caused by blocked cross-origin requests to the local MCP agent server.",
      },
      {
        title: "Screen corner radius",
        description:
          "Screen corner radius is now only applied when bezel or curvature effects are active, preventing clipped corners on a flat display.",
      },
    ],
    improvements: [
      {
        title: "Auto-hide header",
        description:
          "The header bar auto-hides in full-page and fullscreen modes to maximize screen real estate.",
      },
      {
        title: "SmartPort drive stacking",
        description:
          "SmartPort drives now stack vertically when the window is narrow, improving usability on smaller screens.",
      },
      {
        title: "Disk drive toolbar",
        description:
          "Moved disk drive Surface/Details toggles from the header into a content toolbar for a cleaner layout.",
      },
    ],
  },
  {
    week: "March 14, 2026",
    features: [
      {
        title: "WASM emulator moved to Web Worker",
        description:
          "The entire WASM emulation core now runs in a dedicated Web Worker thread, eliminating main-thread blocking from CPU emulation, disk image parsing, and state serialization. Loading large disk images (including 32MB SmartPort hard drives) no longer causes audio dropouts or UI freezes.",
      },
    ],
    fixes: [],
    improvements: [
      {
        title: "Audio-driven timing preserved",
        description:
          "The AudioWorklet remains the master clock, requesting samples from the Worker to maintain cycle-accurate timing at 48kHz regardless of screen refresh rate or system load.",
      },
      {
        title: "Async RPC proxy",
        description:
          "All WASM function calls are transparently proxied through an ES6 Proxy that auto-generates async RPC. Fire-and-forget calls (input, control) skip waiting for responses. Debug windows use batched queries for efficient multi-register reads.",
      },
    ],
  },
  {
    week: "March 7, 2026",
    features: [
      {
        title: "Screen text capture",
        description:
          "Added captureScreenText agent tool to read text content from the Apple //e screen, with optional row/column range parameters. Works with 40 and 80-column modes.",
      },
      {
        title: "Screen capture",
        description:
          "Added captureScreenshot agent tool to capture the emulator display as a base64 PNG image.",
      },
    ],
    fixes: [],
    improvements: [],
  },
  {
    week: "February 28, 2026",
    features: [
      {
        title: "Cursor keys as joystick",
        description:
          "Added a cursor keys joystick toggle in the Monitor title bar that remaps arrow keys to joystick input with full deflection. The JOY label highlights green when active.",
      },
      {
        title: "CP/M disk library",
        description:
          "Added CP/M disk images to the built-in disk library.",
      },
    ],
    fixes: [
      {
        title: "Release notes crash",
        description:
          "Fixed a crash in the release notes window caused by missing fixes array and null reminderController.",
      },
    ],
    improvements: [],
  },
  {
    week: "February 22, 2026",
    features: [
      {
        title: "Super Serial Card",
        description:
          "Added Super Serial Card (SSC) emulation with ACIA 6551 chip and a built-in WebSocket-to-TCP proxy, enabling serial communication from the Apple //e to external services.",
      },
      {
        title: "Hayes modem emulator",
        description:
          "Added Hayes-compatible modem emulation for SSC serial connections, supporting AT command set for BBS and dial-up software.",
      },
      {
        title: "Microsoft Z-80 SoftCard",
        description:
          "Added Z-80 SoftCard expansion card emulation with full Z80 CPU, enabling CP/M software to run on the Apple //e. Includes address translation matching real hardware and CPU switching via I/O and memory-mapped access.",
      },
    ],
    fixes: [],
    improvements: [
      {
        title: "Performance optimizations",
        description:
          "Reduced CPU/GPU usage through render loop and audio path optimizations for smoother operation and lower power consumption.",
      },
      {
        title: "Code reorganization",
        description:
          "Reorganized expansion card and CPU source files into per-card subdirectories, moved emulator split files into emulator/ subdirectory, and removed dead code and unused assets.",
      },
      {
        title: "Feature flags system",
        description:
          "Added a feature flags system to hide unreleased UI features during development.",
      },
    ],
  },
  {
    week: "February 17, 2026",
    features: [
      {
        title: "No-Slot Clock (DS1215)",
        description:
          "Added emulation of the DS1215 No-Slot Clock, a ProDOS-compatible real-time clock that piggybacks on the 80-column firmware ROM at $C300. Provides automatic date/time stamping in ProDOS without occupying an expansion slot. Enable it via the toggle in the Expansion Slots window.",
      },
    ],
    fixes: [
      {
        title: "WASM crash on card removal",
        description:
          "Fixed a crash when removing Mockingboard or Disk II cards from their slots.",
      },
    ],
    improvements: [
      {
        title: "Code organization",
        description:
          "Extracted debug facilities and state serialization from emulator.cpp into separate files for better maintainability.",
      },
    ],
  },
  {
    week: "February 16, 2026",
    features: [
      {
        title: "AI Agent integration",
        description:
          "Full AI agent control via Model Context Protocol (MCP) and AG-UI event protocol. Agents can manage the emulator, load disks and hard drive images, read/write BASIC programs, assemble code, browse disk files, and configure expansion slots — all programmatically.",
      },
      {
        title: "Agent version checking",
        description:
          "The emulator validates the connected agent server version to prevent incompatible connections. Version mismatches are reported clearly in the status bar.",
      },
      {
        title: "Agent connection management",
        description:
          "Single-client enforcement with the ability to reclaim the port from a stale connection. Port conflict detection and graceful disconnect/reconnect handling.",
      },
    ],
    fixes: [
      {
        title: "Rule Builder empty on refresh",
        description:
          "The Condition Rule Builder no longer appears empty after a page refresh. The window stays hidden until explicitly opened via a breakpoint edit action.",
      },
    ],
  },
  {
    week: "February 15, 2026",
    features: [
      {
        title: "BASIC conditional breakpoints",
        description:
          "Set conditional breakpoints on BASIC variables and arrays using the Rule Builder. Supports simple variables (e.g. break when SCORE >= 1000), 1D arrays (e.g. A(5) == 42), and 2D arrays (e.g. G(2,3) == 23). Conditions are evaluated natively in C++ at every BASIC statement boundary for accuracy.",
      },
      {
        title: "Condition-only rules",
        description:
          "Add breakpoint rules that aren't tied to a specific line — they evaluate on every BASIC statement and break wherever the condition becomes true. Access via the 'if...' button in the breakpoint toolbar.",
      },
      {
        title: "Breakpoint trigger indicators",
        description:
          "When a breakpoint fires, the triggered item in the breakpoint list pulses red and the source line highlights in red (instead of the usual blue stepping highlight), making it clear which breakpoint stopped execution.",
      },
    ],
    fixes: [
      {
        title: "BASIC editor gutter scroll",
        description:
          "The breakpoint gutter no longer scrolls independently of the editor — it stays locked to the editor content.",
      },
      {
        title: "2D array breakpoint formula",
        description:
          "Fixed the flat index calculation for 2D array variable watches to match Applesoft's column-major storage order.",
      },
    ],
  },
  {
    week: "February 14, 2026",
    features: [
      {
        title: "BASIC Stop button",
        description:
          "New Stop button in the BASIC debugger toolbar sends Ctrl+C to the emulator to break a running program. Also unpauses the emulator if paused so the keystroke is processed.",
      },
      {
        title: "Game controller support",
        description:
          "Physical game controllers are now detected and functional via the Gamepad API. The left stick maps to paddle values and buttons A/B map to Apple II buttons 0/1, with configurable deadzone.",
      },
    ],
    fixes: [
      {
        title: "Save states light theme",
        description:
          "Fixed the Save States window rendering with hardcoded dark colours. All values now use CSS theme variables for correct light and dark theme appearance.",
      },
      {
        title: "Window focus click-through",
        description:
          "Buttons and interactive controls in unfocused windows now respond on the first click instead of requiring a second click after focusing the window.",
      },
      {
        title: "Screen window keyboard focus",
        description:
          "Clicking the emulator screen window now immediately gives the canvas keyboard focus, so keystrokes reach the emulator without needing a second click.",
      },
      {
        title: "BASIC tokenizer empty lines",
        description:
          "Lines with only a line number and no code are now skipped when writing a BASIC program to memory, preventing unintended line deletions.",
      },
    ],
  },
  {
    week: "February 13, 2026",
    features: [
      {
        title: "BASIC line heat map",
        description:
          "New Heat toggle in the BASIC editor toolbar shows a colour-coded gutter (blue to red) indicating how frequently each line executes. Driven by cycle-accurate C++ tracking at every BASIC statement, with smooth decay so lines fade when no longer active.",
      },
      {
        title: "BASIC trace toggle",
        description:
          "New Trace toggle lets you disable current-line highlighting while a BASIC program runs, reducing visual noise for long-running programs.",
      },
      {
        title: "BASIC editor improvements",
        description:
          "Statement hover highlighting, live current-line tracking while running, tokenizer fixes, and 30fps refresh rate for smoother updates.",
      },
      {
        title: "Window z-index persistence",
        description:
          "Window stacking order is now saved and restored between sessions, so your window layout is exactly as you left it.",
      },
      {
        title: "Click-to-focus windows",
        description:
          "Clicking an unfocused window now only brings it to front — buttons and inputs don't activate until the window has focus.",
      },
      {
        title: "Window switcher completeness",
        description:
          "The window switcher (Ctrl+`) now includes all windows: Hard Drives, File Explorer, and Trace Panel were missing.",
      },
    ],
    fixes: [
      {
        title: "Disk drives window sizing",
        description:
          "Fixed disk drives window being too small for new users with no saved state.",
      },
      {
        title: "Agent connection",
        description:
          "Fixed app connection to the MCP agent server.",
      },
    ],
  },
  {
    week: "February 12, 2026",
    features: [
      {
        title: "Expansion Slots redesign",
        description:
          "Redesigned the Expansion Slots window with a drag-and-drop card tray and motherboard layout for intuitive slot management.",
      },
      {
        title: "Joystick window redesign",
        description:
          "Redesigned the joystick window with a circular pad, gauge bars, and LED-style buttons for a more tactile feel.",
      },
      {
        title: "Update badge on Help button",
        description:
          "Service worker updates now show a badge on the Help button instead of auto-reloading the page.",
      },
    ],
    fixes: [
      {
        title: "SmartPort ProDOS boot",
        description:
          "Fixed SmartPort card breaking ProDOS floppy boot and slow agent text input speed.",
      },
      {
        title: "SmartPort crash on empty drive",
        description:
          "Fixed a crash when booting with a SmartPort card installed but no disk image loaded.",
      },
      {
        title: "Window state persistence",
        description:
          "Fixed slot config and disk drives window state not persisting across browser refresh.",
      },
      {
        title: "Slot config UX",
        description:
          "Merged the slot config warning into the Apply button and fixed window sizing issues.",
      },
      {
        title: "Window centering",
        description:
          "Windows now center in the viewport when no saved position exists instead of appearing at origin.",
      },
      {
        title: "CRT static noise",
        description:
          "Reduced CRT static noise block size for a finer grain effect.",
      },
    ],
  },
  {
    week: "February 9, 2026",
    features: [
      {
        title: "BASIC tokenizer in WASM",
        description:
          "Moved the Applesoft BASIC tokenizer from JavaScript to C++ WebAssembly for faster and more accurate tokenization. Fixed detokenizer spacing for numbers and variables.",
      },
      {
        title: "Assembler symbol integration",
        description:
          "Assembler symbols are now automatically loaded into the CPU debugger, so breakpoints and disassembly show your label names. Added a Debug button to the assembler toolbar.",
      },
      {
        title: "Instruction Trace window",
        description:
          "New debug window that records a full disassembled instruction trace with auto-scroll, clear, and column-aligned display.",
      },
      {
        title: "Disk Library",
        description:
          "Added a Disk Library window for one-click loading of bundled disk images, cached locally in IndexedDB for instant access.",
      },
      {
        title: "SmartPort hard drive UI",
        description:
          "SmartPort Drives window now uses a side-by-side layout with drive separators. Added toast notifications and slot validation.",
      },
      {
        title: "Browse button on floppy drives",
        description:
          "Floppy disk drives now have a Browse button that opens the File Explorer for the inserted disk.",
      },
      {
        title: "Custom confirm dialogs",
        description:
          "Replaced all native browser confirm() dialogs with styled in-app modals that match the emulator theme.",
      },
      {
        title: "CSS bundling via Vite",
        description:
          "Moved all CSS into src/css/ so Vite can bundle and minify stylesheets for production builds.",
      },
    ],
    fixes: [
      {
        title: "Warm reset behavior",
        description:
          "Warm reset (Ctrl+Reset) now resets soft switches and stops the disk motor like real hardware, while preserving memory contents.",
      },
      {
        title: "Disk II write support",
        description:
          "Fixed Disk II write failures by correctly converting the level signal to flux transitions.",
      },
      {
        title: "Disk drives window layout",
        description:
          "Fixed Browse button styling, widened the drives window to fit content, and constrained it to the viewport after resize.",
      },
      {
        title: "Cold reset cleanup",
        description:
          "Clearing stale frame sync and BASIC state on cold reset to prevent ghost state from previous sessions.",
      },
    ],
  },
  {
    week: "February 2, 2026",
    features: [
      {
        title: "65C02 assembler editor",
        description:
          "Full-featured assembler with syntax highlighting, gutter line numbers, breakpoints, validation, error display, ROM routine reference panel, and file save/load.",
      },
      {
        title: "BASIC debugger",
        description:
          "Added breakpoints, statement-level stepping, variable inspection and editing, runtime error detection, and line highlighting for Applesoft BASIC programs.",
      },
      {
        title: "SmartPort expansion card",
        description:
          "New SmartPort hard drive controller supporting two ProDOS block devices with a self-built ROM. Includes pulsing LED activity indicator and hard drive file browsing.",
      },
      {
        title: "Light and dark themes",
        description:
          "Added light, dark, and system-follow theme support. All accent colors are derived from the six-stripe Apple rainbow logo palette.",
      },
      {
        title: "Save States window",
        description:
          "New save states manager with an autosave slot and five manual save slots, including high-res hover previews of each state.",
      },
      {
        title: "Mouse Interface Card",
        description:
          "Apple Mouse Interface Card emulation using the AppleWin PIA command protocol, with a dedicated debug window showing PIA registers and position.",
      },
      {
        title: "Per-scanline raster rendering",
        description:
          "Video output is now rendered scanline-by-scanline with sub-scanline precision and a 2-cycle pipeline delay, enabling accurate raster bar effects.",
      },
      {
        title: "Mockingboard improvements",
        description:
          "Unified channel-centric debug window with inline waveforms, level meters, per-channel mute, and MAME-aligned PSG audio engine.",
      },
      {
        title: "Window management",
        description:
          "Added window switcher overlay (Ctrl+`), Option+Tab cycling, focused window highlighting, viewport-lock for the screen, and auto-hiding toolbar in full-page mode.",
      },
      {
        title: "Canvas-based disk surface",
        description:
          "Replaced disk drive PNG images with real-time canvas-rendered disk surfaces showing track position and read/write activity.",
      },
      {
        title: "Core logic moved to WASM",
        description:
          "Migrated debug evaluation, filesystem parsing, BASIC detokenization, screen text extraction, and input handling from JavaScript to C++ WebAssembly.",
      },
      {
        title: "Beam position breakpoints",
        description:
          "CPU debugger can now break at specific scanline and horizontal beam positions, with wildcard support and a multi-beam tab panel.",
      },
      {
        title: "Dev menu",
        description:
          "New Dev menu grouping the Assembler and BASIC Program windows, with a dedicated category in the window switcher.",
      },
      {
        title: "Color bleed CRT effect",
        description:
          "Added a color bleed shader parameter for more authentic CRT monitor appearance.",
      },
      {
        title: "Merlin source viewer",
        description:
          "File Explorer can now display Merlin assembler source files from disk images.",
      },
    ],
    fixes: [
      {
        title: "Disk II accuracy",
        description:
          "Replaced the nibble-at-a-time disk model with a P6 ROM-driven Logic State Sequencer for cycle-accurate disk emulation.",
      },
      {
        title: "Speaker audio quality",
        description:
          "Fixed speaker pitch drift on subsequent beeps and audio mixing clipping when speaker and Mockingboard play simultaneously.",
      },
      {
        title: "AY-3-8910 sound chip",
        description:
          "Fixed noise LFSR polynomial, envelope timing, period-zero handling, PSG phase cancellation, and aligned output with MAME reference.",
      },
      {
        title: "Double Low-Res rendering",
        description:
          "Fixed color rendering using wrong palette and missing auxiliary memory nibble rotation.",
      },
      {
        title: "Paste performance",
        description:
          "Replaced slow keyboard-paste BASIC loading with instant direct memory insertion. Fixed paste queue setTimeout violations.",
      },
      {
        title: "Theme consistency",
        description:
          "Fixed hardcoded dark-theme colors in CPU Debugger and BASIC Program windows.",
      },
    ],
  },
  {
    week: "January 26, 2026",
    features: [
      {
        title: "Expansion card architecture",
        description:
          "Added a pluggable expansion card system matching real Apple IIe hardware, with an Expansion Slots configuration UI for managing cards in slots 1-7.",
      },
      {
        title: "Thunderclock Plus",
        description:
          "ProDOS-compatible real-time clock card that provides the current date and time to software. Configurable for slot 5 or 7.",
      },
      {
        title: "Dropdown menus",
        description:
          "Replaced header buttons with grouped dropdown menus (File, View, Debug, Dev, Help) for a cleaner toolbar.",
      },
      {
        title: "Emulation speed multiplier",
        description:
          "Adjustable speed control for fast-forwarding through slow operations like BASIC program loading and disk access.",
      },
      {
        title: "Mockingboard waveform scope",
        description:
          "Split Mockingboard window into detail and scope views with real-time waveform visualization and channel muting.",
      },
      {
        title: "Stereo audio output",
        description:
          "Mockingboard now outputs in stereo with proper PSG channel separation between left and right speakers.",
      },
      {
        title: "Viewport-lock for screen",
        description:
          "Screen window can be locked to fill the browser viewport, with proper aspect ratio enforcement.",
      },
      {
        title: "Window option persistence",
        description:
          "All window toggle states, view modes, and mute settings are now saved between sessions via localStorage.",
      },
    ],
    fixes: [
      {
        title: "VIA 6522 timer interrupts",
        description:
          "Fixed timer interrupt handling on mode transitions and ensured timers always decrement for proper Mockingboard detection.",
      },
      {
        title: "Mockingboard audio clipping",
        description:
          "Fixed audio timing, clipping, and output normalization issues in the Mockingboard sound engine.",
      },
      {
        title: "PSG register timing",
        description:
          "Added timestamped PSG register writes for cycle-accurate audio, with proper bipolar AC-coupled output.",
      },
      {
        title: "Window positioning",
        description:
          "Constrained all windows to visible viewport bounds and computed sensible default positions.",
      },
      {
        title: "NTSC fringing in monochrome",
        description:
          "Monochrome display modes now correctly skip NTSC color artifact rendering.",
      },
    ],
  },
  {
    week: "January 19, 2026",
    features: [
      {
        title: "Mockingboard sound card",
        description:
          "Dual AY-3-8910 PSG chips with VIA 6522 timers, providing stereo music and sound effects for supported software.",
      },
      {
        title: "BASIC Program window",
        description:
          "Load Applesoft BASIC programs directly into emulator memory with IntelliSense autocomplete for keywords, variables, and line numbers.",
      },
      {
        title: "Drag-to-move",
        description:
          "Monitor and disk drives can now be repositioned by dragging, with viewport constraint to keep everything on screen.",
      },
      {
        title: "Recent disks",
        description:
          "Per-drive recent disks dropdown menus with clear option, so frequently used disk images are always one click away.",
      },
      {
        title: "Disk persistence",
        description:
          "Inserted disk images and any modifications are preserved across browser sessions automatically.",
      },
      {
        title: "Joystick window",
        description:
          "Floating joystick window for paddle and joystick input with snap-back-to-center behavior.",
      },
      {
        title: "CRT shader enhancements",
        description:
          "Added rounded corners, edge highlights, and color fringing toggle for more realistic CRT monitor appearance.",
      },
      {
        title: "Help system",
        description:
          "Comprehensive help window (F1) with documentation, plus release notes page with automatic update checking.",
      },
    ],
    fixes: [
      {
        title: "65C02 CPU compliance",
        description:
          "Fixed CPU emulation bugs discovered by Klaus Dormann's 65C02 functional test suite.",
      },
      {
        title: "Double Lo-Res colors",
        description:
          "Corrected the color palette mapping for Double Lo-Res graphics mode.",
      },
      {
        title: "Disk II stepper motor",
        description:
          "Fixed stepper motor timing to match real hardware quarter-track behavior.",
      },
      {
        title: "Hi-Res color rendering",
        description:
          "Fixed alternating fill patterns for continuous colored lines and artifact color accuracy.",
      },
      {
        title: "DSK disk corruption",
        description:
          "Fixed a bug that corrupted DSK disk images during write operations.",
      },
      {
        title: "Keyboard and DHGR",
        description:
          "Fixed keyboard input issues and Double Hi-Res rendering problems.",
      },
    ],
  },
  {
    week: "January 12, 2026",
    features: [
      {
        title: "File Explorer",
        description:
          "Browse the contents of DOS 3.3 and ProDOS disk images, view BASIC listings with syntax formatting, and disassemble binary files with recursive descent flow analysis.",
      },
      {
        title: "C++ disassembler",
        description:
          "New disassembler running in WebAssembly with virtual scrolling, categorized symbols, clickable jump targets, and tooltips.",
      },
      {
        title: "Monochrome display modes",
        description:
          "Green, amber, and white phosphor display modes that bypass NTSC artifact coloring for a classic terminal look.",
      },
      {
        title: "Debug window system",
        description:
          "Movable, resizable debug windows with viewport constraints, minimum size enforcement, and state persistence. Includes Memory Map, heat map, and soft switch monitor.",
      },
      {
        title: "Display settings window",
        description:
          "Converted display settings to a movable window with improved layout, brightness, contrast, saturation, and CRT effect controls.",
      },
      {
        title: "Text selection",
        description:
          "Select and copy text directly from the emulator screen with Ctrl+C support and proper Apple II screen code conversion.",
      },
      {
        title: "Full-page mode",
        description:
          "Expand the emulator to fill the entire browser window, exit with Ctrl+Escape.",
      },
      {
        title: "Mobile layout",
        description:
          "Responsive layout with virtual keyboard support for mobile devices.",
      },
      {
        title: "PWA support",
        description:
          "Progressive Web App with offline functionality, service worker caching, and an update notification button.",
      },
      {
        title: "UK/US character set",
        description:
          "Toggle between UK and US character ROMs with persistent setting.",
      },
      {
        title: "Sound controls",
        description:
          "Volume slider, mute toggle, and disk drive seek sound with persistent settings.",
      },
      {
        title: "State persistence",
        description:
          "Auto-save emulator state on exit and restore on reload, including all window positions and sizes.",
      },
    ],
    fixes: [
      {
        title: "Memory banking",
        description:
          "Fixed Language Card write behavior, double-read requirement, 80STORE banking, and expansion ROM space handling ($C800-$CFFF).",
      },
      {
        title: "Soft switches",
        description:
          "Fixed read side effects for $C000-$C00F, INTCXROM handling, and comprehensive soft switch support.",
      },
      {
        title: "WOZ disk timing",
        description:
          "Fixed WOZ disk write/read timing and added disk image export functionality.",
      },
      {
        title: "Hi-Res and Double Hi-Res",
        description:
          "Fixed DHR mode detection, 80-column text rendering, and hi-res graphics artifact colors.",
      },
      {
        title: "Character ROM",
        description:
          "Fixed Enhanced character ROM rendering and flash character display.",
      },
    ],
  },
  {
    week: "January 5, 2026",
    features: [
      {
        title: "Initial release",
        description:
          "Apple //e emulator with cycle-accurate 65C02 CPU, audio-driven frame sync at 60Hz, Disk II controller with DSK/DO/PO/NIB/WOZ support, WebGL rendering, and CRT shader effects.",
      },
    ],
    fixes: [],
  },
];
