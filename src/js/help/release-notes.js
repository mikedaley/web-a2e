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
    week: "September 16, 2026",
    features: [
      {
        title: "Two more machines: the Apple IIc and the Apple IIgs",
        description:
          "The IIc is a //e in a slab, with no expansion sockets but every slot address answered by a part soldered to the board: two serial ports, the 80-column firmware, an IOU mouse and an IWM driving the drive in its case. The IIgs is a different computer, built from its own parts: a 65C816 at 2.8 MHz on a 24-bit bus, 256K to 8M of fast RAM, Super Hi-Res, an Ensoniq, ADB, and a battery-backed clock. It boots GS/OS System 6.0.4 to the Finder with a working mouse and sound.",
      },
      {
        title: "Save states know which machine wrote them",
        description:
          "Every state now starts with the id of the machine that saved it. Load one from another machine and the emulator offers to switch to that machine first, rather than refusing. Save slots and the autosave are kept per machine.",
      },
      {
        title: "The menus, the picture and the keyboard follow the machine",
        description:
          "Menu items for hardware a machine does not have are hidden rather than greyed out. Display settings are remembered per machine, each with its own screen border default. On the IIgs the Command key is Open Apple, so GS/OS menu shortcuts work; View > \u2318 as Open Apple is the switch.",
      },
      {
        title: "The debugger works on every machine",
        description:
          "Addresses are 24 bits throughout and registers 16, so a 6502 and a 65816 answer the same windows. Disassembly is chosen by the core, because a 65816's instruction lengths depend on its register-width flags. Breakpoints, watchpoints, the trace and beam breakpoints are shared.",
      },
      {
        title: "The IIgs takes expansion cards",
        description:
          "All seven sockets accept a Mockingboard, Mouse Card, Thunderclock, Super Serial Card, Parallel Card or SmartPort. Each slot also has a device built into the machine, and only one of the two answers at a time, so the Expansion Slots window carries the Control Panel's per-slot setting beside each socket. Your choice survives a reboot, which the firmware would otherwise overwrite.",
      },
      {
        title: "A long print no longer runs off the end of the paper",
        description:
          "The paper now scrolls through a window of live pages, so a print of any length comes out whole. Exports cover the whole job rather than whatever was still on screen.",
      },
    ],
    fixes: [
      {
        title: "A GS/OS install disk would stop at the splash screen",
        description:
          "Three faults in the ADB controller: read-memory took one argument byte instead of two, ADB bus commands were not decoded, and a Talk was never answered. The firmware timed out and unwound into the monitor, hidden behind the splash screen. Those disks now boot to the Finder.",
      },
      {
        title: "Saving on a IIgs with two hard drives did nothing at all",
        description:
          "A SmartPort card's saved state is its disk images, and writing a 72MB state needed about 190MB because each card was copied twice. That stopped the emulator outright, which is why the button looked dead. Card state is now written straight into the save, and a save that fails says so.",
      },
      {
        title: "A GS/OS print came out sliced in half",
        description:
          "The ImageWriter driver sets the paper feed for each 8-dot band, and that escape sequence was breaking the carriage return and line feed pairing, so every band fed twice.",
      },
      {
        title: "The screen could refuse to draw at all",
        description:
          "With the monitor's edge highlight turned up, the renderer threw an error on every frame and drew nothing.",
      },
      {
        title: "The IIgs's sound was wrong in three ways",
        description:
          "The Ensoniq's oscillators were split by channel, so a game's bass came out of one speaker and its melody the other; a stock machine hears the sum. The volume nibble was applied as a ratio rather than a taper, leaving the machine 9.5dB quiet, and it read back as full volume however it had been set.",
      },
    ],
  },
  {
    week: "September 10, 2026",
    features: [
      {
        title: "There is a second machine: the Apple II Plus",
        description:
          "The badge in the header is no longer a picture of a //e — it names the machine you are running, and clicking it lets you choose one. There are two: the Apple //e, and the Apple II Plus, the machine most of the software you remember was written on. Its differences are real rather than cosmetic. It has an NMOS 6502 instead of a 65C02, no auxiliary bank at all — which is what makes 80 columns and every double-resolution mode genuinely unavailable rather than merely hidden — its own character generator, only one character set, and it never switches off the colour burst, so its text fringes green and violet in every mode exactly as the real machine's did. Slot 0 exists and holds the language card that turns a 48K machine into the 64K one nearly all II+ software expects. Switching machines rebuilds the machine from scratch, so anything in the drives or in memory is lost just as it would be on a reload — the menu says so before it does it — but your display settings, volume, character set and speed follow you across, because those were your choices and not the machine's. Each machine remembers its own slot layout, so a card you fitted to one does not turn up in the other. The Apple II Plus needs its own ROMs, which are not distributed with the emulator; without them the machine is still listed but is marked as unavailable rather than quietly failing to start.",
      },
      {
        title: "A Sirius Joyport, for two digital joysticks",
        description:
          "The Joystick window has a new Game port selector, and it chooses what is plugged into the machine's game connector: the usual Apple joystick, or a Sirius Joyport. The Joyport was Sirius Software's 1981 adapter, and it put two Atari-style digital sticks on the same connector — switches instead of potentiometers, so there was no capacitor to wait for, and two players instead of one. Look at almost any Sirius game and it offers you \"Apple Joystick\" or \"Joyport\"; now the second option works. Choose it and the window swaps its paddle knob for a pair of digital sticks you can work with the mouse, and a connected game controller drives one from either its D-pad or its left stick. Connect two controllers and each one gets a stick; with only one connected it drives both, so a game that reads the second stick still plays. Your choice is remembered, and survives a reset and a change of machine. One small liberty is taken with the hardware: a Joyport holds the machine's two button lines high when nothing is pressed, and on a //e those lines are the Open and Closed Apple keys, so a real //e with a Joyport fitted ran its self test at every reset instead of booting. Here the Joyport lets go of them for an instant after a reset, so the machine starts normally and the sticks work the rest of the time.",
      },
      {
        title: "A font editor for the printers",
        description:
          "The glyph banks the virtual printers render from — ImageWriter I and II, the Epson FX-80, the Apple DMP — can now be authored in a standalone editor at /printers/rom-editor.html. It draws characters dot by dot, handles the alternate-language code points each printer swapped in per locale, imports and exports either as a ROM module or as ASCII dot art, and can trace over a scan of a manual's character chart so a font can be rebuilt from the page it was printed on. It is one plain page with no build step, so it opens straight off disk.",
      },
    ],
  },
  {
    week: "August 24, 2026",
    features: [
      {
        title: "A shared link can start the machine for you",
        description:
          "Add ?autostart to a link and the //e powers on and boots as the page finishes loading, with nothing to click. Send someone a link to a disk and they watch it boot, rather than arriving at a dark screen and having to work out which button starts a computer they have never used. The machine starts silent, because no browser will let a page make sound until someone has interacted with it — click or type anything and the speaker joins in. Clicking the power button yourself still works exactly as before, and the \"No disk? Press Ctrl+Reset\" hint stays out of the way when the link has already put a disk in the drive.",
      },
    ],
    fixes: [
      {
        title: "A powered-on machine could sit frozen instead of running",
        description:
          "The emulation is paced by the audio hardware clock — it runs the processor for exactly as long as the sound card asks for sound — which is what keeps the speed steady. But a browser refuses to start audio until someone has interacted with the page, so on a page nobody had touched yet there was nothing asking, and a machine that had been switched on stood still: powered, lit, and not running. The emulator now paces itself from a timer for as long as audio is asleep, and hands back to the audio clock the moment it wakes, so the machine runs from the instant it is switched on whether or not anyone has touched anything yet.",
      },
      {
        title: "The agent could not load source into the Assembler window",
        description:
          "Setting the assembler's source through the agent — pasting a program in, or loading one from a file — failed outright, because it was still calling a routine the editor dropped when its gutter started coming from the real assembler. The editor now simply revalidates and redraws, which is what the removed routine had been there to prompt.",
      },
    ],
  },
  {
    week: "August 21, 2026",
    features: [
      {
        title: "A real Merlin assembler",
        description:
          "The Assembler window used to accept a plain list of 65C02 instructions and little else. It is now a Merlin-compatible assembler in the emulator's core, and Merlin source written in 1985 assembles as written: macros with parameters and their own local labels, conditional assembly, LUP loops, dummy sections for laying out structures, local labels and variables, the full set of data and string directives, PUT and USE reading included source straight off the disk in a drive, DSK and SAV writing the object back to one, and SW for Woz's Sweet-16 interpreter. It follows Merlin's conventions rather than a generic assembler's — expressions run strictly left to right with no operator precedence, so 1+2*3 is 9, because that is what the sources you might load were written against. Directives that need a linker, and the 65816 ones a //e cannot run, are reported instead of quietly doing nothing. A Problems panel lists every error and warning from the last assembly, and clicking one goes to its line.",
      },
      {
        title: "The gutter now shows what was really assembled",
        description:
          "The addresses, cycle counts and bytes beside each line used to come from a second, simpler assembler living in the editor, which meant it could not follow a macro or a conditional and quietly disagreed with the code that was actually produced. The gutter is now filled in by the real assembler, so a line inside a conditional that was not taken shows nothing, a macro call is credited with the bytes its expansion produced, and cycle counts are per opcode rather than per instruction name — LDA $10 and LDA $1000 differ, as they do on the chip.",
      },
    ],
    fixes: [
      {
        title: "Pasted text was arriving with characters missing",
        description:
          "Paste a program into some titles and roughly every second character vanished, while other programs took the same paste perfectly — which made it look like the paste was too fast rather than wrong. Clearing the keyboard strobe twice is how an Apple II flushes the keyboard, and the ROM, DOS and countless programs do it before settling down to wait for input. A person typing leaves nothing pending for a flush to throw away; the emulator was handing each flush a fresh character. Pasted text now goes into a type-ahead buffer inside the machine, and a character becomes available a short interval after the last one was taken — longer after a carriage return, because the machine has a line to tokenise and a command to run before it asks for the next one. Nothing can now be overwritten before it has been read.",
      },
      {
        title: "The machine thought a key was held down forever",
        description:
          "The //e reports whether a key is physically held on a separate line from the one that reports a key waiting to be read, and games and key-repeat code watch it. It went high on the first keystroke of a session and stayed there, because something asserted it with nothing to release it. It is now derived from the only two things that can hold a key down — a key you are actually holding, and a pasted character the program has not read yet — so it releases on its own. Held keys are tracked by the physical key, so letting go of Shift first cannot strand one, auto-repeat cannot double-count, and clicking away from the window releases everything rather than losing the key-up.",
      },
      {
        title: "Typing on a phone or tablet dropped characters",
        description:
          "An on-screen keyboard can deliver several characters in one go — autocorrect finishing a word, a swipe, a paste into the input field — and each one was written straight into the keyboard latch, overwriting whatever the machine had not read yet. Mobile typing now goes through the same type-ahead buffer as a paste, so the machine gets all of it at its own pace.",
      },
    ],
    improvements: [
      {
        title: "Pasting no longer fights the emulator for time",
        description:
          "The old paste ran the CPU itself from the browser's main thread in small bursts, asking the emulator whether the keyboard was ready once per character and translating each character with a separate call — three round trips per key, competing with the audio-driven emulation for the same machine. The text now crosses into the emulator in whole lines, a paste of any size costs a fixed handful of calls, and the machine pulls characters out at its own pace. All the host still does is drop the speed boost and notice when the paste has finished.",
      },
    ],
  },
  {
    week: "August 15, 2026",
    features: [
      {
        title: "Run the machine faster than a real one",
        description:
          "View > CPU Speed picks 1x, 2x, 4x or 8x of the //e's 1.023 MHz — 8x is roughly what an accelerator card gave you in 1986, and it turns a slow BASIC listing, a long compile or a ProDOS copy from something you wait for into something that is simply done. The sound speeds up with it. Everything the speaker and the Mockingboard do happens in a fraction of the time and rises in pitch to match, exactly as it did on accelerated hardware, so you can hear how fast the machine is going rather than only see it. The picture still runs at sixty frames a second; the machine just gets more done between them. Any setting above 1x shows a small chip in the Monitor title bar so a fast machine is never a mystery, and your choice is remembered between sessions and survives a reset or a reboot — a speed you chose is not something a restart should quietly take away from you. Pasting still sprints to 8x for the duration of the paste and then hands the machine back to whatever speed you picked.",
      },
      {
        title: "Save As for assembly and BASIC source",
        description:
          "There was no way to choose the name of a source file once one was in play. Save asked for a name the first time and never again, and opening a file meant never being asked at all, so the only way to save under a different name was to start a new file and lose what you had. Both editors now have a Save As button next to Save, on Ctrl or Cmd with Shift and S. Save writes back to the file you are working on without interrupting; Save As always asks. On Safari and Firefox, which cannot offer a system save dialog, saving used to drop a file into your downloads under a name chosen for you — the BASIC editor always called it program.bas whatever the program was — so both now ask for a name whenever it is not already settled, the same way saving a disk image does.",
      },
      {
        title: "Real composite colour, decoded from the real signal",
        description:
          "An Apple //e does not send a monitor pixels. It sends one bit per 14.31818 MHz dot, four dots to a cycle of the colour subcarrier, and every colour you have ever seen on one was manufactured by the monitor from that stream of bits. The emulator used to skip all of that: it looked up colours from a table and then tinted the edges afterwards to suggest fringing. It now generates the dot stream the machine really produces and decodes it the way a monitor does, and several things that were previously approximated fall out of that on their own. The high bit of a hi-res byte is a real half-pixel delay again rather than a swap to a different set of colours, which is the actual reason orange and blue exist and why they sit half a pixel to the right of green and violet. Colour burst is modelled, so text is colourless because the machine stops sending a colour reference during it, not because white was written into the code. And artifact colours and lo-res colours are finally the same sixteen colours, because they are the same mechanism — something the old hand-tuned tables disagreed about. Text on a mixed graphics screen fringes green and violet, exactly as it does on real hardware, while a full screen of text stays crisp and white.",
      },
      {
        title: "Save your own display profiles",
        description:
          "The Monitor dropdown offered five monitors and no way to keep one of your own. Tune the picture however you like and Save As gives it a name; it then appears under My Profiles alongside the built-in monitors. Adjusting a saved profile keeps its name and offers Save, which writes the changes back without asking anything, so refining one does not mean naming it again every time. Unlike the built-in monitors, which deliberately leave your brightness, contrast, saturation and bezel alone, a profile remembers everything — it is a snapshot of a picture you liked, so selecting it gives that picture back whole. Profiles are kept separately from the rest of the display settings, so Reset to Defaults does not delete them.",
      },
    ],
    fixes: [
      {
        title: "Double hi-res was showing the wrong colours",
        description:
          "Every double hi-res picture was a quarter turn out around the colour wheel — greens for blues, pinks for lavenders — which is subtle enough to look like a stylistic choice rather than a fault. The 80-column video path is clocked one dot later than the 40-column one, so the same pattern of dots lands on the next point of the colour cycle and comes out a different colour. That one dot was missing.",
      },
      {
        title: "Colour where there should have been none",
        description:
          "Pixel Exact was quietly applying a composite effect. It worked out each pixel's colour by looking at its neighbours, which meant colour bled onto dots that were switched off and spilled past the edges of white shapes — the left edge of a white block ran magenta, violet and lavender before reaching white. A sharp mode should not do that. Unlit dots are now black, colour stops at the last lit dot, and edges are hard. Hi-res pixels and text keep the colours their dot patterns really produce; what they no longer do is smear them.",
      },
    ],
    improvements: [
      {
        title: "Composite no longer bends the picture",
        description:
          "The Composite Color monitor came with a curved screen. Real colour sets were curved, but curvature is not what that setting is about — the composite look is in how the colour is decoded, and the barrel distortion mostly made the picture harder to read. It is now flat, and this reaches anyone who already had Composite selected rather than only new users. Screen Curvature is still under Advanced.",
      },
      {
        title: "The NTSC Fringing slider has gone",
        description:
          "It faked composite artifacts by tinting edges it detected in an already-finished picture, which meant it added colour to things that were never encoded in the first place and missed colour that should have been there. Now that fringing comes out of the signal itself, keeping the slider would only have applied the effect twice.",
      },
    ],
  },
  {
    week: "August 8, 2026",
    features: [],
    fixes: [
      {
        title: "Colour fringing now behaves like a real tube's",
        description:
          "The RGB Offset effect pulls the red, green and blue images slightly apart, imitating the three electron beams not landing in quite the same place. It was doing so evenly across the screen, which left visible colour fringing in the middle of the picture and no more of it in the corners than at the edges — the reverse of a real monitor, where the beams are aligned dead centre and drift further apart the further out they are steered. It is now clean through the middle and worst in the corners, slightly stronger left-to-right than top-to-bottom because a picture tube deflects the beam through a wider angle horizontally. The fringing is also fixed to the screen rather than sliding about with the Jitter and Horizontal Sync effects, since where the beams land is a property of the tube and not of the signal.",
      },
    ],
    improvements: [
      {
        title: "The bezel no longer pretends to reflect the screen",
        description:
          "The surround around the picture tried to show a reflection of what was on screen. A bezel is matte plastic, and a matte surface scatters light instead of forming an image, so there is no reflection to see on a real monitor however hard you look — the effect was imitating something that does not happen. What a real bezel does is simply catch a little light from a bright screen, which amounts to a faint lightening of the innermost few millimetres and nothing more, so it has been taken out rather than approximated. The Spill Reach and Spill Intensity sliders have gone with it. The bezel keeps its own shading, colour and texture; it just no longer changes with the picture.",
      },
    ],
  },
  {
    week: "August 5, 2026",
    features: [],
    fixes: [],
    improvements: [
      {
        title: "Phosphor that fades like phosphor",
        description:
          "The Burn In effect, which leaves a fading afterimage behind moving graphics, was subtracting a fixed amount of brightness on every pass. That made a dim trail take just as long to disappear as a bright one, and gave the fade an abrupt end where it hit black. Real phosphor fades quickly at first and then lingers, which is why a trail on a picture tube has a long faint tail rather than simply stopping. It now does the same. A colour tube's three phosphors also do not fade together — green holds on longest and blue goes first — so the trail behind moving white text now tints green as it dies, exactly as it does on real hardware. The green and amber monochrome modes instead fade every colour at one rate, because a monochrome tube has a single phosphor coating and nothing to tint, and they hold their image around half again as long. The Burn In slider now sets how long the phosphor holds rather than how much brightness to remove, and the fade no longer runs faster or slower depending on how busy your machine is.",
      },
    ],
  },
  {
    week: "August 4, 2026",
    features: [],
    fixes: [
      {
        title: "Ejecting a disk kept saving a copy of it",
        description:
          "Ejecting a disk produced a saved image whether or not anything had been written to it, which quietly filled the downloads folder with copies. There were two reasons. A blank disk was marked as changed the moment it was created, so ejecting one you had never written to always offered to save an empty disk; creating a disk is not a change to it, and it now starts clean. And the emulator was only asking whether a write had happened at some point rather than whether the disk was actually different — software rewrites parts of a disk with identical content as a matter of course. It now compares the disk against the one that went in, and ejects silently when they match. A disk you really have changed still offers to save, as before.",
      },
      {
        title: "Naming a disk image when saving it",
        description:
          "On Safari and Firefox, saving an ejected disk dropped a file straight into your downloads with a name chosen for you and no way to decline. Those browsers do not give web pages the system save dialog that Chrome and Edge do, so there is now a dialog asking what to call the image, with a Cancel button. Chrome and Edge continue to use the real system save dialog.",
      },
      {
        title: "Expansion slot defaults disagreed with themselves",
        description:
          "The default card layout was written down twice in the code and the two copies had Thunderclock and SmartPort in opposite slots. It only showed if the emulator could not report its own configuration, at which point the slot window would have described two slots wrongly. There is now a single definition.",
      },
    ],
    improvements: [],
  },
  {
    week: "August 2, 2026",
    features: [],
    fixes: [
      {
        title: "The no-signal screen no longer shows a power symbol",
        description:
          "The screen shown while the machine is off had a power symbol drawn beneath the message. It was part of the picture rather than a control — the emulator's screen does nothing with a click — so it invited people to press the one thing on screen that could not work, instead of the power button in the toolbar. It has been removed and the message re-centred.",
      },
    ],
    improvements: [],
  },
  {
    week: "July 31, 2026",
    features: [
      {
        title: "Monitor presets",
        description:
          "Display Settings now opens with a single choice: which monitor you are pretending to have. Composite Color for a colour television or composite monitor, RGB Monitor for the sharp separate-signal look, Monochrome Green or Monochrome Amber for the two phosphor tubes, or Pixel Exact for no simulation at all. Each sets the whole picture at once, and the values follow from the real hardware — the composite set gets a dot triad mask and colour fringing, the RGB monitor gets neither because there is no encoded signal to decode, and the monochrome tubes get long persistence and no mask at all, since a mask exists only to keep three beams apart and a monochrome tube has a single one. Every individual slider is still there under Advanced. Presets leave your brightness, contrast, saturation and bezel alone, and adjusting anything a preset covers relabels it as Custom without changing what you set.",
      },
      {
        title: "Cursor Keys as joystick is now in the View menu",
        description:
          "The toggle that makes the arrow keys drive the joystick only existed in the Monitor window's title bar, which is not on screen in the Play, Code or Debug layouts — so in those layouts the setting could not be reached at all. It is now in the View menu as well, and the two stay in step with each other.",
      },
      {
        title: "A shadow mask you can actually see",
        description:
          "The Shadow Mask slider was measuring its pattern in screen pixels rather than in the fixed spacing a real mask has, so on a high-resolution display the whole red-green-blue triad spanned three physical pixels and was very nearly invisible — and it changed size if you moved the window to a different monitor. It now keeps a constant apparent size everywhere. There is also a choice of geometry: the vertical stripes it has always drawn, or the dot triad an Apple Monitor //e actually used.",
      },
    ],
    fixes: [
      {
        title: "Arrow keys stopped working when Cursor Keys was on",
        description:
          "With the Joystick window's Cursor Keys toggle switched on, the arrow keys drove the joystick and nothing else — anything that navigates with them, such as a ProDOS file selector, a catalogue menu or editing a line of BASIC, simply stopped responding. The arrows were being taken by the joystick and never passed on. They now do both jobs at once: the toggle decides whether the arrows also move the joystick, not whether they still work as keys. Reported by anomixer.",
      },
      {
        title: "Every key press was being handled twice",
        description:
          "Each keystroke was picked up by two separate listeners and sent to the emulator twice over. The machine latches a key press, so the second copy made no visible difference and it went unnoticed for a long time — but it meant twice the work for every key typed, and anything attached to a key press happening twice as well.",
      },
      {
        title: "The BASIC viewer lost track of a running program after loading a state",
        description:
          "Save a state while a BASIC program was running, load it back, and the BASIC Program Viewer showed the program as idle: no line highlighting, no live variables, no trace. The emulator works out that a program is running by watching for the moment BASIC starts one, and a restored state resumes in the middle of the program, so that moment never came around again. It now works this out from the restored machine itself. Existing saved states load correctly too.",
      },
      {
        title: "Two effects could trigger photosensitive seizures",
        description:
          "The television static shown when the machine was off rebuilt a full screen of high-contrast noise fifty times a second and varied the brightness of the whole screen twelve times a second on top of that. The Flicker slider did something similar during normal use, holding a random brightness level for a fifteenth of a second at a time. Both sat squarely in the range of flash rates known to trigger seizures, and both exceeded the published limits for how much the brightness of a screen may change and how often. The static has been removed entirely and replaced with a message telling you to switch the machine on. Flicker is now a slow, gentle undulation, which is closer to what a real set does anyway — its brightness drifts as its field rate beats against the mains rather than jumping about. Anyone who had either effect turned up will notice the difference; this is why.",
      },
      {
        title: "The picture did not follow a change of display",
        description:
          "Dragging the window from a high-resolution display to an ordinary one, or the other way, left the picture being drawn at the old display's resolution — too soft or needlessly oversampled — until something else happened to resize the window. It now follows the change immediately.",
      },
    ],
    improvements: [
      {
        title: "Television static that looks like television static",
        description:
          "The static shown when the machine is off had a horizontal grain to it — rows of speckle that appeared to slide sideways rather than the shifting snow of a detuned set. The pattern changed each frame by sliding the same speckle across the screen instead of making a fresh one, two parts of it were banded by row on purpose, and the scanlines laid over the top were strong enough to be the most obvious thing on screen. It is now a white field of black grain that is genuinely different every frame, with a faint colour speckle and an occasional drifting interference bar. The Static Noise slider under display settings has had the same treatment, and now adds grain that darkens as well as lightens rather than washing the picture out.",
      },
      {
        title: "Tidier File menu",
        description:
          "The File menu ended in an empty line under Save States that never had anything to show. The Save States window lists when each slot was saved, including the automatic one, so the menu no longer repeats it.",
      },
      {
        title: "Scanlines that thicken with brightness",
        description:
          "A real picture tube's beam grows wider as it gets brighter, so bright lines are fatter than dark ones and fill more of the gap to their neighbours. It is why white text on a CRT looks bolder than the same text in a screenshot. The scanlines here were a fixed pattern that dimmed everything equally regardless of what was on it, which reads as stripes laid over the picture rather than as a picture made of lines. They now respond to brightness, with a Beam Bloom slider to control how strongly.",
      },
      {
        title: "A no-signal screen instead of snow",
        description:
          "The television static reworked earlier this week is gone, for the accessibility reasons above. With the machine switched off you now get NO SIGNAL, a line telling you to switch it on, and a power symbol — drawn as though the machine itself were producing it, so it picks up whatever screen curvature, scanlines, colour and phosphor settings you have chosen. It is also more accurate than snow: snow is something a television tuner produces, and a //e is plugged into a monitor that has no tuner. Pull the signal from one of those and you get a black screen.",
      },
    ],
  },
  {
    week: "July 29, 2026",
    features: [],
    fixes: [
      {
        title: "The Memory Browser only showed a single row",
        description:
          "Opening the Memory Browser as a floating window showed one row of memory no matter how large you made it. The list works out how many rows to draw by measuring the space available, but that space was being decided by how much had already been drawn — so one row measured as room for one row, and it never recovered. It now fills the window and follows it as you resize. Docked in a workspace it was always fine, which is why it went unnoticed.",
      },
      {
        title: "Run to Cursor never stopped",
        description:
          "Right-clicking a line and choosing Run to Cursor set the breakpoint and then quietly removed it again a fraction of a second later, so the program ran on forever. The debugger was watching the program counter a few dozen times a second to spot arrival, and in a loop it would catch the target address while the processor was still running and treat that as having arrived. It now waits for the emulator itself to report that it has halted. Step Over and Step Out were leaning on the same unreliable check and are steadier for it.",
      },
      {
        title: "The installed app did not work offline",
        description:
          "Installing the emulator as an app appeared to work but it could not start without a network connection. The list of files to store for offline use still named stylesheets that no longer exist under those names, and storing that list fails completely if any single file is missing — so nothing was ever stored. The list is now correct, includes files that were missing outright (the emulator's worker and the screen shaders, both required to start), works out the bundled files for itself so it cannot fall out of date again, and tolerates a missing file rather than giving up on everything.",
      },
      {
        title: "The emulator could freeze after loading a saved state",
        description:
          "Loading a state left the screen stuck on the restored image with no sound, and nothing typed had any effect until the page was clicked. Restoring used to switch the emulator off and on again, which rebuilds the sound system — and the sound card is what paces the emulator, so when the browser held it back until the next click, the machine sat there doing nothing. A machine that is already on now keeps running through the restore. Loading a state from a file also reported success even when the file could not be read.",
      },
    ],
    improvements: [
      {
        title: "Debug windows no longer slow the machine down",
        description:
          "The emulator runs on a separate thread, and every question a debug window asked it had to wait in the same queue as the emulation itself — so watching the machine actually slowed it down, sometimes enough to make the sound crackle. The CPU Debugger was the worst offender, asking around 120 separate questions every time it redrew its disassembly, thirty times a second; it now asks once. The disk lights, the Stack Viewer and the Memory Browser were tidied up the same way, and nothing is asked at all while the machine is switched off.",
      },
      {
        title: "Smoother picture and less stuttering",
        description:
          "Each frame of the screen was being copied into a brand new block of memory and handed across to be drawn, roughly fifty megabytes a second of throwaway work that the browser periodically had to stop and clean up — the cause of the occasional hitch. The picture and the sound now travel through memory shared between the threads instead, which also takes the sound out of the main thread's hands so a busy interface can no longer interrupt it. Drawing also happens at the right moment in the frame now rather than just after it.",
      },
      {
        title: "Windows are solid instead of frosted",
        description:
          "Window and panel backgrounds are now opaque. The frosted glass effect meant the screen behind every open window had to be blurred again for every frame the emulator drew, whether or not anything in the window had changed — expensive with several windows open, and text sat on a busy background. Windows also move and resize more smoothly, as dragging no longer re-measures the page on every mouse movement.",
      },
    ],
  },
  {
    week: "July 28, 2026",
    features: [
      {
        title: "Share a link that opens with a disk already loaded",
        description:
          "Add a disk image URL to the address and the emulator starts with it in the drive: ?disk= for drive 1, ?disk2= for drive 2, ?hd= and ?hd2= for the SmartPort devices. Images hosted alongside the emulator can use a plain path, such as ?disk=/disks/demo.dsk. Disks loaded this way are not saved to your browser storage or your Recent list, and autosave pauses for the session, so a link someone sends you never replaces the disks in your own drives — open the plain address again and everything is as you left it. The file's host has to allow other sites to read it, which GitHub, Google Drive and Dropbox do but most classic archive mirrors do not; where a URL has no filename, ?name= supplies one, which .nib and .2mg images need.",
      },
    ],
    fixes: [],
    improvements: [
      {
        title: "Expansion slot hints follow Apple's slot assignments",
        description:
          "The hints beside each slot now match Apple's documented conventions — slot 1 printer, slot 2 modem, slot 4 mouse, slot 5 3.5\" drives, slot 7 hard disk — instead of listing secondary uses first. Slot 7 no longer suggests a RAM card, which the emulator does not offer.",
      },
      {
        title: "SmartPort now starts in slot 7 and Thunderclock in slot 5",
        description:
          "Matches the conventional layout, where slot 7 is the hard disk and boot slot. Only affects a first visit; a slot layout you have already applied is left alone.",
      },
    ],
  },
  {
    week: "July 27, 2026",
    features: [],
    fixes: [
      {
        title: "Apple buttons could stick down",
        description:
          "Releasing right Alt while Ctrl was held cleared the wrong button, and holding a key while switching to another window never released it at all — either way the button stayed pressed until it was tapped again. Key handling now derives the buttons from which keys are actually held rather than toggling them, so a missed or misreported release cannot latch one on. Note that macOS browsers report nothing when one of two held Option keys is released, so that button stays down until the second key is released; this cannot be detected by a web page.",
      },
      {
        title: "Left and right Alt now select different Apple keys",
        description:
          "Left Alt is Open Apple and right Alt is Closed Apple, matching AppleWin and Apple2TS. Both sides report the same key code, so the two are told apart by which side of the keyboard the key is on. The Windows and Context Menu keys are no longer mapped, as the operating system intercepts them before the browser sees them. Contributed by @anomixer.",
      },
      {
        title: "Closed Apple could stick down",
        description:
          "Releasing right Alt while Ctrl was held released Open Apple instead, leaving Closed Apple pressed until the key was tapped again.",
      },
      {
        title: "The emulator ran fast after continuing from a breakpoint",
        description:
          "Typing text into the emulator temporarily runs it at 8x. A breakpoint hit while characters were still queued left that speed-up in place, so the emulator sprinted as soon as it was resumed.",
      },
      {
        title: "BASIC listings indented everything after a one-line FOR/NEXT",
        description:
          "A line that both opened and closed a loop, such as FOR I = 1 TO 5 : NEXT, left every following line indented for the rest of the listing.",
      },
      {
        title: "BASIC listings stopped at 4096 lines",
        description:
          "Longer programs were silently truncated part way through. Listings are now limited only by the overall output size.",
      },
      {
        title: "Statement highlighting disagreed with statement breakpoints",
        description:
          "On lines containing DATA, the highlighted statement and the statement a breakpoint stopped on were counted by different rules and could differ. Both now use the same one.",
      },
      {
        title: "Programs loaded into memory could start with tracing switched on",
        description:
          "Under ProDOS, a stale trace flag survived and printed a line number before every statement. Multiple spaces inside string literals were also collapsed when loading.",
      },
      {
        title: "Menus let screen text bleed through",
        description:
          "Menu backgrounds are now opaque.",
      },
      {
        title: "Redeploys were not always picked up",
        description:
          "The service worker now revalidates the page on each load, so a new version appears without clearing the browser cache.",
      },
      {
        title: "Open and Save in the editors on Safari and Firefox",
        description:
          "Both now fall back to a download and file picker where the File System Access API is unavailable.",
      },
    ],
    improvements: [
      {
        title: "Joystick window shows live button state",
        description:
          "The PB0 and PB1 indicators now light for Open and Closed Apple however they are pressed — keyboard, on-screen button, or gamepad. Previously they only responded to clicking the on-screen buttons, so keyboard presses showed nothing.",
      },
      {
        title: "Faster BASIC variable inspector",
        description:
          "Variables and arrays are now read in bulk rather than a byte at a time. A thousand-element array previously took thousands of separate reads to display.",
      },
      {
        title: "Formatted BASIC listings",
        description:
          "Listings shown in the BASIC window and returned to AI agents now carry FOR/NEXT indentation and spacing around operators.",
      },
      {
        title: "More documentation",
        description:
          "Added Workspace, Expansion Slots, and Joystick/Paddles sections to the in-app help.",
      },
      {
        title: "Consolidated Applesoft handling",
        description:
          "Tokenizing, detokenizing, variable decoding, and statement parsing now have a single implementation shared by the emulator and its tools, with automated tests covering each. Several of the fixes above came from removing duplicates that had drifted apart.",
      },
    ],
  },
  {
    week: "July 12, 2026",
    features: [],
    fixes: [
      {
        title: "WOZ copy-protected disks that hung mid-load",
        description:
          "Rewrote the Disk II stepper motor to the canonical four-magnet model. The head position now stays in sync through recalibration and non-overlapping seek routines, so disks that step the head without overlapping phases (e.g. subLOGIC's Flight Simulator II) no longer stall while loading.",
      },
      {
        title: "Reading unformatted tracks no longer freezes the drive",
        description:
          "When the head is parked over an empty or unformatted track — as some protection checks do by seeking to the inner head stop — the drive now returns random weak-bit noise like real hardware instead of a frozen data register. Fixes disks such as Elite that hung after seeking to track 39.",
      },
    ],
    improvements: [],
  },
  {
    week: "June 21, 2026",
    features: [
      {
        title: "Virtual dot-matrix printer",
        description:
          "Added an emulated printer peripheral with four period-correct models: ImageWriter II (colour ribbon), ImageWriter I, Epson FX-80 (Roman/Italic fonts, international character sets), and Apple DMP. Output renders to an on-screen sheet of fanfold paper with carriage sweep, impact sounds, and PNG/PDF export.",
      },
      {
        title: "Parallel Card (Centronics)",
        description:
          "Added an Apple Parallel Interface Card using the authentic 341-0005 firmware. Install in slot 1 or 2 to enable the Epson FX-80 and Apple DMP. The Super Serial Card (SSC) in slot 1 or 2 enables the ImageWriter I and ImageWriter II.",
      },
    ],
    fixes: [],
    improvements: [],
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
