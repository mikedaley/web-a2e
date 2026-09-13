/*
 * test_iigs_debug.cpp - Debugging a IIgs
 *
 * The same facilities the //e's test_emulator_debug.cpp pins, asked of the
 * machine that has banks: a breakpoint is an address with a bank in it, a
 * watchpoint is the *program* touching memory rather than the video scanner
 * reading it, a step over has to know that a long call is four bytes, and a
 * trace entry has to hold registers twice the width.
 *
 * No ROM is needed: the code under test is written into the machine's own RAM
 * and the processor is pointed at it.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "cpu65816.hpp"
#include "iigs_machine.hpp"
#include "iigs_memory.hpp"
#include "machine/machine_profile.hpp"
#include "mmu/mmu.hpp"

#include <vector>

using namespace a2e;
using namespace a2e::iigs;

namespace {

constexpr uint32_t bankAddress(uint8_t bank, uint16_t offset) {
  return (static_cast<uint32_t>(bank) << 16) | offset;
}

// A machine with a program in it and the processor pointed at the program.
struct Program {
  IIgsMachine machine;

  Program(uint8_t bank, uint16_t at, const std::vector<uint8_t> &bytes) {
    for (size_t i = 0; i < bytes.size(); i++) {
      machine.memory().write(bankAddress(bank, static_cast<uint16_t>(at + i)),
                             bytes[i]);
    }
    machine.cpu().setPBR(bank);
    machine.cpu().setPC(at);
    // Native mode with sixteen-bit registers, which is where a IIgs program
    // spends its time and where the widths actually matter. Leaving emulation
    // mode does not widen anything on its own — the M and X flags stay as
    // they were, which is why a IIgs program's first instruction after XCE is
    // almost always REP — so they are cleared here as that REP would.
    machine.cpu().setEmulation(false);
    machine.cpu().setP(static_cast<uint8_t>(
        (machine.cpu().getP() & ~(FLAG816_M | FLAG816_X)) | FLAG816_I));
    machine.cpu().setSP(0x01FF);
  }

  MachineDebug &debug() { return machine.debug(); }
  uint32_t pc() {
    return (static_cast<uint32_t>(machine.cpu().getPBR()) << 16) |
           machine.cpu().getPC();
  }
};

// NOP, NOP, NOP, ... — somewhere to put a breakpoint in the middle of.
const std::vector<uint8_t> NOPS(32, 0xEA);

} // namespace

TEST_CASE("A breakpoint stops a IIgs at the address it names",
          "[iigs][debug][breakpoint]") {
  Program program(0x02, 0x0300, NOPS);
  program.debug().addBreakpoint(bankAddress(0x02, 0x0304));

  program.machine.runCycles(1000);

  REQUIRE(program.debug().isBreakpointHit());
  REQUIRE(program.debug().breakpointAddress() == bankAddress(0x02, 0x0304));
  REQUIRE(program.pc() == bankAddress(0x02, 0x0304));
  REQUIRE(program.machine.isPaused());
}

TEST_CASE("A breakpoint belongs to one bank", "[iigs][debug][breakpoint]") {
  // The thing a 16-bit debugger cannot express. The same offset in a different
  // bank is a different instruction, and on this machine both are running
  // code: bank $00 is a program's and $E1 is the Mega II's.
  Program program(0x02, 0x0300, NOPS);
  program.debug().addBreakpoint(bankAddress(0x03, 0x0304));

  program.machine.runCycles(2000);

  REQUIRE_FALSE(program.debug().isBreakpointHit());
  REQUIRE_FALSE(program.machine.isPaused());
}

TEST_CASE("A removed or disabled breakpoint does not stop a IIgs",
          "[iigs][debug][breakpoint]") {
  SECTION("removed") {
    Program program(0x02, 0x0300, NOPS);
    program.debug().addBreakpoint(bankAddress(0x02, 0x0304));
    program.debug().removeBreakpoint(bankAddress(0x02, 0x0304));
    program.machine.runCycles(1000);
    REQUIRE_FALSE(program.debug().isBreakpointHit());
  }
  SECTION("disabled") {
    Program program(0x02, 0x0300, NOPS);
    program.debug().addBreakpoint(bankAddress(0x02, 0x0304));
    program.debug().enableBreakpoint(bankAddress(0x02, 0x0304), false);
    program.machine.runCycles(1000);
    REQUIRE_FALSE(program.debug().isBreakpointHit());
  }
}

TEST_CASE("Resuming runs past the breakpoint it is sitting on",
          "[iigs][debug][breakpoint]") {
  // Without this, continuing from a breakpoint stops on the same one again
  // having run nothing, and the machine cannot be got going.
  Program program(0x02, 0x0300, NOPS);
  program.debug().addBreakpoint(bankAddress(0x02, 0x0304));
  program.machine.runCycles(1000);
  REQUIRE(program.pc() == bankAddress(0x02, 0x0304));

  program.machine.setPaused(false);
  program.machine.runCycles(100);
  REQUIRE(program.pc() != bankAddress(0x02, 0x0304));
}

TEST_CASE("A watchpoint sees the program, not the video scanner",
          "[iigs][debug][watchpoint]") {
  // Which is why the check is on the processor's bus rather than inside the
  // memory: the Mega II's video reads the text page on every one of 192
  // lines, and a watchpoint there that fired for the scanner would stop the
  // machine before a program had touched anything.
  SECTION("a write from the program stops it") {
    // LDA #$42 / STA $E10400 (long), then NOPs.
    Program program(0x02, 0x0300,
                    {0xA9, 0x42, 0x00, 0x8F, 0x00, 0x04, 0xE1, 0xEA, 0xEA});
    program.debug().addWatchpoint(bankAddress(0xE1, 0x0400),
                                  bankAddress(0xE1, 0x0400),
                                  MachineDebug::WP_WRITE);
    program.machine.runCycles(1000);

    REQUIRE(program.debug().isWatchpointHit());
    REQUIRE(program.debug().watchpointAddress() == bankAddress(0xE1, 0x0400));
    REQUIRE(program.debug().isWatchpointWrite());
    REQUIRE(program.machine.isPaused());
  }

  SECTION("a screenful of scanning does not") {
    Program program(0x02, 0x0300, NOPS);
    program.debug().addWatchpoint(bankAddress(0xE0, 0x0400),
                                  bankAddress(0xE0, 0x07FF),
                                  MachineDebug::WP_READWRITE);
    // More than a frame, so the video has read the whole text page.
    const auto &timing = machineProfile(MachineId::AppleIIgs).timing;
    program.machine.runCycles(timing.cyclesPerFrame() + 1000);

    REQUIRE_FALSE(program.debug().isWatchpointHit());
  }
}

TEST_CASE("Step over knows a long call is four bytes",
          "[iigs][debug][stepover]") {
  SECTION("JSL") {
    // JSL $020400, and the instruction after it is four bytes on. The
    // subroutine is a bare RTL, so the step has somewhere to come back from.
    Program program(0x02, 0x0300, {0x22, 0x00, 0x04, 0x02, 0xEA});
    program.machine.memory().write(bankAddress(0x02, 0x0400), 0x6B); // RTL
    const uint32_t after = program.machine.stepOver();
    REQUIRE(after == bankAddress(0x02, 0x0304));

    program.machine.runCycles(20000);
    REQUIRE(program.debug().isTempBreakpointHit());
    REQUIRE(program.pc() == bankAddress(0x02, 0x0304));
  }

  SECTION("JSR, which is three") {
    Program program(0x02, 0x0300, {0x20, 0x00, 0x04, 0xEA});
    REQUIRE(program.machine.stepOver() == bankAddress(0x02, 0x0303));
  }

  SECTION("anything else is a single step") {
    Program program(0x02, 0x0300, NOPS);
    REQUIRE(program.machine.stepOver() == 0);
    REQUIRE(program.pc() == bankAddress(0x02, 0x0301));
  }
}

TEST_CASE("Step out returns to what the call pushed",
          "[iigs][debug][stepout]") {
  SECTION("a short call, whose return stays in the program bank") {
    // JSR $0400, and at $0400 a NOP: step out from inside the subroutine.
    Program program(0x02, 0x0300, {0x20, 0x00, 0x04});
    program.machine.memory().write(bankAddress(0x02, 0x0400), 0xEA);
    program.machine.stepInstruction(); // take the call
    REQUIRE(program.pc() == bankAddress(0x02, 0x0400));

    REQUIRE(program.machine.stepOut() == bankAddress(0x02, 0x0303));
  }

  SECTION("a long call, whose return names its own bank") {
    // JSL $030400 from bank $02: the return address on the stack carries the
    // bank, and the RTL at the far end is what says to read it.
    Program program(0x02, 0x0300, {0x22, 0x00, 0x04, 0x03});
    program.machine.memory().write(bankAddress(0x03, 0x0400), 0x6B); // RTL
    program.machine.stepInstruction();
    REQUIRE(program.pc() == bankAddress(0x03, 0x0400));

    // Four bytes of JSL, so the instruction after it is at $0304.
    REQUIRE(program.machine.stepOut() == bankAddress(0x02, 0x0304));
  }
}

TEST_CASE("A trace entry holds what a 65816 has to say", "[iigs][debug][trace]") {
  // Sixteen-bit registers, a program counter with a bank in it, a data bank, a
  // direct page, and the widths — without which an immediate's length, and so
  // every instruction after it, cannot be read back.
  Program program(0x02, 0x0300, {0xA9, 0x34, 0x12, 0xEA, 0xEA});
  program.debug().setTraceEnabled(true);
  program.machine.runCycles(200);

  REQUIRE(program.debug().traceCount() > 1);
  const MachineDebug::TraceEntry *entries = program.debug().traceBuffer();
  const MachineDebug::TraceEntry &first = entries[0];

  REQUIRE(first.pc == bankAddress(0x02, 0x0300));
  REQUIRE(first.opcode == 0xA9);
  REQUIRE(first.instrLen == 3); // sixteen-bit accumulator
  REQUIRE(first.operand1 == 0x34);
  REQUIRE(first.operand2 == 0x12);
  REQUIRE((first.widths & MachineDebug::WIDTH_EMULATION) == 0);
  REQUIRE((first.widths & MachineDebug::WIDTH_A8) == 0);

  // The instruction after the load has the whole sixteen bits in A.
  REQUIRE(entries[1].a == 0x1234);
  REQUIRE(entries[1].sp == 0x01FF);

  SECTION("and clearing the trace empties it") {
    program.debug().clearTrace();
    REQUIRE(program.debug().traceCount() == 0);
  }
}

TEST_CASE("A beam breakpoint stops a IIgs on a scanline",
          "[iigs][debug][beam]") {
  Program program(0x02, 0x0300, NOPS);
  // NOPs run off the end of the program into zeroed memory, which is BRK, so
  // fill the bank with something that runs forever instead.
  for (uint16_t at = 0x0300; at != 0x0000; at++) {
    program.machine.memory().write(bankAddress(0x02, at), 0xEA);
  }

  const int32_t id = program.debug().addBeamBreakpoint(100, -1);
  REQUIRE(id > 0);

  const auto &timing = machineProfile(MachineId::AppleIIgs).timing;
  program.machine.runCycles(timing.cyclesPerFrame());

  REQUIRE(program.debug().isBeamBreakpointHit());
  REQUIRE(program.debug().beamBreakpointHitId() == id);
  REQUIRE(program.debug().beamBreakScanline() == 100);
  REQUIRE(program.machine.isPaused());
}

TEST_CASE("The beam is where the machine's own clock says",
          "[iigs][debug][beam]") {
  IIgsMachine machine;
  const auto &timing = machineProfile(MachineId::AppleIIgs).timing;

  BeamPosition beam = machine.beam();
  REQUIRE(beam.scanline == 0);
  REQUIRE(beam.hPos == 0);
  REQUIRE(beam.inHorizontalBlank); // a scanline starts in blanking
  REQUIRE(beam.column == -1);

  // A IIgs counts the beam in the Mega II's cycles, which is the same
  // 65-by-262 raster a //e has.
  REQUIRE(timing.cyclesPerScanline == 65);
  REQUIRE(timing.scanlinesPerFrame == 262);
}
