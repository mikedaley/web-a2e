/*
 * machine_debug.hpp - The parts of debugging that are not about one processor
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "machine/machine_profile.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <vector>

namespace a2e {

/**
 * MachineDebug - breakpoints, watchpoints, a trace ring and beam breakpoints
 *
 * These are the same question whichever processor is running: has execution
 * reached this address, has anything touched that one, what happened just
 * before it went wrong, where is the beam. None of them is about the
 * instruction set, so none of them belongs to a machine — which is why a //e
 * and a IIgs own one of these rather than each having their own answer.
 *
 * **Addresses are 24 bits throughout.** A 6502's address is a 65816's with a
 * zero bank, so the wider one describes both; the alternative was a 16-bit
 * interface with a bank alongside it, which is the same thing with a seam in
 * it for something to fall through.
 *
 * What is deliberately *not* here:
 *
 * - **Anything that reads memory or registers.** The trace entry is filled in
 *   by the machine, because only the machine knows which bus to peek and how
 *   long an instruction is on its processor. This class holds the ring.
 * - **Cycle profiling.** The //e's is an array of one counter per address,
 *   which is 256KB; the same thing for a 65816's address space would be 64MB.
 *   It stays on Emulator until there is a shape that suits both.
 * - **Anything about BASIC.** Applesoft breakpoints are about a program in
 *   memory rather than about a processor, and they live on Emulator.
 */
class MachineDebug {
public:
  // ===== Execution breakpoints =====

  void addBreakpoint(uint32_t address);
  void removeBreakpoint(uint32_t address);
  void enableBreakpoint(uint32_t address, bool enabled);
  void clearBreakpoints();
  bool hasBreakpoints() const { return !breakpoints_.empty(); }

  bool isBreakpointHit() const { return breakpointHit_; }
  uint32_t breakpointAddress() const { return breakpointAddress_; }

  /**
   * Run on past the breakpoint the machine is already sitting on.
   *
   * Without this, continuing from a breakpoint hits the same one again before
   * a single instruction has run.
   */
  void skipNextBreakpoint() { skipBreakpointOnce_ = true; }

  // ===== The temporary breakpoint behind step over and step out =====

  void setTempBreakpoint(uint32_t address);
  void clearTempBreakpoint();
  bool isTempBreakpointActive() const { return tempActive_; }
  bool isTempBreakpointHit() const { return tempHit_; }
  uint32_t tempBreakpointAddress() const { return tempAddress_; }

  /**
   * Should the machine stop before executing the instruction at `pc`?
   *
   * Records which breakpoint it was, so a caller only has to pause. The
   * temporary breakpoint is tested first and disarms itself: a step over that
   * left it armed would stop again on the next loop round the same code.
   */
  bool shouldBreakBefore(uint32_t pc);

  // ===== Watchpoints =====

  enum WatchpointType : uint8_t { WP_READ = 1, WP_WRITE = 2, WP_READWRITE = 3 };

  void addWatchpoint(uint32_t start, uint32_t end, WatchpointType type);
  void removeWatchpoint(uint32_t start);
  void clearWatchpoints();
  bool hasWatchpoints() const { return !watchpoints_.empty(); }

  bool isWatchpointHit() const { return watchpointHit_; }
  uint32_t watchpointAddress() const { return watchpointAddress_; }
  uint8_t watchpointValue() const { return watchpointValue_; }
  bool isWatchpointWrite() const { return watchpointIsWrite_; }

  /**
   * The machine reports every access while it has watchpoints; these decide
   * whether it was a watched one. True means the machine should stop.
   */
  bool onRead(uint32_t address, uint8_t value);
  bool onWrite(uint32_t address, uint8_t value);

  // ===== Beam breakpoints =====

  struct BeamBreakpoint {
    int32_t id;
    int16_t scanline; // -1 matches any
    int16_t hPos;     // -1 matches any
    bool enabled;
    // Which frame and scanline this last fired on, so it fires once per pass
    // over its position rather than on every cycle the beam spends inside it.
    uint64_t lastFireFrame;
    int16_t lastFireScanline;
  };

  /** Returns the new breakpoint's id, or -1 if there is no room for another. */
  int32_t addBeamBreakpoint(int16_t scanline, int16_t hPos);
  void removeBeamBreakpoint(int32_t id);
  void enableBeamBreakpoint(int32_t id, bool enabled);
  void clearBeamBreakpoints();
  bool hasBeamBreakpoints() const { return !beamBreakpoints_.empty(); }

  bool isBeamBreakpointHit() const { return beamHit_; }
  int32_t beamBreakpointHitId() const { return beamHitId_; }
  int16_t beamBreakScanline() const { return beamHitScanline_; }
  int16_t beamBreakHPos() const { return beamHitHPos_; }

