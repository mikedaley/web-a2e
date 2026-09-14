/*
 * iigs_scc.hpp - The Z8530 SCC behind a IIgs's two serial ports
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../emulator/state_stream.hpp"

#include <array>
#include <cstdint>
#include <functional>

namespace a2e::iigs {

/**
 * IIgsSCC - the Zilog Z8530 at $C038-$C03B
 *
 * Two serial channels behind four addresses: a command register and a data
 * register for each. The command register is a window onto sixteen write
 * registers and sixteen read registers per channel — a write to it with the
 * pointer at zero picks a register, and the next access goes to that one and
 * puts the pointer back — which is how sixty-odd registers fit in four bytes.
 *
 * Nothing is plugged into either port, and this is not a serial link to the
 * outside world. It is the chip itself: the register file, the transmitter
 * and receiver with their timing, local loopback and auto echo, and the
 * interrupt logic, because software exercises those without a cable. The
 * ROM's interrupt manager asks RR3 first on every interrupt, the serial
 * firmware programs a channel and waits on RR0, and the IIgs Diagnostic's
 * Serial Internal Test resets the chip, writes its registers and reads them
 * back, and sends bytes round the loop and expects them received — with
 * interrupts.
 *
 * Offsets are the four addresses in order: command B, command A, data B,
 * data A. Time is the Mega II's clock, passed to advance().
 */
class IIgsSCC {
public:
  IIgsSCC() { reset(); }

  /** A hardware reset, as the pin or WR9's command does it. */
  void reset();

  uint8_t read(uint8_t offset);
  void write(uint8_t offset, uint8_t value);

  /** A read with no side effects, for the debugger. */
  uint8_t peek(uint8_t offset) const;

  /** The clock has moved: shift bytes out, and round the loop back in. */
  void advance(uint32_t cycles);

  /** Whether the chip is holding the interrupt line down. */
  bool interruptPending() const;

  /**
   * A loopback cable between the two ports, as the Diagnostic's External
   * Serial Ports Test asks for: each port's transmit data into the other's
   * receiver, and each port's handshake out (DTR) into the other's handshake
   * in (CTS).
   *
   * **Not fitted by default.** A cable between a machine's own two ports is a
   * test rig, not a machine: with one on, a byte the printer driver sends goes
   * round to the other port instead of out of the back, so nothing plugged in
   * would ever hear anything. It is a choice the host makes, and the only
   * thing that asks for it is that one diagnostic.
   */
  void setLoopbackCable(bool fitted);
  bool hasLoopbackCable() const { return cable_; }

  /**
   * Where a byte goes when it leaves a port with no cable on it: out of the
   * machine, to whatever the host has on the other end.
   *
   * The channel is which port sent it — A is the printer port (slot 1) and B
   * the modem port (slot 2), which the firmware settles rather than the
   * address order: slot 1's firmware programs $C039/$C03B and slot 2's
   * $C038/$C03A.
   */
  using TransmitCallback = std::function<void(int channel, uint8_t byte)>;
  void setTransmitCallback(TransmitCallback cb) { transmit_ = std::move(cb); }

  /** A byte arriving from outside, into one port's receiver. */
  void receive(int channel, uint8_t byte) { receiveByte(channels_[channel & 1], byte); }

  // ===== For the tests =====

  uint8_t writeRegister(int channel, int reg) const { return channels_[channel].wr[reg & 0x0F]; }
  bool transmitterBusy(int channel) const { return channels_[channel].txBusy; }

  /** Both channels in a save state; the cable is a host setting and is not. */
  void serialize(StateWriter &w) const;
  void deserialize(StateReader &r);

  static constexpr int CHANNEL_A = 0;
  static constexpr int CHANNEL_B = 1;

  // RR0 bits.
  static constexpr uint8_t RR0_RX_AVAILABLE = 0x01;
  static constexpr uint8_t RR0_ZERO_COUNT = 0x02;
  static constexpr uint8_t RR0_TX_EMPTY = 0x04;
  static constexpr uint8_t RR0_DCD = 0x08;
  static constexpr uint8_t RR0_SYNC_HUNT = 0x10;
  static constexpr uint8_t RR0_CTS = 0x20;
  static constexpr uint8_t RR0_TX_UNDERRUN = 0x40;
  static constexpr uint8_t RR0_BREAK = 0x80;

  // RR3 bits: which sources are asking, channel B in the low three.
  static constexpr uint8_t RR3_B_EXT = 0x01;
  static constexpr uint8_t RR3_B_TX = 0x02;
  static constexpr uint8_t RR3_B_RX = 0x04;
  static constexpr uint8_t RR3_A_EXT = 0x08;
  static constexpr uint8_t RR3_A_TX = 0x10;
  static constexpr uint8_t RR3_A_RX = 0x20;

private:
  struct Channel {
    std::array<uint8_t, 16> wr{};
    int pointer = 0; // the register the next command access reaches

    // The receiver: a three-byte FIFO, and the error bits that travel with
    // each byte as RR1 shows them for the byte at the front.
    std::array<uint8_t, 3> rxFifo{};
    std::array<uint8_t, 3> rxError{};
    int rxCount = 0;

    // The transmitter: a buffer the CPU writes and a shift register it
    // empties into, timed in cycles.
    uint8_t txBuffer = 0;
    bool txBufferFull = false;
    bool txBusy = false; // a byte is on its way out
    uint8_t txShift = 0;
    int32_t txCyclesLeft = 0;

    // The status the ext/status logic latches and compares against.
    uint8_t latchedStatus = 0; // RR0's ext/status bits at the last reset-ext
    bool extLatched = false;   // RR0 is frozen until the CPU resets it

    // Interrupt sources pending: set by events, cleared by their reset.
    bool rxPending = false;
    bool txPending = false;
    bool extPending = false;
    bool rxFirstCharArmed = true; // "interrupt on first character" is one-shot

    bool txUnderrun = true; // RR0 bit 6, set by reset

    // The baud rate generator, counting down to its zero count.
    int32_t brgCyclesLeft = 0;
    bool zeroCount = false; // RR0 bit 1: true for the instant it hits zero
  };

  uint8_t readRegister(int channel, int reg);
  void writeRegister(int channel, int reg, uint8_t value);
  uint8_t statusRegister(const Channel &ch) const;
  uint8_t pendingBits() const;
  uint8_t modifiedVector() const;
  void channelReset(int channel);
  void hardwareReset();
  void extStatusChanged(Channel &ch);
  void receiveByte(Channel &ch, uint8_t byte);
  uint8_t readData(int channel);
  void writeData(int channel, uint8_t value);
  int32_t cyclesPerCharacter(const Channel &ch) const;
  int32_t cyclesPerZeroCount(const Channel &ch) const;
  void advanceGenerator(Channel &ch, uint32_t cycles);

  std::array<Channel, 2> channels_;
  bool cable_ = false;
  TransmitCallback transmit_;
};

} // namespace a2e::iigs
