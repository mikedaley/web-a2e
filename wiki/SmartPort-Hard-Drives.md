# SmartPort Hard Drives

The SmartPort card gives the emulator hard drive volumes -- a ProDOS disk large enough not to think about, instead of swapping 140KB floppies. Open **View > SmartPort Drives** to manage them.

---

## Table of Contents

- [Requirements](#requirements)
- [Image Formats](#image-formats)
- [Inserting an Image](#inserting-an-image)
- [Booting from a Hard Drive](#booting-from-a-hard-drive)
- [Persistence](#persistence)
- [How It Works](#how-it-works)

---

## Requirements

A **SmartPort** card must be installed. By default it is in slot 7. If the window reports that no card is present, open **View > Expansion Slots** and drag SmartPort into slot 2, 4, 5 or 7.

**On an Apple IIgs there is nothing to fit.** Its SmartPort is in slot 5 as part of the machine — no card to insert, no Control Panel setting — so the window is simply there. It answers where the machine's own firmware does: the real slot 5 firmware has its ProDOS entry at `$C50A` and its SmartPort entry at `$C50D`, and software written for a IIgs hard-codes those rather than reading the ROM's own entry byte. It reports the firmware's four removable, interrupting volumes whatever is fitted, because ProDOS 8 1.x needs the second drive that byte implies: its device-table builder only balances with two drives in the boot slot.

**On an Apple //c there is no SmartPort and no menu item**, because it has nowhere to put one.

The card provides **two** block devices, shown in the window as two drive bays.

## Image Formats

| Extension | Notes |
|-----------|-------|
| `.hdv` | Raw ProDOS-ordered blocks. The most common hard drive image format. |
| `.po` | ProDOS-ordered; the same layout, usually used for floppy-sized images. |
| `.2mg` | A 64-byte header followed by block data. The header is parsed and honoured. |

Images can be far larger than a floppy -- ProDOS supports volumes up to 32MB, and the usual convention is one 32MB volume per device.

## Inserting an Image

Three ways:

- **Drag and drop** an image file onto a drive bay.
- **Click a bay** to open a file picker.
- **Use a link** -- `?hd=` and `?hd2=` insert images from a URL when the page loads. See [[URL-Parameters]].

Recently used images are remembered and can be re-inserted from the recents list without browsing for them again.

## Booting from a Hard Drive

With a bootable ProDOS volume in the first device, the machine boots from the SmartPort card if it takes boot priority. From the BASIC prompt you can also boot a specific slot:

```
PR#7
```

(Substituting whichever slot holds the card.) If a floppy is in drive 1 of the Disk II controller, that will normally win at power-on, so eject it first if you want the hard drive to boot.

## Persistence

Hard drive images and any writes made to them are stored in the browser's IndexedDB and restored on reload, so a volume you installed software onto is still there next session.

Images loaded from a URL are deliberately **transient**: they are not saved to storage or added to recents, and autosave is suspended for that session so a shared link cannot quietly overwrite the persisted image. See [[URL-Parameters]].

## How It Works

The card implements the SmartPort protocol -- `STATUS`, `READBLOCK`, `WRITEBLOCK`, `FORMAT`, `CONTROL` and `INIT` -- together with the older ProDOS block-device entry point, so software using either convention works.

Unlike the other cards, the SmartPort card **builds its own ROM at runtime** rather than loading a dump: the firmware is small, and generating it avoids shipping a ROM image whose provenance would be unclear.

Blocks are 512 bytes. The card reports its device count and each volume's block count through `STATUS`, which is how ProDOS discovers the volumes at boot.

---

## See Also

- [[Expansion-Slots]] -- installing the card
- [[Disk-Drives]] -- 5.25" floppy emulation
- [[File-Explorer]] -- browsing a volume's contents
- [[URL-Parameters]] -- opening the emulator with an image already inserted