  /**
   * Should the machine stop with the beam here?
   *
   * `frameBaseCycle` identifies the frame, which is how a breakpoint fires
   * once per pass over its position rather than on every cycle the beam
   * spends inside it: one that names a scanline fires once a frame, and one
   * that names only a horizontal position fires once a line.
   */
  bool shouldBreakAtBeam(uint64_t frameBaseCycle, int scanline, int hPos);

  static constexpr size_t MAX_BEAM_BREAKPOINTS = 16;

  // ===== The trace ring =====

  /**
   * One instruction, as either processor leaves it.
   *
   * Wide enough for a 65816 — a 24-bit program counter, sixteen-bit registers,
   * a data bank and a direct page, three operand bytes — and a 6502 fills in
   * the low halves and leaves the rest zero. The layout is read by the host,
   * which asks for `entrySize` rather than assuming, so this can grow again.
   */
  struct TraceEntry {
    uint32_t pc;     // 24-bit, bank included
    uint32_t cycle;  // The machine's own clock
    uint16_t a, x, y, sp;
    uint16_t d;      // Direct page; zero on a 6502
    uint8_t p;
    uint8_t dbr;     // Data bank; zero on a 6502
    uint8_t opcode;
    uint8_t operand1, operand2, operand3;
    uint8_t instrLen;
    uint8_t widths;  // See the WIDTH_ flags
    uint8_t padding[6];
  };
  static_assert(sizeof(TraceEntry) == 32, "the host reads this layout");

  // What `widths` says about the processor at the moment of the instruction,
  // which is what decides how long an immediate was.
  static constexpr uint8_t WIDTH_EMULATION = 0x01;
  static constexpr uint8_t WIDTH_A8 = 0x02;
  static constexpr uint8_t WIDTH_INDEX8 = 0x04;

  void setTraceEnabled(bool enabled) { traceEnabled_ = enabled; }
  bool isTraceEnabled() const { return traceEnabled_; }
  void clearTrace() { traceHead_ = 0; traceCount_ = 0; }
  size_t traceCount() const { return traceCount_; }
  size_t traceHead() const { return traceHead_; }
  size_t traceCapacity() const { return traceBuffer_.size(); }
  const TraceEntry *traceBuffer() const { return traceBuffer_.data(); }

  /**
   * The next entry to write, or null if tracing is off.
   *
   * The machine fills it in — only the machine knows which bus to peek and
   * how long an instruction is on its processor — and the ring advances when
   * it is done.
   */
  TraceEntry *beginTraceEntry();

  // ===== Hit state =====

  /** Forget what hit, keeping every breakpoint. Used when resuming. */
  void clearHits();

  /** Forget everything, breakpoints included. Used on a machine reset. */
  void reset();

private:
  std::set<uint32_t> breakpoints_;
  std::set<uint32_t> disabledBreakpoints_;
  bool breakpointHit_ = false;
  uint32_t breakpointAddress_ = 0;
  bool skipBreakpointOnce_ = false;

  uint32_t tempAddress_ = 0;
  bool tempActive_ = false;
  bool tempHit_ = false;

  struct Watchpoint {
    uint32_t start;
    uint32_t end;
    WatchpointType type;
    bool enabled;
  };
  std::vector<Watchpoint> watchpoints_;
  bool watchpointHit_ = false;
  uint32_t watchpointAddress_ = 0;
  uint8_t watchpointValue_ = 0;
  bool watchpointIsWrite_ = false;

  std::vector<BeamBreakpoint> beamBreakpoints_;
  int32_t nextBeamId_ = 1;
  bool beamHit_ = false;
  int32_t beamHitId_ = -1;
  int16_t beamHitScanline_ = -1;
  int16_t beamHitHPos_ = -1;

  std::vector<TraceEntry> traceBuffer_;
  size_t traceHead_ = 0;
  size_t traceCount_ = 0;
  bool traceEnabled_ = false;
};

/**
 * Where the beam is, from a cycle count and the machine's timing.
 *
 * The video scanner counts like a television scans, so the cycle count *is*
 * the beam position, and both machines derive it the same way from their own
 * profile. `hPos` is the horizontal state in the scanner's own numbering, with
 * blanking first; `column` is the visible column, or -1 during blanking.
 */
struct BeamPosition {
  int scanline;
  int hPos;
  int column;
  bool inVerticalBlank;
  bool inHorizontalBlank;
};

BeamPosition beamPosition(uint64_t cycle, const MachineTiming &timing);

} // namespace a2e
