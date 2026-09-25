/*
 * disasm_align.hpp - Choosing where a disassembly listing starts.
 *
 * A listing centred on an address has to show that address as an
 * instruction, because the address is almost always the program counter and
 * a program counter is an instruction boundary by definition. The bytes
 * before it are another matter: instruction boundaries cannot be recovered by
 * scanning backwards, and whatever sits above the centre may be a table, a
 * page of ROM that is not code, or an empty slot reading the floating bus.
 *
 * The rule here is the one most debuggers use. Try each start address from
 * the furthest lookback inwards, walk forward an instruction at a time, and
 * keep the first walk that lands exactly on the centre with the context asked
 * for. A walk that overshoots the centre started mid-instruction and is
 * thrown away. Should nothing align at all, the listing starts at the centre
 * with no context, which is honest, where the old fixed lookback showed the
 * centre swallowed by a misread operand and never listed it: paused at $C600
 * with slot 5 empty, every byte above read $A0 and "LDY #$A2" at $C5FF ate
 * the LDX at $C600 (issue #76).
 *
 * Header-only and templated on the length function so the binding, which
 * chooses between two disassemblers by what machine is running, can hand it
 * either, and the tests can hand it a table.
 */

#pragma once

#include <cstdint>

namespace a2e {

/**
 * Pick the address a listing centred on `centre` should begin at.
 *
 * @param centre             Offset within the bank that must appear as an
 *                           instruction boundary.
 * @param instructionsBefore How many instructions of context are wanted
 *                           above it.
 * @param maxOperandBytes    Longest instruction on the machine (3 on a 6502,
 *                           4 on a 65816); sets how far back the search goes.
 * @param lengthAt           Callable: offset -> length of the instruction
 *                           decoded there. A result below 1 counts as 1.
 * @return                   The offset to start listing from. Walking forward
 *                           from it, one instruction at a time, reaches
 *                           `centre` exactly.
 */
template <typename LengthAt>
uint16_t alignedDisassemblyStart(uint16_t centre, int instructionsBefore,
                                 int maxOperandBytes, LengthAt lengthAt) {
  if (instructionsBefore <= 0) return centre;

  // Enough bytes for the context at the longest instruction the machine has,
  // and a little more so a walk has room to fall into step.
  const int maxLookback = instructionsBefore * maxOperandBytes + 10;

  // Best start seen so far when no walk supplies the full context.
  uint16_t bestStart = centre;
  int bestCount = 0;

  for (int lookback = maxLookback; lookback >= 1; lookback--) {
    const int start = static_cast<int>(centre) - lookback;
    if (start < 0) continue;

    // Walk forward, remembering the last `instructionsBefore` boundaries in a
    // ring so the answer can be read off once the centre is reached.
    uint16_t ring[64];
    const int ringSize =
        instructionsBefore < 64 ? instructionsBefore : 64;
    int count = 0;
    int at = start;
    while (at < static_cast<int>(centre)) {
      ring[count % ringSize] = static_cast<uint16_t>(at);
      count++;
      const int length = static_cast<int>(lengthAt(static_cast<uint16_t>(at)));
      at += length > 0 ? length : 1;
    }
    if (at != static_cast<int>(centre)) continue; // Overshot: misaligned.

    if (count >= ringSize) {
      // The oldest boundary in the ring is the one `ringSize` back.
      return ring[count % ringSize];
    }
    if (count > bestCount) {
      bestCount = count;
      bestStart = static_cast<uint16_t>(start);
    }
  }

  return bestStart;
}

} // namespace a2e
