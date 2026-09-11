# CPU Emulation

This page describes the 65C02 CPU emulation implementation, covering the instruction set, addressing modes, cycle timing, decimal mode, interrupts, and NMOS/CMOS behavioral differences.

---

## Table of Contents

- [Overview](#overview)
- [CPU Variant Selection](#cpu-variant-selection)
- [Registers](#registers)
- [Status Flags](#status-flags)
- [Addressing Modes](#addressing-modes)
- [Instruction Set](#instruction-set)
- [65C02 Extensions](#65c02-extensions)
- [Cycle Counting](#cycle-counting)
- [Page Crossing Penalties](#page-crossing-penalties)
- [Branch Timing](#branch-timing)
- [Read-Modify-Write Behavior](#read-modify-write-behavior)
- [Decimal Mode](#decimal-mode)
- [Interrupts](#interrupts)
- [JMP Indirect Bug](#jmp-indirect-bug)
- [Memory Access Pattern](#memory-access-pattern)
- [Reset Sequence](#reset-sequence)
- [Testing](#testing)

---

## Overview

The CPU is implemented in `src/core/cpu/6502/cpu6502.cpp` and `cpu6502.hpp`. It emulates the WDC 65C02 processor used in the Apple IIe Enhanced, running at 1.023 MHz. The implementation is instruction-level cycle-accurate: each instruction consumes the correct number of cycles as looked up from a 256-entry cycle table, with additional cycles added dynamically for page crossings, taken branches, and decimal mode operations.

The CPU does not access memory directly. Instead, it receives read and write callback functions at construction time that route all bus access through the MMU. This allows the MMU to handle soft switches, bank switching, and expansion card I/O transparently.

---

## CPU Variant Selection

The emulator supports two CPU variants:

| Variant | Enum | Description |
|---------|------|-------------|
| NMOS 6502 | `CPUVariant::NMOS_6502` | Original MOS 6502 with JMP indirect bug, invalid decimal mode flags |
| CMOS 65C02 | `CPUVariant::CMOS_65C02` | WDC 65C02 with fixed JMP, valid decimal flags, new instructions |

The Apple IIe Enhanced uses `CMOS_65C02`. The NMOS variant is available for testing with the Klaus Dormann 6502 test suite.

---

## Registers

| Register | Size | Initial Value | Description |
|----------|------|---------------|-------------|
| A | 8-bit | `$00` | Accumulator |
| X | 8-bit | `$00` | X index register |
| Y | 8-bit | `$00` | Y index register |
| SP | 8-bit | `$FD` | Stack pointer (stack at `$0100`-`$01FF`) |
| PC | 16-bit | from `$FFFC` | Program counter (loaded from reset vector) |
| P | 8-bit | `$24` | Processor status (I and U flags set) |

---

## Status Flags

The processor status register (P) contains 8 flags packed into a single byte:

| Bit | Flag | Hex | Description |
|-----|------|-----|-------------|
| 0 | C | `$01` | Carry -- set on unsigned overflow/borrow |
| 1 | Z | `$02` | Zero -- set when result is zero |
| 2 | I | `$04` | Interrupt disable -- masks IRQ when set |
| 3 | D | `$08` | Decimal -- enables BCD arithmetic in ADC/SBC |
| 4 | B | `$10` | Break -- distinguishes BRK from IRQ on stack |
| 5 | U | `$20` | Unused -- always reads as 1 |
| 6 | V | `$40` | Overflow -- set on signed overflow |
| 7 | N | `$80` | Negative -- set when bit 7 of result is 1 |

The B flag is not a physical register bit. It is pushed as 1 by BRK/PHP and as 0 by IRQ/NMI, allowing interrupt handlers to distinguish the source. The U bit is always set when the status register is pushed to the stack.

---

## Addressing Modes

The emulator implements all standard 6502 addressing modes plus 65C02 extensions:

| Mode | Syntax | Bytes | Example | Implementation |
|------|--------|-------|---------|----------------|
| Implied | -- | 1 | `CLC` | No operand |
| Accumulator | A | 1 | `ASL A` | Operates on A register |
| Immediate | #$nn | 2 | `LDA #$42` | `addrImmediate()` -- returns PC, increments PC |
| Zero Page | $nn | 2 | `LDA $42` | `addrZeroPage()` -- fetches byte, address is `$00nn` |
| Zero Page,X | $nn,X | 2 | `LDA $42,X` | `addrZeroPageX()` -- `(fetch() + X) & $FF` (wraps) |
| Zero Page,Y | $nn,Y | 2 | `LDX $42,Y` | `addrZeroPageY()` -- `(fetch() + Y) & $FF` (wraps) |
| Absolute | $nnnn | 3 | `LDA $1234` | `addrAbsolute()` -- fetches 16-bit word |
| Absolute,X | $nnnn,X | 3 | `LDA $1234,X` | `addrAbsoluteX()` -- base + X, +1 cycle if page crossed |
| Absolute,Y | $nnnn,Y | 3 | `LDA $1234,Y` | `addrAbsoluteY()` -- base + Y, +1 cycle if page crossed |
| Indirect | ($nnnn) | 3 | `JMP ($1234)` | `addrIndirect()` -- reads pointer, then reads target |
| (Indirect,X) | ($nn,X) | 2 | `LDA ($42,X)` | `addrIndexedIndirect()` -- `(zp + X) & $FF`, reads pointer |
| (Indirect),Y | ($nn),Y | 2 | `LDA ($42),Y` | `addrIndirectIndexed()` -- reads pointer from zp, adds Y |
| (Indirect ZP) | ($nn) | 2 | `LDA ($42)` | `addrIndirectZP()` -- 65C02 only, reads pointer from zp |
| (Absolute,X) | ($nnnn,X) | 3 | `JMP ($1234,X)` | 65C02 only, used by JMP |
| ZP Relative | $nn,$rr | 3 | `BBR0 $42,$rr` | 65C02 only, used by BBR/BBS |
| Relative | $rr | 2 | `BEQ $rr` | Signed offset from PC+2 |

Zero page addressing always wraps within the `$00`-`$FF` range. For `(Indirect,X)`, both the base and pointer read wrap within zero page.

---

## Instruction Set

### Load/Store

| Opcode | Instruction | Modes |
|--------|-------------|-------|
| LDA | Load A | imm, zp, zp,X, abs, abs,X, abs,Y, (zp,X), (zp),Y, (zp) |
| LDX | Load X | imm, zp, zp,Y, abs, abs,Y |
| LDY | Load Y | imm, zp, zp,X, abs, abs,X |
| STA | Store A | zp, zp,X, abs, abs,X, abs,Y, (zp,X), (zp),Y, (zp) |
| STX | Store X | zp, zp,Y, abs |
| STY | Store Y | zp, zp,X, abs |
| STZ | Store Zero | zp, zp,X, abs, abs,X (65C02) |

### Arithmetic

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| ADC | Add with Carry | A = A + M + C (decimal mode aware) |
| SBC | Subtract with Carry | A = A - M - !C (decimal mode aware) |
| INC | Increment Memory | M = M + 1 |
| DEC | Decrement Memory | M = M - 1 |
| INC A | Increment A | A = A + 1 (65C02) |
| DEC A | Decrement A | A = A - 1 (65C02) |
| INX/INY | Increment X/Y | X/Y = X/Y + 1 |
| DEX/DEY | Decrement X/Y | X/Y = X/Y - 1 |

### Logic

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| AND | Bitwise AND | A = A & M |
| ORA | Bitwise OR | A = A \| M |
| EOR | Bitwise XOR | A = A ^ M |
| BIT | Bit Test | Z = !(A & M), N = M.7, V = M.6 |

### Shift/Rotate

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| ASL | Arithmetic Shift Left | C <- [7..0] <- 0 |
| LSR | Logical Shift Right | 0 -> [7..0] -> C |
| ROL | Rotate Left | C <- [7..0] <- C |
| ROR | Rotate Right | C -> [7..0] -> C |

### Compare

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| CMP | Compare A | Sets C, Z, N from A - M |
| CPX | Compare X | Sets C, Z, N from X - M |
| CPY | Compare Y | Sets C, Z, N from Y - M |

### Branch

All branches are relative with a signed 8-bit offset. Base timing is 2 cycles; +1 if taken, +1 more if page crossed.

| Opcode | Instruction | Condition |
|--------|-------------|-----------|
| BPL | Branch if Plus | N = 0 |
| BMI | Branch if Minus | N = 1 |
| BVC | Branch if Overflow Clear | V = 0 |
| BVS | Branch if Overflow Set | V = 1 |
| BCC | Branch if Carry Clear | C = 0 |
| BCS | Branch if Carry Set | C = 1 |
| BNE | Branch if Not Equal | Z = 0 |
| BEQ | Branch if Equal | Z = 1 |
| BRA | Branch Always | always (65C02) |

### Jump/Subroutine

| Opcode | Hex | Description |
|--------|-----|-------------|
| JMP abs | `$4C` | Jump to absolute address |
| JMP (ind) | `$6C` | Jump indirect |
| JMP (abs,X) | `$7C` | Jump absolute indexed indirect (65C02) |
| JSR | `$20` | Push PC-1, jump to subroutine |
| RTS | `$60` | Pull PC, increment, return from subroutine |
| RTI | `$40` | Pull P and PC, return from interrupt |

### Stack

| Opcode | Hex | Description |
|--------|-----|-------------|
| PHA | `$48` | Push A |
| PLA | `$68` | Pull A (updates N, Z) |
| PHP | `$08` | Push P (with B and U set) |
| PLP | `$28` | Pull P (B cleared, U set) |
| PHX | `$DA` | Push X (65C02) |
| PLX | `$FA` | Pull X (65C02) |
| PHY | `$5A` | Push Y (65C02) |
| PLY | `$7A` | Pull Y (65C02) |

### Transfer

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| TAX | A -> X | Updates N, Z |
| TAY | A -> Y | Updates N, Z |
| TXA | X -> A | Updates N, Z |
| TYA | Y -> A | Updates N, Z |
| TSX | SP -> X | Updates N, Z |
| TXS | X -> SP | No flag changes |

### Flag Operations

| Opcode | Instruction | Description |
|--------|-------------|-------------|
| CLC | Clear Carry | C = 0 |
| SEC | Set Carry | C = 1 |
| CLI | Clear Interrupt | I = 0 |
| SEI | Set Interrupt | I = 1 |
| CLV | Clear Overflow | V = 0 |
| CLD | Clear Decimal | D = 0 |
| SED | Set Decimal | D = 1 |

### BRK

`BRK` ($00) pushes PC+2 and P (with B set) to the stack, sets I, and jumps to the IRQ vector at `$FFFE`/`$FFFF`. On the 65C02, the D flag is also cleared.

---

## 65C02 Extensions

The emulator implements the full WDC 65C02 instruction set, including Rockwell/WDC extensions:

### New Instructions

| Instruction | Opcodes | Description |
|-------------|---------|-------------|
| STZ | `$64`, `$74`, `$9C`, `$9E` | Store zero to memory |
| BRA | `$80` | Unconditional branch |
| PHX/PLX | `$DA`/`$FA` | Push/pull X register |
| PHY/PLY | `$5A`/`$7A` | Push/pull Y register |
| INC A | `$1A` | Increment accumulator |
| DEC A | `$3A` | Decrement accumulator |
| TRB | `$14`, `$1C` | Test and Reset Bits (M = M & ~A, Z from A & M) |
| TSB | `$04`, `$0C` | Test and Set Bits (M = M \| A, Z from A & M) |

### New Addressing Modes

| Instruction | Mode | Description |
|-------------|------|-------------|
| LDA/STA/etc. (zp) | Zero Page Indirect | `$B2`, `$92`, etc. |
| JMP (abs,X) | Absolute Indexed Indirect | `$7C` |
| BIT #imm | Immediate BIT | `$89` -- only sets Z, not N or V |
| BIT zp,X / abs,X | Extended BIT | `$34`, `$3C` |

### Rockwell/WDC Bit Instructions

| Instruction | Opcodes | Description |
|-------------|---------|-------------|
| RMB0-RMB7 | `$07`-`$77` (step `$10`) | Reset (clear) bit n in zero page |
| SMB0-SMB7 | `$87`-`$F7` (step `$10`) | Set bit n in zero page |
| BBR0-BBR7 | `$0F`-`$7F` (step `$10`) | Branch if bit n in zero page is clear |
| BBS0-BBS7 | `$8F`-`$FF` (step `$10`) | Branch if bit n in zero page is set |

BBR/BBS use a 3-byte encoding: opcode, zero page address, relative offset. The bit number is encoded in bits 4-6 of the opcode.

### WAI and STP

`WAI` (`$CB`) and `STP` (`$DB`) are recognized but treated as no-ops in the current implementation.

---

## Cycle Counting

Each instruction's base cycle count is stored in a 256-entry lookup table (`CYCLE_TABLE`). The `executeInstruction()` method loads the base count, executes the opcode, adds any dynamic penalties, then commits the total to `totalCycles_`.

The `getTotalCycles()` method returns cycle counts that account for mid-instruction timing. During instruction execution (when `cycleCount_ > 0`), it returns the cycle of the last bus access rather than the final cycle. This matters for soft switch callbacks that need to know when the effective memory operation occurred.

```cpp
uint64_t getTotalCycles() const {
    return cycleCount_ > 0 ? totalCycles_ + cycleCount_ - 1 : totalCycles_;
}
```

---

## Page Crossing Penalties

Indexed addressing modes (Absolute,X / Absolute,Y / (Indirect),Y) add one extra cycle when the indexing causes a page boundary crossing (i.e., the high byte of the effective address differs from the high byte of the base address).

For **read** operations (LDA, CMP, ADC, etc.), the page crossing check is enabled (`checkPage = true`), adding +1 cycle.

For **write** and **read-modify-write** operations (STA, ASL abs,X, etc.), the page crossing check is disabled (`checkPage = false`) because the 6502 always takes the extra cycle on these operations regardless of whether a page is actually crossed.

---

## Branch Timing

Branch instructions have variable timing:

- **Not taken**: 2 cycles (base)
- **Taken, same page**: 3 cycles (+1)
- **Taken, page crossed**: 4 cycles (+2)

```cpp
void CPU6502::branch(bool condition) {
    int8_t offset = static_cast<int8_t>(fetch());
    if (condition) {
        uint16_t oldPC = pc_;
        pc_ += offset;
        cycleCount_++;
        if ((oldPC & 0xFF00) != (pc_ & 0xFF00)) {
            cycleCount_++;  // Page crossing penalty
        }
    }
}
```

---

## Read-Modify-Write Behavior

Read-modify-write instructions (ASL, LSR, ROL, ROR, INC, DEC on memory) have different bus behavior between the NMOS and CMOS variants:

- **NMOS 6502**: Read value, write original value back (dummy write), write new value. This "double write" can trigger side effects in I/O registers.
- **CMOS 65C02**: Read value, dummy read (re-read), write new value. The dummy read avoids the double-write problem.

```cpp
// Example: ASL zero page
addr = addrZeroPage();
value = read(addr);
if (variant_ == CPUVariant::CMOS_65C02)
    read(addr);         // 65C02: dummy read
else
    write(addr, value); // NMOS: dummy write of old value
write(addr, opASL(value));
```

---

## Decimal Mode

When the D flag is set, ADC and SBC perform BCD (Binary-Coded Decimal) arithmetic. The emulator implements full BCD correction for both operations.

### ADC in Decimal Mode

1. Compute the binary overflow flag (V) from the uncorrected binary result
2. Add low nibbles with carry; if > 9, add 6 (BCD correction) and carry into high nibble
3. Add high nibbles; if > 9, add 6 and set carry
4. On 65C02: N and Z flags are set from the final BCD result (valid flags)
5. On 65C02: takes one extra cycle compared to binary mode

### SBC in Decimal Mode

1. Compute binary carry and overflow flags from the uncorrected binary result
2. Subtract low nibbles with borrow; if < 0, subtract 6 and borrow from high nibble
3. Subtract high nibbles; if < 0, subtract 6
4. N and Z from the final BCD result
5. On 65C02: takes one extra cycle

### NMOS vs CMOS Decimal Behavior

On the NMOS 6502, the N and Z flags after decimal ADC/SBC are derived from the intermediate binary result and may not be meaningful. The 65C02 corrects this by computing N and Z from the final BCD-adjusted result. The implementation uses the same code path for both variants, with the 65C02 producing correct flags by virtue of updating N/Z after BCD correction.

---

## The 65816

The Apple IIgs's processor is a separate core, `CPU65816` in `src/core/cpu/65816/`, and shares nothing with the 6502 one. It has a 24-bit address bus, 16-bit registers whose width changes at runtime, a direct page and stack that can sit anywhere in bank zero, separate banks for code and data, and two operating modes — emulation, which behaves as a 65C02 and is what every IIgs boots into, and native.

It counts cycles but does not model the cycle *pattern*: which cycle of an instruction touches which address. A //e's video reads the bus during those cycles, which is why the 6502 core models them; a IIgs's video reads the Mega II's bus on the other side of the machine, so the count is what matters.

The core is checked against [SingleStepTests/65816](https://github.com/SingleStepTests/65816) — 10,000 recorded before-and-after states for every opcode in both modes, 5.1 million in all, comparing registers, memory and cycle count. See `tests/conformance/test_65816_vectors.cpp`; the vectors are 3GB and are fetched rather than committed.

## Interrupts

### IRQ (Maskable Interrupt)

- Checked at the start of each `executeInstruction()` call
- Only fires if `irqPending_` is true and the I flag is clear
- Pushes PC and P (with B clear, U set) to the stack
- Sets the I flag (and clears D on 65C02)
- Loads PC from the IRQ vector at `$FFFE`/`$FFFF`
- Takes 7 cycles
- The `irq()` method sets `irqPending_ = true` — an edge, latched until the CPU can take it, which is how a device that interrupts while the I flag is set (from inside somebody else's handler) is not forgotten
- **The line itself is sampled every instruction, while the I flag is clear.** `setIRQStatusCallback()` registers a predicate answering "is anything pulling IRQ down right now", and the emulator builds it from the devices that can: the Mockingboard's VIAs, the mouse (card or a //c's IOU), and the serial ports. This is what a level means: a handler that returns without clearing its device is re-entered immediately, and the interrupt stops the instant the device lets go — both of which real hardware does and an edge-only model does not.
- It is sampled only while the I flag is clear, which is cheaper and closer to the part: a level that cannot be taken now will still be there on the instruction after the `CLI`, and a device that asserts and releases entirely inside another handler never interrupted anything on real hardware either.

### NMI (Non-Maskable Interrupt)

- Edge-triggered: only fires once per assertion
- `nmi()` sets `nmiPending_` and `nmiEdge_` (only if `nmiEdge_` is not already set)
- Same push/vector sequence as IRQ but uses vector at `$FFFA`/`$FFFB`
- Cannot be masked by the I flag
- Takes 7 cycles, clears D on 65C02

### BRK (Software Interrupt)

- Pushes PC+2 (skip past the BRK signature byte)
- Pushes P with B flag set (distinguishes from hardware IRQ)
- Jumps to the IRQ vector at `$FFFE`/`$FFFF`
- 65C02 clears D flag

---

## JMP Indirect Bug

The NMOS 6502 has a well-known bug with `JMP ($xxFF)`: when the low byte of the pointer address is `$FF`, the high byte is fetched from `$xx00` instead of `$xx00+$0100`. The emulator faithfully reproduces this behavior for the NMOS variant:

```cpp
uint16_t CPU6502::addrIndirect() {
    uint16_t ptr = fetchWord();
    if (variant_ == CPUVariant::NMOS_6502 && (ptr & 0xFF) == 0xFF) {
        return read(ptr) | (read(ptr & 0xFF00) << 8);  // Bug: wraps within page
    }
    return read(ptr) | (read(ptr + 1) << 8);  // 65C02: correct behavior
}
```

The 65C02 fixes this bug -- `JMP ($xxFF)` correctly reads the high byte from `$xx00+$0100`.

---

## Memory Access Pattern

The CPU never accesses memory directly. All reads and writes go through callback functions provided at construction:

```cpp
CPU6502(ReadCallback read, WriteCallback write, CPUVariant variant);
```

Where:
- `ReadCallback = std::function<uint8_t(uint16_t)>`
- `WriteCallback = std::function<void(uint16_t, uint8_t)>`

In the emulator, these callbacks route through the MMU:

```cpp
cpu_ = std::make_unique<CPU6502>(
    [this](uint16_t addr) { return cpuRead(addr); },
    [this](uint16_t addr, uint8_t val) { cpuWrite(addr, val); },
    CPUVariant::CMOS_65C02);
```

This design means the CPU has no knowledge of bank switching, soft switches, or expansion card I/O. The MMU handles all address decoding transparently.

---

## Reset Sequence

On reset (`reset()` method):

1. A, X, Y are set to `$00`
2. SP is set to `$FD` (the 6502 decrements SP by 3 during reset but does not write)
3. P is set to `$24` (I flag set, U always set)
4. PC is loaded from the reset vector at `$FFFC`/`$FFFD`
5. 7 cycles are added to the total cycle count
6. All interrupt pending flags are cleared

A warm reset (`warmReset()` in the `Emulator` class) only resets the CPU -- memory, disk state, and expansion cards are preserved, matching the behavior of pressing Ctrl+Reset on real hardware.

---

## Testing

The CPU implementation is validated against the Klaus Dormann 6502/65C02 functional test suites:

- **`klaus_6502_test`** -- Tests all NMOS 6502 instructions, addressing modes, and flags, including decimal mode
- **`klaus_65c02_test`** -- Tests 65C02 extended opcodes (STZ, BRA, PHX/PLX, PHY/PLY, TRB/TSB, etc.)

These tests run as native C++ executables and are executed via CTest:

```bash
cd build-native && ctest --verbose
```

The test runner executes the test ROM until PC reaches a known completion address or detects a stuck loop (indicating a test failure). Both tests must pass to confirm correct CPU emulation.

---

## See Also

- [[Architecture-Overview]] -- How the CPU fits into the emulator
- [[Memory-System]] -- MMU that handles CPU read/write callbacks
- [[Video-Rendering]] -- Scanline rendering driven by CPU cycle count
