# Disk Drives

The Disk Drives window provides a visual interface for the emulated Disk II controller. Open it from **View > Disk Drives** in the toolbar.

The Apple //e supported two floppy drives connected to the Disk II controller card in Slot 6. Each drive reads and writes 5.25-inch floppy disks with 35 tracks and 16 sectors per track.

**The controller differs by machine, the drives do not.** A //e and a II Plus fit a Disk II card in a slot. An Apple //c and an Apple IIgs have an **Integrated Woz Machine** built in instead — the same sixteen addresses at `$C0E0-$C0EF` meaning the same things, with the drives, the stepper, the motor and Woz's Logic State Sequencer shared with the card. What the IWM adds is the register file a read sees in front of it: data, status, handshake, and a mode register writable only with the drive line low. What it has no need of is a ROM, because the disk firmware on both machines is in the system ROM rather than in slot 6's 256 bytes. Everything in this window works the same way whichever is fitted.

---

## Table of Contents

- [Supported Disk Formats](#supported-disk-formats)
- [Loading a Disk](#loading-a-disk)
- [Ejecting a Disk](#ejecting-a-disk)
- [Blank Disks](#blank-disks)
- [Recent Disks](#recent-disks)
- [Disk Persistence](#disk-persistence)
- [Drive Status Display](#drive-status-display)
- [Surface Visualisation](#surface-visualisation)
- [Detail Panel](#detail-panel)
- [Drive Sounds](#drive-sounds)
- [Drag and Drop](#drag-and-drop)
- [Write Protection and Saving](#write-protection-and-saving)

---

## Hard Drives

This page covers the 5.25" floppy drives on the Disk II controller. For hard drive volumes -- larger ProDOS disks on `.hdv`, `.po` or `.2mg` images -- see [[SmartPort-Hard-Drives]].

## Supported Disk Formats

The emulator accepts the following 5.25-inch disk image formats:

| Format | Extension | Description |
|--------|-----------|-------------|
| **DSK** | `.dsk` | Raw sector-order disk image (140KB). The most common format. Sectors are stored in DOS 3.3 logical order. |
| **DO** | `.do` | Identical to DSK format. The extension explicitly indicates DOS 3.3 sector ordering. |
| **PO** | `.po` | ProDOS-order disk image (140KB). Sectors are stored in ProDOS physical order. |
| **WOZ** | `.woz` | Bit-level disk image with timing and metadata. The most accurate format, capable of representing copy-protected disks. Stores individual flux transitions. |

The file input accepts all five extensions: `.dsk`, `.do`, `.po` and `.woz`

## Loading a Disk

There are three ways to insert a disk:

1. **Insert button** -- Click the **Insert** button on a drive to open a file browser. Select a disk image file to load it into that drive.

2. **Drag and drop** -- Drag a disk image file from your desktop onto the emulator screen. The file will be loaded into the first empty drive. If both drives are occupied, it replaces the disk in Drive 1.

3. **Recent Disks** -- Click the **Recent** button to see a dropdown of previously loaded disks. Click an entry to reload it instantly (see [Recent Disks](#recent-disks)).

When a disk is loaded, the drive displays the filename (with scrolling animation if the name is too long) and enables the Eject button.

## Ejecting a Disk

Click the **Eject** button on a drive to remove the disk. If the disk has been modified since it was loaded, a save dialog appears offering to save the changes before ejecting:

- **Save** -- Opens the browser's file-save dialog with the current filename as the default. After saving (or cancelling the file picker), the disk is ejected.
- **Don't Save** -- Ejects the disk immediately, discarding any unsaved modifications.
- Press **Escape** to cancel the eject entirely and keep the disk inserted.

After ejecting, the drive display resets to "No Disk" and the surface visualisation clears.

## Blank Disks

Click the **Blank** button to insert a fresh, empty 140KB DOS 3.3 disk image. This is useful for saving data from within the emulated Apple //e -- format the blank disk with DOS 3.3 `INIT` or ProDOS, then save files to it.

## Recent Disks

Each drive maintains its own list of recently used disk images. Click **Recent** on a drive to see a dropdown of previous disks:

- Click a disk name to reload it into that drive.
- Click **Clear Recent** at the bottom of the list to remove all entries for that drive.
- Recent disks are stored in IndexedDB along with their full disk image data, so they survive browser restarts.

## Disk Persistence

Inserted disks are automatically saved to IndexedDB so they persist across browser sessions:

- When you load a disk, its data is stored in IndexedDB keyed by drive number.
- On the next page load, both drives are automatically restored to their previous state.
- Ejecting a disk clears its persisted data for that drive.

This is separate from the [[Save-States]] system, which captures the entire emulator state including disk contents and modifications.

## Drive Status Display

Each drive shows:

| Element | Description |
|---------|-------------|
| **Disk name** | The filename of the currently inserted disk. Long names scroll horizontally. |
| **Track indicator** | Shows the current head position as `T00` through `T34`. Highlighted in colour when the drive motor is on and this drive is selected. Displays `T--` when no disk is present. |

## Surface Visualisation

Each drive includes a real-time canvas rendering of the disk surface. This animated view shows:

- **Rotating disk** -- The floppy disk spins at 300 RPM when the motor is active. When the motor turns off, the disk decelerates realistically before stopping.
- **35 tracks** -- Concentric rings from the outer edge to just outside the centre hub. Each track corresponds to one of the 35 tracks on a 5.25-inch floppy.
- **16 sectors** -- Radial lines divide the disk into 16 sectors, matching the Apple II's sector layout.
- **Head position** -- A small indicator shows which quarter-track the drive head is currently positioned over.
- **Track access heat map** -- Recently accessed tracks glow with a colour intensity proportional to access frequency. The heat decays over time, so you can see which areas of the disk are being read or written in real time.
- **Write mode indicator** -- The head indicator changes appearance when the drive is in write mode.
- **Hub hole and reinforcement ring** -- The centre of the disk shows the large hub hole and the white reinforcement ring, matching the physical appearance of a 5.25-inch floppy.

The surface visualisation can be hidden by clicking the **eye icon** in the window title bar. When hidden, the window switches to a compact mode showing only the controls and status indicators.

## Detail Panel

Click the **info icon** in the title bar to expand a technical detail panel below each drive. This shows low-level controller state updated in real time:

| Field | Description |
|-------|-------------|
| **QTrack** | Quarter-track position (0-139). The Disk II controller positions the head in quarter-track increments. |
| **Phase** | Current stepper motor phase. |
| **Nibble** | Current nibble position within the track's data stream. |
| **Motor** | Motor state: ON or OFF. |
| **Mode** | Read or Write mode. |
| **Byte** | The last byte read from or written to the disk (hex). Only shown for the currently selected drive. |

## Drive Sounds

The emulator synthesises realistic Disk II drive sounds using the Web Audio API. All drive sounds are controlled by the master volume slider and can be toggled on or off from the **Sound** popup in the toolbar.

### Sound Layers

**Seek / Step sound** -- A short mechanical click played each time the drive head moves to a new track. Synthesised from a combination of:
- A sharp initial noise transient (the physical click)
- A high-frequency metallic tick at ~2200 Hz
- A higher harmonic at ~3800 Hz for metallic character
- A lower body resonance at ~1200 Hz

All components decay rapidly within 25ms, passed through a 6 kHz low-pass filter.

**Motor sound** -- A continuous layered sound while the drive motor is spinning, composed of three layers:
- **Motor hum** -- A 55 Hz sawtooth oscillator through a 129 Hz low-pass filter, producing the low-frequency rumble of the spindle motor.
- **Mechanical whir** -- Band-passed noise centred at ~499 Hz, simulating the general mechanical noise of the drive mechanism.
- **Disk swish** -- Band-passed noise at ~1917 Hz modulated by a ~2.7 Hz LFO, reproducing the rhythmic sound of the floppy disk rubbing against its jacket at 300 RPM (~5 rotations per second).

When the motor stops, all layers fade out over 150ms to avoid audio clicks.

### Sound Controls

- **Volume slider** -- The main volume slider in the toolbar header scales all drive sounds proportionally.
- **Mute toggle** -- The mute toggle silences all emulator audio including drive sounds.
- **Drive Sounds toggle** -- A dedicated toggle in the Sound popup enables or disables drive sounds independently of the main speaker and Mockingboard audio.

## Drag and Drop

You can drag disk image files directly from your file manager onto the emulator screen:

1. The monitor frame highlights to indicate it is ready to receive a drop.
2. Drop the file to insert it into the first empty drive (Drive 1 checked first, then Drive 2).
3. If both drives already have disks, the dropped file replaces Drive 1.

## Write Protection and Saving

When ejecting a modified disk, the emulator prompts to save changes. The save dialog uses the browser's File System Access API (where available) to let you choose a save location and filename. The default filename is the original disk image name.

Disk modifications are also preserved in save states -- see [[Save-States]] for details.
