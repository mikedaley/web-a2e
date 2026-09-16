# Audio System

The emulator's audio subsystem handles two distinct sound sources: the Apple IIe's built-in 1-bit speaker and the optional Mockingboard expansion card with its dual AY-3-8910 programmable sound generators. Audio also serves as the primary timing mechanism for the entire emulator.

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Audio-Driven Timing](#audio-driven-timing)
- [Speaker Emulation](#speaker-emulation)
  - [Toggle Recording](#toggle-recording)
  - [Sample Generation](#sample-generation)
  - [Low-Pass Filter](#low-pass-filter)
  - [DC Offset Removal](#dc-offset-removal)
- [Mockingboard Sound Card](#mockingboard-sound-card)
  - [Hardware Layout](#hardware-layout)
  - [VIA 6522 Emulation](#via-6522-emulation)
  - [AY-3-8910 Sound Generator](#ay-3-8910-sound-generator)
  - [Tone Generation](#tone-generation)
  - [Noise Generation](#noise-generation)
  - [Envelope Generator](#envelope-generator)
  - [Mixer and Channel Output](#mixer-and-channel-output)
  - [Phase Coherence](#phase-coherence)
  - [Incremental Audio Generation](#incremental-audio-generation)
- [Audio Mixing Pipeline](#audio-mixing-pipeline)
- [JavaScript Audio Layer](#javascript-audio-layer)
  - [AudioDriver](#audiodriver)
  - [AudioWorklet](#audioworklet)
  - [Fallback Timing](#fallback-timing)
- [Volume and Mute Control](#volume-and-mute-control)
- [State Serialization](#state-serialization)
- [Source Files](#source-files)

---

## Architecture Overview

The audio system spans two layers:

| Layer | Component | Responsibility |
|-------|-----------|----------------|
| C++ (WASM) | `Audio` | Speaker toggle recording, sample generation, mixing |
| C++ (WASM) | `MockingboardCard` | VIA 6522 timers, AY-3-8910 PSG emulation |
| JavaScript | `AudioDriver` | Web Audio API context, worklet management, volume |
| JavaScript | `AudioWorklet` | Real-time sample buffering on the audio thread |

Audio flows from C++ through WASM memory to the JavaScript AudioWorklet, which feeds the Web Audio API output.

---

## Audio-Driven Timing

The emulator uses the Web Audio API as its master clock rather than `requestAnimationFrame` or `setInterval`. This provides several advantages:

1. The AudioWorklet runs on a dedicated audio thread at a precise 48 kHz sample rate
2. Timing remains accurate even when the browser tab is backgrounded
3. Frame synchronization derives naturally from the audio sample count

The timing chain works as follows:

1. The AudioWorklet requests 1600 sample frames when its buffer runs low
2. The `AudioDriver` calls `_generateStereoAudioSamples()` in WASM
3. WASM runs the CPU for approximately 21.3 cycles per sample (1,023,000 Hz / 48,000 Hz)
4. After approximately 800 samples (~17,030 cycles), a 60 Hz video frame boundary is reached
5. The `onFrameReady` callback triggers the display to render the completed frame

---

## Speaker Emulation

The Apple IIe speaker is a simple 1-bit output device toggled by any access (read or write) to soft switch `$C030`. There is no volume control or waveform selection in hardware -- all sounds are produced by toggling the speaker at specific frequencies from software.

### Toggle Recording

Each access to `$C030` records the CPU cycle count when the toggle occurred:

```
toggleSpeaker(cycleCount) -> toggleCycles_[] vector (up to 8192 entries)
```

The `MAX_TOGGLES` constant (8192) provides sufficient headroom for high-frequency audio within a single audio buffer period.

### Sample Generation

The `generateStereoSamples()` method converts toggle events into audio samples using a bandwidth-limited reconstruction approach:

1. For each output sample, determine the time window in CPU cycles
2. Walk the toggle event list to find all toggles within that window
3. Compute the ratio of "high time" vs "low time" within the sample period
4. Output `(highTime - lowTime) / totalTime` as the raw sample value (-1.0 to +1.0)

This fractional approach naturally handles the case where multiple toggles occur within a single audio sample period, producing correct anti-aliased output.

### Low-Pass Filter

A single-pole IIR low-pass filter smooths the speaker output:

| Parameter | Value | Effect |
|-----------|-------|--------|
| `FILTER_ALPHA` | 0.15 | Cutoff ~7.8 kHz at 48 kHz sample rate |

The filter equation is:

```
filterState = filterState + FILTER_ALPHA * (rawValue - filterState)
```

The 7.8 kHz cutoff preserves speaker harmonics while removing aliasing artifacts from the 1-bit reconstruction.

### DC Offset Removal

A high-pass filter removes DC bias that accumulates when the speaker is held in one state:

| Parameter | Value | Effect |
|-----------|-------|--------|
| `DC_ALPHA` | 0.995 | Fast tracking of speaker state changes |

```
dcOffset = DC_ALPHA * dcOffset + (1 - DC_ALPHA) * filterState
output = filterState - dcOffset
```

The relatively fast time constant (compared to the Mockingboard's 0.9999) allows the DC removal to track the speaker's binary state transitions without introducing audible artifacts.

---

## Mockingboard Sound Card

The Mockingboard is a popular third-party sound card that adds multi-channel music and sound effects to the Apple IIe. The emulation models the complete hardware architecture: two VIA 6522 interface adapters, each controlling an AY-3-8910 programmable sound generator.

### Hardware Layout

The Mockingboard occupies slot 4 by default. Unlike most expansion cards, it uses the slot ROM address space for its registers rather than the I/O space:

| Address Range | Component | Function |
|---------------|-----------|----------|
| `$C400-$C47F` | VIA 1 | Left channel PSG control (bit 7 = 0) |
| `$C480-$C4FF` | VIA 2 | Right channel PSG control (bit 7 = 1) |
| `$C0C0-$C0CF` | (unused) | Standard I/O space not used |

Register selection uses bits 0-3 of the address. Bit 7 selects which VIA is addressed.

### VIA 6522 Emulation

Each VIA provides timer-driven interrupt generation and an 8-bit parallel port interface to its connected PSG. The VIA register map:

| Register | Offset | Name | Description |
|----------|--------|------|-------------|
| ORB | `$00` | Output Register B | PSG control lines (BC1, BDIR, RESET) |
| ORA | `$01` | Output Register A | PSG data bus |
| DDRB | `$02` | Data Direction B | Port B direction (1 = output) |
| DDRA | `$03` | Data Direction A | Port A direction (1 = output) |
| T1CL | `$04` | Timer 1 Counter Low | Read: counter low byte |
| T1CH | `$05` | Timer 1 Counter High | Write: loads counter and starts timer |
| T1LL | `$06` | Timer 1 Latch Low | Low byte of reload value |
| T1LH | `$07` | Timer 1 Latch High | High byte; clears T1 interrupt |
| T2CL | `$08` | Timer 2 Counter Low | Read: counter low; Write: latch low |
| T2CH | `$09` | Timer 2 Counter High | Write: loads counter and starts timer |
| SR | `$0A` | Shift Register | Serial data shift register |
| ACR | `$0B` | Auxiliary Control | Timer modes (bit 6: T1 free-running) |
| PCR | `$0C` | Peripheral Control | Handshake control |
| IFR | `$0D` | Interrupt Flag | Active interrupt sources |
| IER | `$0E` | Interrupt Enable | Interrupt mask (bit 7: set/clear mode) |
| ORA_NH | `$0F` | ORA (no handshake) | Same as ORA without clearing flags |

**Timer Behavior:**
- Timer 1 supports one-shot and free-running modes (ACR bit 6)
- Timer period = latch value + 2 cycles (per Rockwell datasheet)
- Counter is loaded with latch + 1 on write to T1CH (the +2nd cycle is the write itself)
- In free-running mode, the counter automatically reloads from the latch on underflow
- Timer 2 is one-shot only; it wraps after underflow but does not reload or re-fire

**IRQ Handling:**
- IRQ flag bits: T1 (`$40`), T2 (`$20`), CB1 (`$10`), CB2 (`$08`), SR (`$04`), CA1 (`$02`), CA2 (`$01`)
- IRQ callback only fires on 0-to-1 transition (edge detection prevents duplicate assertions)
- IER bit 7 controls set/clear mode: writing with bit 7 = 1 sets specified bits, bit 7 = 0 clears them

**PSG Control Protocol:**

The VIA communicates with the AY-3-8910 through Port B control lines:

| Port B Bit | Signal | Function |
|------------|--------|----------|
| 0 | BC1 | Bus Control 1 |
| 1 | BDIR | Bus Direction |
| 2 | ~RESET | PSG reset (active low) |

The PSG function is determined by the BC1/BDIR combination:

| BC1 | BDIR | Function | Operation |
|-----|------|----------|-----------|
| 0 | 0 | INACTIVE | No operation |
| 1 | 0 | READ | PSG drives data onto Port A |
| 0 | 1 | WRITE | Port A data written to selected PSG register |
| 1 | 1 | LATCH | Port A value selects PSG register address |

Operations only execute when transitioning from the INACTIVE state (AppleWin-compatible behavior). The address must be latched before reads or writes; writes to an unlatched address are rejected.

### AY-3-8910 Sound Generator

Each PSG provides three independent tone channels, a noise generator, and an envelope generator. The PSG clock is derived from the Apple IIe CPU clock at approximately 1.023 MHz.

**Register Map:**

| Register | Address | Bits | Description |
|----------|---------|------|-------------|
| Tone A Fine | R0 | 7-0 | Channel A period low byte |
| Tone A Coarse | R1 | 3-0 | Channel A period high nibble |
| Tone B Fine | R2 | 7-0 | Channel B period low byte |
| Tone B Coarse | R3 | 3-0 | Channel B period high nibble |
| Tone C Fine | R4 | 7-0 | Channel C period low byte |
| Tone C Coarse | R5 | 3-0 | Channel C period high nibble |
| Noise Period | R6 | 4-0 | Noise generator period (5-bit) |
| Mixer | R7 | 5-0 | Tone/noise enable per channel |
| Amplitude A | R8 | 4-0 | Channel A volume or envelope mode |
| Amplitude B | R9 | 4-0 | Channel B volume or envelope mode |
| Amplitude C | R10 | 4-0 | Channel C volume or envelope mode |
| Envelope Fine | R11 | 7-0 | Envelope period low byte |
| Envelope Coarse | R12 | 7-0 | Envelope period high byte |
| Envelope Shape | R13 | 3-0 | Envelope shape control |
| I/O Port A | R14 | 7-0 | General-purpose I/O |
| I/O Port B | R15 | 7-0 | General-purpose I/O |

### Tone Generation

Each tone channel uses a 12-bit period counter (fine + coarse registers). The counter increments at the master clock / 8 rate. When the counter reaches the period value, the tone output toggles:

```
Tone frequency = PSG_CLOCK / (8 * period)
```

Where `PSG_CLOCK` = 1,023,000 Hz. A period of 0 is treated as 1 (highest frequency).

### Noise Generation

The noise generator uses a 17-bit linear feedback shift register (LFSR) following the MAME/hardware implementation:

```
feedback = bit0 XOR bit3
noiseShiftReg = (noiseShiftReg >> 1) | (feedback << 16)
```

The noise generator clocks at half the tone generator rate (master / 16 vs master / 8), so the period comparison is doubled internally. Noise output is taken directly from bit 0 of the shift register.

### Envelope Generator

The envelope generator produces a 4-bit volume ramp (0-15) controlled by the shape register (R13):

| Bit | Name | Function |
|-----|------|----------|
| 3 | CONT | Continue after first cycle (0 = hold at 0) |
| 2 | ATT | Attack direction (1 = rising, 0 = falling) |
| 1 | ALT | Alternate direction each cycle |
| 0 | HOLD | Hold final value after first cycle |

Common envelope shapes:

| Shape | CONT-ATT-ALT-HOLD | Waveform |
|-------|-------------------|----------|
| `$00` | 0-0-0-0 | Decay then silence |
| `$04` | 0-1-0-0 | Attack then silence |
| `$08` | 1-0-0-0 | Repeating sawtooth (falling) |
| `$0A` | 1-0-1-0 | Repeating triangle |
| `$0C` | 1-1-0-0 | Repeating sawtooth (rising) |
| `$0E` | 1-1-1-0 | Repeating triangle (inverted) |

Writing to R13 resets the envelope counter and sets the initial volume based on the attack direction.

The envelope period ticks at the same rate as tone counters (master / 8). Each step of the 16-level ramp takes `period` ticks, giving a full envelope cycle frequency of:

```
fEnvelope = PSG_CLOCK / (256 * period)
```

### Mixer and Channel Output

The mixer register (R7) controls which generators feed each channel. A bit value of 1 means **disabled** (bypassed/always high):

| Bit | Function |
|-----|----------|
| 0 | Tone A disable |
| 1 | Tone B disable |
| 2 | Tone C disable |
| 3 | Noise A disable |
| 4 | Noise B disable |
| 5 | Noise C disable |

Channel output follows MAME's mixer logic:

```
output = (tone_output OR tone_disable) AND (noise_output OR noise_disable)
```

The output is unipolar (0 or +level), matching real hardware. The level is determined by either the fixed volume (amplitude register bits 3-0) or the envelope volume (when amplitude bit 4 is set).

**Volume Table:**

The 4-bit volume uses a logarithmic scale based on AppleWin/MAME measurements:

| Level | Amplitude | Level | Amplitude |
|-------|-----------|-------|-----------|
| 0 | 0.0000 | 8 | 0.1691 |
| 1 | 0.0137 | 9 | 0.2647 |
| 2 | 0.0205 | 10 | 0.3527 |
| 3 | 0.0291 | 11 | 0.4499 |
| 4 | 0.0423 | 12 | 0.5704 |
| 5 | 0.0618 | 13 | 0.6873 |
| 6 | 0.0847 | 14 | 0.8482 |
| 7 | 0.1369 | 15 | 1.0000 |

The final PSG output is the sum of all three unmuted channels divided by 3 for normalization.

### Phase Coherence

Many Mockingboard music players program both PSGs with identical register values to produce mono output on both channels. Because each PSG maintains independent tone counters, the two chips can drift out of phase, causing cancellation artifacts when the signals are summed.

The emulator detects when both PSGs have identical sound registers (R0-R13) and substitutes PSG1's output for both channels. This eliminates phase cancellation while preserving true stereo when the PSGs are programmed differently.

### Incremental Audio Generation

Rather than generating all Mockingboard audio in bulk when the audio buffer is requested, the emulator generates samples incrementally during CPU execution. The `update()` method is called after each CPU instruction:

1. Accumulate CPU cycles in a fractional counter (`cycleAccum_`)
2. When enough cycles accumulate for one audio sample (~21.3 cycles), generate a single sample from each PSG
3. Store interleaved stereo samples in `sampleAccum_`
4. When the audio system requests samples, `consumeStereoSamples()` drains the accumulated buffer

This approach ensures that PSG register changes from VIA timer IRQ handlers are immediately reflected in the audio output at the correct time, rather than being batched.

---

## Audio Mixing Pipeline

The final stereo output combines the speaker and Mockingboard:

```
Speaker (mono, centered) -----> [mix scale 0.5] --+
                                                    +--> Left channel  --> clamp [-1, +1]
Mockingboard PSG1 (left) -----> [mix scale 0.5] --+

Speaker (mono, centered) -----> [mix scale 0.5] --+
                                                    +--> Right channel --> clamp [-1, +1]
Mockingboard PSG2 (right) ----> [mix scale 0.5] --+
```

Both sources are scaled by 0.5 before mixing to prevent clipping when both are active simultaneously. The Mockingboard output includes per-channel DC offset removal (alpha = 0.9999, ~200 ms time constant at 48 kHz) to convert the unipolar PSG output to bipolar for audio playback.

When the system is muted, the speaker state tracking continues (toggle events are still recorded and processed) but the output buffer is zeroed. Mockingboard samples are consumed and discarded to keep the PSG state synchronized.

---

## JavaScript Audio Layer

### AudioDriver

`AudioDriver` (`src/js/audio/audio-driver.js`) manages the Web Audio API context and serves as the bridge between the browser and the WASM audio generation.

Key constants:

| Constant | Value | Description |
|----------|-------|-------------|
| `SAMPLE_RATE` | 48,000 Hz | Audio output sample rate |
| `AUDIO_BUFFER_SIZE` | 128 | AudioWorklet quantum size |
| `CYCLES_PER_SECOND` | 1,023,000 | Apple IIe CPU clock |
| `DEFAULT_VOLUME` | 0.5 | Initial volume level |

The audio graph is:

```
AudioWorkletNode ("apple-audio-processor")
    --> GainNode (volume/mute control)
        --> AudioContext.destination (speakers)
```

If AudioWorklet is unavailable, the driver falls back to a `ScriptProcessorNode` with a 4096-sample buffer. The ScriptProcessor path deinterleaves stereo samples into separate left/right channel buffers.

### AudioWorklet

`AppleAudioProcessor` (`src/js/audio/audio-worklet.js`) runs on the Web Audio rendering thread. It reads from a shared ring buffer and asks for a refill when the buffer runs low. The request is relayed to the Worker, which generates the samples and writes them into the ring directly -- sample data never crosses the main thread. Without `SharedArrayBuffer` it falls back to receiving samples by `postMessage` (see [[Worker-Architecture]]):

1. Worklet posts `requestSamples` message with count = 1600
2. Main thread runs WASM to generate 1600 stereo sample frames
3. Main thread posts `samples` message with `Float32Array` (transferred, not copied)
4. Worklet appends to its buffer and deinterleaves into left/right output channels

The 128-sample AudioWorklet quantum means the worklet's `process()` method is called approximately 375 times per second.

### Fallback Timing

When the Web Audio API is unavailable or suspended (browser autoplay policy), a `setInterval`-based fallback runs at 60 Hz:

```
cyclesPerTick = 1,023,000 / 60 = ~17,050 cycles
```

The fallback runs `_runCycles()` and checks `_consumeFrameSamples()` for frame updates. Once the user interacts with the page (click or keypress), the AudioContext is resumed and proper audio timing takes over.

---

## Volume and Mute Control

All audio sources share a single unified volume control:

- **JavaScript GainNode** controls the final output volume (0.0 to 1.0)
- **C++ mute flag** zeroes the output buffer while maintaining internal state tracking
- Volume and mute state persist in `localStorage` (`a2e-volume`, `a2e-muted`)
- On startup, saved values are restored and synchronized to both the JS GainNode and the C++ Audio class

The volume setter clamps values to [0.0, 1.0] and updates both the GainNode and the WASM module's internal volume state.

---

## State Serialization

Both the Mockingboard and speaker state are included in save states.

**Mockingboard state size:** 161 bytes total

| Component | Size | Contents |
|-----------|------|----------|
| Enabled flag | 1 byte | Card enabled/disabled |
| VIA 1 | 32 bytes | Port registers, timers, control, PSG state machine |
| PSG 1 | 48 bytes | 16 registers, tone/noise/envelope counters and state |
| VIA 2 | 32 bytes | Same as VIA 1 |
| PSG 2 | 48 bytes | Same as PSG 1 |

PSG state includes the 17-bit noise shift register, envelope counter, and envelope flags to ensure seamless audio continuity across save/load. The DC filter states are reset on load for a clean audio restart.

---

## The IIgs's Ensoniq

A IIgs has an Ensoniq 5503 DOC as well as a speaker. `IIgsSound` models the chip as GSSquared and MAME do: resolution-shifted table addressing, a zero byte halting every mode, the table's end wrapping free-run and halting the rest, swap mode handing over to the partner, sync mode restarting the oscillator below, and one scan per `8 × (oscillators + 2)` ticks of 7.16 MHz.

It runs on the machine's clock rather than on demand, and it **interrupts**: an oscillator with its interrupt bit set raises one when it halts. The sound tools play every sample through swapped pairs refilled from those interrupts, so a chip that only ran when the host asked for a buffer played the first buffer of anything and then stopped.

**Every oscillator is summed, whatever channel it is assigned to.** The chip has one analogue output pin: it visits its channels in turn and puts each one's sample on that same pin, with the channel strobes saying which channel is on it. A stock machine filters the pin and hears the sum; only a stereo card in a slot uses the strobes to pull the channels apart, and there is no such card here. Splitting by the channel field put a game's bass in one speaker and its melody in the other. The uppermost enabled oscillator is heard three times over, which is real silicon.

**The volume nibble in `$C03C` reaches the speaker and not the Ensoniq.** One amplifier really does carry both on the machine, and modelling that sounded wrong: sound software drops the nibble to about 5 and puts it back to 15 around every burst of DOC access, in flips lasting well under ten milliseconds, so scaling the synthesiser by it wobbles a steady note at whatever rate the software happens to be transferring at. The host's volume control is the amplifier for the Ensoniq instead.

Where the nibble *is* applied it is a **taper, not a ratio** — a cube root. That came out of a measurement: a plain ratio put the machine 9.5dB below a //e for the same speaker click at the setting its own firmware boots with. The taper keeps what the nibble is for — silence at zero, full output at fifteen, every step ordered — and puts the default within 3dB of the other machines. The nibble also reads back as it was written, which it did not: forcing it to 15 meant the Control Panel's volume setting and the toolbox's `SetSoundVolume`, which both change it by reading and writing it back, were working from a machine that claimed to be at full volume.

The speaker's gain follows the nibble's writes at the slow-clock times they happened, applied per sample through a twenty-millisecond slew, because one gain per buffer made the ROM's bell fade a staircase and brought its decaying tail back at full level when the volume went back up.

## Source Files

| File | Description |
|------|-------------|
| `src/core/audio/audio.hpp` | Speaker audio class declaration |
| `src/core/audio/audio.cpp` | Speaker toggle recording and sample generation |
| `src/core/cards/mockingboard/mockingboard_card.hpp` | Mockingboard card interface |
| `src/core/cards/mockingboard/mockingboard_card.cpp` | Card I/O routing, mixing, incremental generation |
| `src/core/cards/mockingboard/ay8910.hpp` | AY-3-8910 PSG class declaration |
| `src/core/cards/mockingboard/ay8910.cpp` | Tone, noise, envelope generators |
| `src/core/cards/mockingboard/via6522.hpp` | VIA 6522 class declaration |
| `src/core/cards/mockingboard/via6522.cpp` | Timer, port, IRQ, PSG control protocol |
| `src/js/audio/audio-driver.js` | Web Audio API driver and timing |
| `src/js/audio/audio-worklet.js` | AudioWorklet sample buffer processor |

---

See also: [[Architecture Overview]] | [[Expansion Slots]] | [[CPU Emulation]]
