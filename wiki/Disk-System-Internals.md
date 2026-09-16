# Disk System Internals

This page covers the low-level implementation of the 5.25" drive controller and disk image format support. For user-facing disk drive operations, see [[Disk Drives]].

The machines drive a 5.25" disk with different parts: a //e or a II Plus has a **Disk II controller card** in slot 6, while a //c and an Apple IIgs each have an **IWM** soldered to the board. Everything below the chip is the same in all of them and lives in `DiskController` — the drives, the stepper, the motor and the sequencer — so read "the controller" here as any of them.

One difference matters on a IIgs and nowhere else. **ENABLE is not the motor**: a drive keeps turning for about a second after the CPU switches it off, which is right for reading, but the IWM's mode register is writable exactly while the *line* is low and the sequencer must not write flux when it is. The IIgs firmware exercises both in one instruction — it switches the drive off and writes the mode register at `$C0EF`, which is also Q7 — so a machine that asked about the mechanism instead of the wire would spin for a second and erase track zero while doing it. `DiskController::isDriveEnabled()` is the wire; `isMotorOn()` is the mechanism.

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Disk II Controller Card](#disk-ii-controller-card)
  - [The IWM](#the-iwm)
  - [Slot Assignment and Memory Map](#slot-assignment-and-memory-map)
  - [Soft Switches](#soft-switches)
  - [Logic State Sequencer](#logic-state-sequencer)
  - [P6 ROM](#p6-rom)
  - [Motor Control](#motor-control)
  - [Stepper Motor and Head Positioning](#stepper-motor-and-head-positioning)
- [Disk Image Abstraction](#disk-image-abstraction)
- [DSK Format](#dsk-format)
  - [File Layout](#file-layout)
  - [Format Detection](#format-detection)
  - [Sector Ordering](#sector-ordering)
  - [Nibblization](#nibblization)
  - [Denibblization](#denibblization)
  - [Bit Stream Cache](#bit-stream-cache)
  - [Disk Rotation Timing](#disk-rotation-timing)
- [WOZ Format](#woz-format)
  - [File Structure](#file-structure)
  - [INFO Chunk](#info-chunk)
  - [TMAP Chunk](#tmap-chunk)
  - [TRKS Chunk](#trks-chunk)
  - [Quarter-Track Support](#quarter-track-support)
  - [Bit-Level Access](#bit-level-access)
  - [Sector Decoding](#sector-decoding)
  - [Export and Saving](#export-and-saving)
- [GCR Encoding](#gcr-encoding)
  - [6-and-2 Encoding](#6-and-2-encoding)
  - [4-and-4 Encoding](#4-and-4-encoding)
  - [Sector Structure](#sector-structure)
  - [Self-Sync Bytes](#self-sync-bytes)
- [Write Support](#write-support)
- [State Serialization](#state-serialization)
- [Source Files](#source-files)

---

## Architecture Overview

The disk subsystem has three layers:

| Layer | Class | Responsibility |
|-------|-------|----------------|
| Controller | `Disk2Card` | Soft switches, Logic State Sequencer, motor/drive control |
| Image abstraction | `DiskImage` | Abstract interface for head positioning and data access |
| Format implementations | `DskDiskImage`, `WozDiskImage` | Format-specific loading, nibblization, bit-level storage |

The controller only communicates through the `DiskImage` interface using phase signals (for head movement) and nibble/bit read/write operations. Format-specific details are hidden behind this abstraction.

---

## Disk II Controller Card

### Slot Assignment and Memory Map

The Disk II occupies slot 6 by default:

| Address Range | Function |
|---------------|----------|
| `$C0E0-$C0EF` | 16 soft switches for disk control |
| `$C600-$C6FF` | 256-byte bootstrap ROM (P5A, 341-0027) |

The card does not use expansion ROM (`$C800-$CFFF`).

### Soft Switches

The 16 soft switch addresses control all disk operations. Each switch is triggered by any access (read or write) to its address:

| Offset | Even (Off/Low) | Odd (On/High) |
|--------|----------------|---------------|
| `$00-$01` | Phase 0 off | Phase 0 on |
| `$02-$03` | Phase 1 off | Phase 1 on |
| `$04-$05` | Phase 2 off | Phase 2 on |
| `$06-$07` | Phase 3 off | Phase 3 on |
| `$08-$09` | Motor off | Motor on |
| `$0A-$0B` | Select Drive 1 | Select Drive 2 |
| `$0C-$0D` | Q6 Low (read data) | Q6 High (WP sense / write load) |
| `$0E-$0F` | Q7 Low (read mode) | Q7 High (write mode) |

The Q6/Q7 combination determines the controller mode:

| Q7 | Q6 | Mode | Operation |
|----|----|------|-----------|
| 0 | 0 | Read | Returns data register (shift register output) |
| 0 | 1 | Sense | Returns write-protect status in bit 7 |
| 1 | 0 | Write | Writes data register to disk |
| 1 | 1 | Load | Loads CPU bus data into data register |

### The IWM

A //c has no slot to put the card in. It has the Integrated Woz Machine (344-0041), the same controller in one package, decoding the same sixteen addresses at `$C0E0-$C0EF` with the same meanings — which is why a //c runs recognisably the same disk code as a //e and why the drives and the sequencer here are shared with the card.

Two things differ:

- **There is no `$C600` ROM.** A card's boot ROM sits in its slot's 256 bytes; a //c's disk firmware is part of the 16KB system ROM, which is what `$C600` reads on that machine. `IWM::hasROM()` is false and the MMU never asks it for a byte.
- **A read returns one of four registers**, chosen by the Q7/Q6 pair rather than always being the data latch:

| Q7 | Q6 | Register | Contents |
|----|----|----------|----------|
| 0 | 0 | Data | The sequencer's shift register, as the card's |
| 0 | 1 | Status | SENSE in bit 7, the motor in bit 5, the mode register in bits 4-0 |
| 1 | 0 | Handshake | Write-data ready in bit 7, underrun in bit 6 |
| 1 | 1 | Write | A write loads the data register, or the mode register with the motor off |

The mode register is where firmware asks for the clock and bit-cell timings it wants. Nothing here runs off it — the sequencer is clocked at the one rate a 5.25" drive uses, which is the mode a //c selects — but it is stored and read back, because the firmware writes it and then checks the status register for it.

The handshake always reports ready and no underrun: the emulated sequencer consumes a written byte inside the bit cell it was loaded in. Firmware polls bit 7 before loading the next byte, and a chip that never said yes would spin there.

### Logic State Sequencer

The heart of the controller is the Logic State Sequencer (LSS), implemented by the P6 ROM (341-0028). The LSS is a state machine that converts between the serial bit stream on the disk and bytes accessible by the CPU.

**Timing:** The LSS runs at 2x the CPU clock rate (approximately 2.046 MHz). An 8-phase clock divides each 4-cycle bit cell into 8 ticks. Disk read/write occurs only at phase 4 of this clock.

**State machine operation:**

1. At each tick, a P6 ROM address is computed from:
   - Sequencer state (4 bits, high nibble)
   - Q7 and Q6 mode bits
   - Data register bit 7 (QA feedback)
   - Read pulse from disk (1 = flux transition, inverted for address)

2. The ROM output encodes the next state (high nibble) and an action code (low nibble)

3. Actions on the data register:

| Action Code | Operation | Description |
|-------------|-----------|-------------|
| `$0-$7` | CLR | Clear data register to `$00` |
| `$8`, `$C` | NOP | No operation |
| `$9` | SL0 | Shift left, insert 0 |
| `$A`, `$E` | SR+WP | Shift right, insert write-protect bit |
| `$B`, `$F` | LOAD | Load from CPU bus |
| `$D` | SL1 | Shift left, insert 1 |

4. In write mode (Q7=1), at phase 4 the sequencer outputs bit 3 of the next state to the disk head.

**Catch-up mechanism:** Rather than running the LSS every CPU cycle, the controller records the last cycle it was clocked and "catches up" when a soft switch is accessed. This is capped at approximately one disk revolution (~53,000 bits x 8 ticks) to prevent excessive computation after long idle periods.

### P6 ROM

The P6 ROM (341-0028) is a 256x4-bit PROM stored in the emulator in de-scrambled BAPD (Beneath Apple ProDOS) logical format. The ROM address is computed as:

```
address = (state << 4) | (Q7 << 3) | (Q6 << 2) | (QA << 1) | inverted_pulse
```

The ROM CRC32 (source format) is `b72a2c70`.

### Motor Control

The disk motor has a delayed-off behavior matching real hardware:

- **Motor on:** Immediate; sets `motorOn_` flag and resets the LSS cycle counter
- **Motor off:** Delayed by approximately 1 second (1,023,000 CPU cycles). The `motorOffCycle_` timestamp is recorded when the off switch is hit, and `isMotorOn()` checks elapsed time lazily

This delay allows software to briefly turn the motor "off" during seeks without actually stopping the disk, which was a common technique.

### Stepper Motor and Head Positioning

The Disk II uses a 4-phase stepper motor for head positioning. The head moves in half-track (2 quarter-track) increments:

1. The controller manages phase magnet states as a 4-bit field (bits 0-3 for phases 0-3)
2. Stepping occurs when the **current phase is turned OFF** and an adjacent phase is ON
3. If the next phase (clockwise) is on, the head steps **inward** (higher track numbers)
4. If the previous phase (counter-clockwise) is on, the head steps **outward** (toward track 0)
5. If both or neither adjacent phases are on, no stepping occurs

Position tracking uses quarter-tracks (0-139 for DSK, 0-159 for WOZ). The visible track number is `quarter_track / 4`.

---

## Disk Image Abstraction

The `DiskImage` abstract base class defines the interface between the controller and format-specific implementations:

```
DiskImage (abstract)
  +-- DskDiskImage   (DSK/DO/PO raw sector format)
  +-- WozDiskImage   (WOZ 1.0/2.0 bit-accurate format)
```

Key interface methods:

| Method | Description |
|--------|-------------|
| `load()` | Load disk image from raw data |
| `setPhase()` | Notify of phase magnet state change |
| `advanceBitPosition()` | Advance disk rotation based on CPU cycles |
| `readNibble()` / `writeNibble()` | Nibble-level access |
| `readBit()` / `writeBit()` | Bit-level access (for LSS) |
| `getSectorData()` | Get decoded sector data for file explorer |
| `exportData()` | Export in native format for saving |

The disk image manages head positioning internally based on stepper motor phase changes. The controller only knows about phases (0-3), not tracks.

---

## DSK Format

### File Layout

DSK is a raw sector image format storing exactly 143,360 bytes (140 KB):

| Parameter | Value |
|-----------|-------|
| Tracks | 35 (0-34) |
| Sectors per track | 16 |
| Bytes per sector | 256 |
| Nibbles per track | 6,656 |
| Total size | 143,360 bytes |

### Format Detection

Format detection uses content-based heuristics rather than relying solely on file extensions:

1. **ProDOS check:** Examine block 2 (offset 1024) for a volume directory header. A valid ProDOS disk has storage type `$Fx` with a valid name length and ASCII characters. If found, the format is PO (ProDOS order).

2. **DOS 3.3 check:** Examine the VTOC at track 17, sector 0 (offset 69,632). A valid DOS 3.3 disk has a catalog track of `$11-$14`, catalog sector of `$00-$0F`, and DOS version `$03`.

3. **Extension fallback:** If content detection fails, `.po` files are treated as ProDOS order; all others default to DOS order.

### Sector Ordering

Two sector ordering schemes are supported, each with its own logical-to-physical mapping:

**DOS 3.3 order (DSK/DO):**

| Logical | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---------|---|---|---|---|---|---|---|---|---|---|----|----|----|----|----|-----|
| Physical | 0 | 13 | 11 | 9 | 7 | 5 | 3 | 1 | 14 | 12 | 10 | 8 | 6 | 4 | 2 | 15 |

**ProDOS order (PO):**

| Logical | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---------|---|---|---|---|---|---|---|---|---|---|----|----|----|----|----|-----|
| Physical | 0 | 2 | 4 | 6 | 8 | 10 | 12 | 14 | 1 | 3 | 5 | 7 | 9 | 11 | 13 | 15 |

### Nibblization

When a track is first accessed, the raw sector data is converted to a GCR-encoded nibble stream on demand. Each track contains 16 sectors laid out sequentially:

1. **Gap 1** (first sector) or **Gap 3** (between sectors): Self-sync bytes
   - First sector: 128 sync bytes
   - Subsequent sectors: 40 bytes (track 0) or 38 bytes (other tracks)
2. **Address field:** Prologue + 4-and-4 encoded volume/track/sector/checksum + epilogue
3. **Gap 2:** 5 sync bytes
4. **Data field:** Prologue + 343 6-and-2 encoded nibbles + epilogue
5. **Gap 3 end:** 1 sync byte

The nibble track is padded or truncated to exactly 6,656 nibbles. Each nibble also carries a sync flag indicating whether it is a 10-bit self-sync byte or a standard 8-bit data byte.

### Denibblization

When a modified nibble track needs to be converted back to sector data (for saving), the denibblizer:

1. Creates an extended buffer with wrap-around (500 extra nibbles) to handle sectors that span the track boundary
2. Searches for address field prologues (`$D5 $AA $96`)
3. Decodes the 4-and-4 encoded address (volume, track, sector, checksum)
4. Verifies the address checksum and track number
5. Finds the data field prologue (`$D5 $AA $AD`) within 50 nibbles
6. Decodes the 343 6-and-2 encoded nibbles back to 256 bytes
7. Writes the decoded data to the sector data array using the appropriate sector mapping

### Bit Stream Cache

For LSS-level access, each nibble track can be converted to a packed bit stream. The conversion respects the sync flag:

- **Data bytes:** 8 bits per nibble (MSB first)
- **Sync bytes:** 10 bits per nibble (8 data bits + 2 extra zero bits)

The total bit count varies per track based on the number of sync bytes. Dirty bit tracks are converted back to nibble tracks before denibblization.

### Disk Rotation Timing

The disk spins at approximately 300 RPM (5 revolutions per second):

| Parameter | Value |
|-----------|-------|
| CPU clock | 1,023,000 Hz |
| Cycles per revolution | ~204,600 |
| Nibbles per track | 6,656 |
| Cycles per nibble | ~31 (giving ~297 RPM, within spec) |

The `advanceBitPosition()` method advances the nibble position based on elapsed CPU cycles to simulate continuous disk rotation while the motor is on.

---

## WOZ Format

WOZ is a bit-accurate disk image format that captures exact magnetic flux transitions. It preserves copy-protection schemes and timing variations that sector-based formats cannot represent. **NOTE THAT COPY PROTECTION IS STILL CAUSING SOME ISSUES SO MORE WORK TO BE DONE :) **

### File Structure

A WOZ file consists of a header followed by a series of chunks:

```
WOZ Header (12 bytes)
  +-- INFO chunk (required)
  +-- TMAP chunk (required)
  +-- TRKS chunk (required)
  +-- META chunk (optional, skipped)
  +-- WRIT chunk (optional, skipped)
```

**Header format (12 bytes):**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | Signature | `WOZ1` (`$57 $4F $5A $31`) or `WOZ2` (`$57 $4F $5A $32`) |
| 4 | 1 | High bits | `$FF` (high bit test) |
| 5 | 3 | LF/CR/LF | `$0A $0D $0A` (line ending test) |
| 8 | 4 | CRC32 | CRC of all data after this field |

**Chunk header (8 bytes):**

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | Chunk ID (4 ASCII characters) |
| 4 | 4 | Data size (not including header) |

### INFO Chunk

The INFO chunk (60 bytes in WOZ2) contains disk metadata:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | Version | 1 or 2 |
| 1 | 1 | Disk type | 1 = 5.25", 2 = 3.5" |
| 2 | 1 | Write protected | 0 or 1 |
| 3 | 1 | Synchronized | Tracks are cross-track synchronized |
| 4 | 1 | Cleaned | MC3470 fake bits removed |
| 5 | 32 | Creator | Software that created the image |
| 37 | 1 | Disk sides | 1 or 2 |
| 38 | 1 | Boot sector format | 0=unknown, 1=16-sector, 2=13-sector, 3=both |
| 39 | 1 | Optimal bit timing | In 125 ns units (default 32 = 4 us) |
| 40 | 2 | Compatible hardware | Bit field |
| 42 | 2 | Required RAM | Minimum RAM in KB |
| 44 | 2 | Largest track | Block count of largest track |

### TMAP Chunk

The TMAP (Track Map) chunk is a 160-byte array that maps quarter-track positions (0-159) to track data indices. A value of `$FF` indicates no track data at that position. Multiple quarter-tracks can reference the same track data index, enabling half-track data sharing.

### TRKS Chunk

**WOZ1 format:** Each track entry is a fixed 6,656 bytes:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 6,646 | Bitstream data |
| 6,646 | 2 | Bytes used |
| 6,648 | 2 | Bit count |
| 6,650 | 2 | Splice point |
| 6,652 | 1 | Splice nibble |
| 6,653 | 1 | Splice bit count |
| 6,654 | 2 | Reserved |

**WOZ2 format:** A table of 160 track entries (8 bytes each), followed by track bit data at 512-byte block offsets:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | Starting block (512-byte blocks from file start) |
| 2 | 2 | Block count |
| 4 | 4 | Bit count |

### Quarter-Track Support

WOZ supports 160 quarter-track positions (40 possible track positions x 4 quarter-tracks each). The TMAP provides the indirection layer:

- Standard tracks are at quarter-track positions 0, 4, 8, ..., 136
- Half-tracks (used by some copy protection) can exist at positions 2, 6, 10, ...
- Quarter-tracks provide even finer granularity

The stepper motor physics are identical to the DSK implementation but with a wider range (0-159 vs 0-139).

### Bit-Level Access

WOZ stores raw bit data in packed MSB-first format. Reading a nibble from the bit stream follows the Disk II hardware behavior:

1. Read bits sequentially
2. Skip zero bits until a 1-bit is found (sync region)
3. Shift bits into an accumulator
4. When bit 7 of the accumulator is set, the nibble is complete

A safety limit of 64 bits prevents infinite loops on corrupted data.

**Bit timing for WOZ:**

| Parameter | Value |
|-----------|-------|
| Cycles per bit | 4 (from optimal bit timing of 32 x 125 ns = 4 us) |
| Bits per revolution | ~51,200 (standard track) |

### Sector Decoding

WOZ images can be decoded to sector data for the [[File Explorer]]. This enables browsing DOS 3.3 and ProDOS files on WOZ disks:

1. For each track, read nibbles from the bit stream (scanning through twice to handle wrap-around)
2. Search for address field prologues (`$D5 $AA $96`)
3. Decode 4-and-4 address fields and verify checksums
4. Find data field prologues (`$D5 $AA $AD`)
5. Decode 6-and-2 data fields

Decoding is considered successful if at least 50% of sectors (280 of 560) are recovered. This tolerance allows copy-protected disks with intentionally bad sectors to still have their readable content browsed.

### Export and Saving

WOZ images are exported in WOZ2 format with the following structure:

1. 12-byte WOZ2 header
2. INFO chunk (68 bytes total with header)
3. TMAP chunk (168 bytes total)
4. TRKS chunk with 160-entry track table (1288 bytes)
5. Track bit data starting at block 3 (offset 1536)

Blank/unformatted disks can be created with 35 tracks of 51,200 bits each, filled with sync bytes (`$FF`).

---

## GCR Encoding

Group Coded Recording (GCR) is the encoding scheme used by the Disk II to store data on floppy disks. It ensures that the encoded bit stream maintains the timing constraints required by the hardware: every byte must have its high bit set and must not contain more than two consecutive zero bits.

### 6-and-2 Encoding

The primary data encoding scheme converts 256 data bytes into 343 disk nibbles (342 encoded + 1 checksum):

**Step 1: Build auxiliary buffer (86 bytes)**

For each of 86 positions, extract bits 0-1 from up to three data bytes:
- `buffer[i]` bits 0-1 from `data[i]` (with bit swap)
- `buffer[i]` bits 2-3 from `data[i+86]` (with bit swap)
- `buffer[i]` bits 4-5 from `data[i+172]` (with bit swap)

**Step 2: Build primary buffer (256 bytes)**

Store bits 2-7 of each data byte (shifted right by 2):
```
buffer[86+i] = data[i] >> 2
```

**Step 3: XOR differential encoding**

Each buffer value is XORed with the previous value before encoding:
```
encoded[i] = ENCODE_6_AND_2[buffer[i] XOR prev]
prev = buffer[i]
```

**Step 4: Checksum**

The final (343rd) nibble encodes the last pre-XOR buffer value, serving as a checksum.

The 6-and-2 lookup table maps 64 possible 6-bit values to 64 valid disk nibbles (values `$96-$FF` with high bit set and no adjacent zeros).

### 4-and-4 Encoding

Address field values (volume, track, sector, checksum) use a simpler encoding that splits each byte into odd and even bits:

```
odd  = 0xAA | (value >> 1) & 0x55   // bits 7,5,3,1 -> positions 6,4,2,0
even = 0xAA | value & 0x55          // bits 6,4,2,0 -> positions 6,4,2,0
```

Each byte becomes two bytes, both guaranteed to have their high bit set.

### Sector Structure

A complete sector on disk consists of:

```
[Gap: 14+ sync bytes]
[Address Prologue: D5 AA 96]
[Volume odd] [Volume even]
[Track odd]  [Track even]
[Sector odd] [Sector even]
[Checksum odd] [Checksum even]   (checksum = volume XOR track XOR sector)
[Address Epilogue: DE AA EB]
[Gap: 6 sync bytes]
[Data Prologue: D5 AA AD]
[343 encoded data nibbles]
[Data Epilogue: DE AA EB]
```

**Marker bytes:**

| Marker | Bytes | Purpose |
|--------|-------|---------|
| Address prologue | `$D5 $AA $96` | Start of address field |
| Address epilogue | `$DE $AA $EB` | End of address field |
| Data prologue | `$D5 $AA $AD` | Start of data field |
| Data epilogue | `$DE $AA $EB` | End of data field |
| Sync byte | `$FF` | Self-synchronizing (10 bits on disk) |

### Self-Sync Bytes

Self-sync bytes are `$FF` values that occupy 10 bits on disk instead of the standard 8 bits. The extra two zero bits allow the read hardware to regain byte synchronization. When the controller encounters a stream of 10-bit `$FF` values, the shift register naturally aligns to byte boundaries because the `$FF` pattern fills all 8 bits with ones regardless of the starting alignment.

---

## Write Support

Both DSK and WOZ formats support write operations:

**DSK writes:**
1. Nibbles are written directly to the nibble track cache at the current position
2. The track is marked dirty
3. On save, dirty tracks are denibblized back to sector data
4. For bit-level writes (LSS), bits are written to the bit track cache and later converted back through bit-track -> nibble-track -> sector-data

**WOZ writes:**
1. Bits are written directly to the track's packed bit array
2. For nibble writes, 8 bits are written MSB-first
3. The modified flag is set for the entire image

Write-protected disks reject all write operations. The write-protect status comes from the WOZ INFO chunk or the `write_protected_` flag for DSK images.

---

## State Serialization

The Disk II controller state (32 bytes maximum) includes:

| Field | Size | Description |
|-------|------|-------------|
| Motor on | 1 byte | Motor running flag |
| Selected drive | 1 byte | 0 or 1 |
| Q6 | 1 byte | Q6 latch state |
| Q7 | 1 byte | Q7 latch state |
| Phase states | 1 byte | 4-bit phase magnet field |
| Data register | 1 byte | LSS shift register value |
| Drive 0 quarter-track | 1 byte | Head position for drive 0 |
| Drive 1 quarter-track | 1 byte | Head position for drive 1 |
| Sequencer state | 1 byte | 4-bit LSS state |
| Bus data | 1 byte | Last CPU bus value |
| LSS clock | 1 byte | 8-phase clock position |

Disk image data (sector data and modifications) is saved separately as part of the full emulator state. See [[Save States]] for details.

---

## Source Files

| File | Description |
|------|-------------|
| `src/core/cards/disk_controller.hpp` | The drives, stepper, motor and sequencer both machines share |
| `src/core/cards/disk_controller.cpp` | Implementation, LSS, P6 ROM |
| `src/core/cards/disk2/disk2_card.hpp` | Disk II card: the shared controller plus its P5A boot ROM |
| `src/core/cards/disk2/disk2_card.cpp` | P5A ROM loading and reads |
| `src/core/cards/iwm/iwm.hpp` | IWM: the shared controller plus the //c's register file |
| `src/core/cards/iwm/iwm.cpp` | Status, handshake and mode register |
| `src/core/disk-image/disk_image.hpp` | Abstract disk image base class |
| `src/core/disk-image/dsk_disk_image.hpp` | DSK format class declaration |
| `src/core/disk-image/dsk_disk_image.cpp` | DSK nibblization, denibblization, stepper |
| `src/core/disk-image/woz_disk_image.hpp` | WOZ format class declaration |
| `src/core/disk-image/woz_disk_image.cpp` | WOZ parsing, bit-level access, sector decoding |
| `src/core/disk-image/gcr_encoding.hpp` | GCR encoding tables and function declarations |
| `src/core/disk-image/gcr_encoding.cpp` | 6-and-2 and 4-and-4 encoding implementation |

---

See also: [[Disk Drives]] | [[File Explorer]] | [[Expansion Slots]] | [[Architecture Overview]]
