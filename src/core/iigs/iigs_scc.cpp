/*
 * iigs_scc.cpp - The Z8530 SCC behind a IIgs's two serial ports
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_scc.hpp"

namespace a2e::iigs {

namespace {
// WR0: the pointer in the low three bits, a command in the next three, and
// the CRC/EOM resets in the top two.
constexpr uint8_t WR0_POINTER_MASK = 0x07;
constexpr uint8_t WR0_COMMAND_MASK = 0x38;
constexpr uint8_t WR0_CMD_POINT_HIGH = 0x08;
constexpr uint8_t WR0_CMD_RESET_EXT = 0x10;
constexpr uint8_t WR0_CMD_ENABLE_INT_NEXT_RX = 0x20;
constexpr uint8_t WR0_CMD_RESET_TX_INT = 0x28;
constexpr uint8_t WR0_CMD_ERROR_RESET = 0x30;
constexpr uint8_t WR0_CMD_RESET_IUS = 0x38;
constexpr uint8_t WR0_RESET_TX_UNDERRUN = 0xC0;

// WR1: which events interrupt.
constexpr uint8_t WR1_EXT_INT_ENABLE = 0x01;
constexpr uint8_t WR1_TX_INT_ENABLE = 0x02;
constexpr uint8_t WR1_RX_INT_MASK = 0x18;
constexpr uint8_t WR1_RX_INT_FIRST = 0x08;
constexpr uint8_t WR1_RX_INT_ALL = 0x10;
constexpr uint8_t WR1_RX_INT_SPECIAL = 0x18;

// WR3: the receiver.
constexpr uint8_t WR3_RX_ENABLE = 0x01;

// WR5: the transmitter, and the handshake out.
constexpr uint8_t WR5_TX_ENABLE = 0x08;
constexpr uint8_t WR5_DTR = 0x80;

// WR9: the interrupt control shared by both channels.
constexpr uint8_t WR9_VIS = 0x01;
constexpr uint8_t WR9_MIE = 0x08;
constexpr uint8_t WR9_STATUS_HIGH = 0x10;
constexpr uint8_t WR9_RESET_MASK = 0xC0;
constexpr uint8_t WR9_RESET_B = 0x40;
constexpr uint8_t WR9_RESET_A = 0x80;
constexpr uint8_t WR9_RESET_HARDWARE = 0xC0;

// WR14: the baud rate generator and the loops.
constexpr uint8_t WR14_BRG_ENABLE = 0x01;
constexpr uint8_t WR14_BRG_SOURCE_PCLK = 0x02;
constexpr uint8_t WR14_AUTO_ECHO = 0x08;
constexpr uint8_t WR14_LOCAL_LOOPBACK = 0x10;

// WR4: the clock divider, which sets how many clocks make a bit.
constexpr uint8_t WR4_CLOCK_MASK = 0xC0;

// RR1 after a reset: all sent, residue code 011.
constexpr uint8_t RR1_RESET = 0x07;
constexpr uint8_t RR1_ALL_SENT = 0x01;

// The bits of RR0 the ext/status logic watches.
constexpr uint8_t EXT_STATUS_BITS = IIgsSCC::RR0_ZERO_COUNT | IIgsSCC::RR0_DCD |
                                    IIgsSCC::RR0_SYNC_HUNT | IIgsSCC::RR0_CTS |
                                    IIgsSCC::RR0_TX_UNDERRUN | IIgsSCC::RR0_BREAK;

// The SCC's clocks on a IIgs — PCLK and the crystal on RTxC are the same
// 3.6864MHz — and the Mega II's.
constexpr int64_t PCLK_HZ = 3686400;
constexpr int64_t RTXC_HZ = 3686400;
constexpr int64_t MEGA_II_HZ = 1023000;
} // namespace

void IIgsSCC::reset() { hardwareReset(); }

void IIgsSCC::hardwareReset() {
  for (int c = 0; c < 2; c++) channelReset(c);
  // The shared registers a hardware reset sets and a channel reset leaves.
  channels_[0].wr[2] = channels_[1].wr[2] = 0;
  channels_[0].wr[9] = channels_[1].wr[9] = 0;
  channels_[0].wr[10] = channels_[1].wr[10] = 0;
  channels_[0].wr[11] = channels_[1].wr[11] = 0x08;
  channels_[0].wr[14] = channels_[1].wr[14] = 0x30;
}

void IIgsSCC::channelReset(int channel) {
  Channel &ch = channels_[channel];
  ch.pointer = 0;
  ch.wr[0] = 0;
  ch.wr[1] &= 0x24; // the wait/DMA and parity-is-special bits survive
  ch.wr[3] &= 0xFE;
  ch.wr[4] |= 0x04;
  ch.wr[5] &= 0x65;
  ch.wr[15] = 0xF8;
  ch.rxCount = 0;
  ch.txBufferFull = false;
  ch.txBusy = false;
  ch.txCyclesLeft = 0;
  ch.rxPending = ch.txPending = ch.extPending = false;
  ch.rxFirstCharArmed = true;
  ch.txUnderrun = true;
  ch.extLatched = false;
  ch.zeroCount = false;
  ch.brgCyclesLeft = 0;
  ch.latchedStatus = statusRegister(ch) & EXT_STATUS_BITS;
}

// ===== The four addresses =====

uint8_t IIgsSCC::read(uint8_t offset) {
  const int channel = (offset & 1) ? CHANNEL_A : CHANNEL_B;
  if (offset & 2) return readData(channel);
  Channel &ch = channels_[channel];
  const int reg = ch.pointer;
  ch.pointer = 0;
  return readRegister(channel, reg);
}

void IIgsSCC::write(uint8_t offset, uint8_t value) {
  const int channel = (offset & 1) ? CHANNEL_A : CHANNEL_B;
  if (offset & 2) {
    writeData(channel, value);
    return;
  }
  Channel &ch = channels_[channel];
  const int reg = ch.pointer;
  ch.pointer = 0;
  writeRegister(channel, reg, value);
}

uint8_t IIgsSCC::peek(uint8_t offset) const {
  const int channel = (offset & 1) ? CHANNEL_A : CHANNEL_B;
  const Channel &ch = channels_[channel];
  if (offset & 2) return ch.rxCount > 0 ? ch.rxFifo[0] : 0;
  return statusRegister(ch);
}

// ===== The register file =====

uint8_t IIgsSCC::statusRegister(const Channel &ch) const {
  uint8_t value = 0;
  if (ch.rxCount > 0) value |= RR0_RX_AVAILABLE;
  if (!ch.txBufferFull) value |= RR0_TX_EMPTY;
  if (ch.txUnderrun) value |= RR0_TX_UNDERRUN;
  if (ch.zeroCount) value |= RR0_ZERO_COUNT;
  // CTS is the other port's DTR through the cable — the bit reads set when
  // the pin is pulled low, which is DTR asserted — and nothing otherwise.
  // DCD is never driven: nothing on the cable reaches it.
  if (cable_) {
    const Channel &other = channels_[&ch == &channels_[0] ? 1 : 0];
    if (other.wr[5] & WR5_DTR) value |= RR0_CTS;
  }
  return value;
}

uint8_t IIgsSCC::pendingBits() const {
  const Channel &a = channels_[CHANNEL_A];
  const Channel &b = channels_[CHANNEL_B];
  uint8_t bits = 0;
  if (b.extPending) bits |= RR3_B_EXT;
  if (b.txPending) bits |= RR3_B_TX;
  if (b.rxPending) bits |= RR3_B_RX;
  if (a.extPending) bits |= RR3_A_EXT;
  if (a.txPending) bits |= RR3_A_TX;
  if (a.rxPending) bits |= RR3_A_RX;
  return bits;
}

uint8_t IIgsSCC::modifiedVector() const {
  // RR2 as channel B reads it: the vector with V3-V1 replaced by the highest
  // priority source asking, and 011 — channel B special receive — when none
  // is. WR9's status-high bit puts the code in V6-V4 instead, reversed.
  const uint8_t bits = pendingBits();
  uint8_t code;
  if (bits & RR3_A_RX) code = 0x06;
  else if (bits & RR3_A_TX) code = 0x04;
  else if (bits & RR3_A_EXT) code = 0x05;
  else if (bits & RR3_B_RX) code = 0x02;
  else if (bits & RR3_B_TX) code = 0x00;
  else if (bits & RR3_B_EXT) code = 0x01;
  else code = 0x03;

  const uint8_t vector = channels_[CHANNEL_A].wr[2];
  if (channels_[CHANNEL_A].wr[9] & WR9_STATUS_HIGH) {
    const uint8_t reversed = static_cast<uint8_t>(((code & 1) << 2) | (code & 2) | ((code >> 2) & 1));
    return static_cast<uint8_t>((vector & 0x8F) | (reversed << 4));
  }
  return static_cast<uint8_t>((vector & 0xF1) | (code << 1));
}

uint8_t IIgsSCC::readRegister(int channel, int reg) {
  Channel &ch = channels_[channel];
  switch (reg) {
  case 0:
  case 4:
    // The ext/status bits are frozen from the moment one of them changes
    // until the CPU acknowledges, so a handler sees what interrupted it.
    if (ch.extLatched) {
      return static_cast<uint8_t>((statusRegister(ch) & ~EXT_STATUS_BITS) |
                                  ch.latchedStatus);
    }
    return statusRegister(ch);
  case 1:
  case 5: {
    uint8_t value = 0x06; // residue code 011: eight bits per character
    if (!ch.txBusy && !ch.txBufferFull) value |= RR1_ALL_SENT;
    if (ch.rxCount > 0) value |= ch.rxError[0];
    return value;
  }
  case 2:
  case 6:
    return channel == CHANNEL_A ? channels_[CHANNEL_A].wr[2] : modifiedVector();
  case 3:
  case 7:
    return channel == CHANNEL_A ? pendingBits() : 0;
  case 8:
    return readData(channel);
  case 9:
  case 13:
    return ch.wr[13];
  case 10:
  case 14:
    return 0;
  case 11:
  case 15:
    return ch.wr[15];
  case 12:
    return ch.wr[12];
  }
  return 0;
}

void IIgsSCC::writeRegister(int channel, int reg, uint8_t value) {
  Channel &ch = channels_[channel];
  switch (reg) {
  case 0: {
    ch.pointer = value & WR0_POINTER_MASK;
    switch (value & WR0_COMMAND_MASK) {
    case WR0_CMD_POINT_HIGH:
      ch.pointer += 8;
      break;
    case WR0_CMD_RESET_EXT:
      ch.extPending = false;
      ch.extLatched = false;
      ch.latchedStatus = statusRegister(ch) & EXT_STATUS_BITS;
      break;
    case WR0_CMD_ENABLE_INT_NEXT_RX:
      ch.rxFirstCharArmed = true;
      break;
    case WR0_CMD_RESET_TX_INT:
      ch.txPending = false;
      break;
    case WR0_CMD_ERROR_RESET:
      for (auto &e : ch.rxError) e = 0;
      break;
    case WR0_CMD_RESET_IUS:
      break;
    default:
      break;
    }
    if ((value & WR0_RESET_TX_UNDERRUN) == WR0_RESET_TX_UNDERRUN) {
      ch.txUnderrun = false;
      extStatusChanged(ch);
    }
    return;
  }
  case 2:
    // One vector for the chip, whichever channel wrote it.
    channels_[0].wr[2] = channels_[1].wr[2] = value;
    return;
  case 9:
    channels_[0].wr[9] = channels_[1].wr[9] = static_cast<uint8_t>(value & ~WR9_RESET_MASK);
    switch (value & WR9_RESET_MASK) {
    case WR9_RESET_B:
      channelReset(CHANNEL_B);
      break;
    case WR9_RESET_A:
      channelReset(CHANNEL_A);
      break;
    case WR9_RESET_HARDWARE:
      hardwareReset();
      break;
    default:
      break;
    }
    return;
  case 8:
    writeData(channel, value);
    return;
  case 5: {
    ch.wr[5] = value;
    // DTR is the other port's CTS across the cable, and a change there is
    // an ext/status event on that channel.
    if (cable_) extStatusChanged(channels_[channel == CHANNEL_A ? CHANNEL_B : CHANNEL_A]);
    return;
  }
  default:
    ch.wr[reg] = value;
    return;
  }
}

// ===== Transmit and receive =====

void IIgsSCC::extStatusChanged(Channel &ch) {
  // An ext/status interrupt is a *change* in a bit WR15 enables, and the
  // bits freeze until the CPU resets them.
  if (ch.extLatched) return;
  const uint8_t now = statusRegister(ch) & EXT_STATUS_BITS;
  const uint8_t changed = static_cast<uint8_t>((now ^ ch.latchedStatus) & ch.wr[15]);
  if (!changed) return;
  ch.latchedStatus = now;
  ch.extLatched = true;
  if (ch.wr[1] & WR1_EXT_INT_ENABLE) ch.extPending = true;
}

void IIgsSCC::receiveByte(Channel &ch, uint8_t byte) {
  if (!(ch.wr[3] & WR3_RX_ENABLE)) return;
  if (ch.rxCount < 3) {
    ch.rxFifo[ch.rxCount] = byte;
    ch.rxError[ch.rxCount] = 0;
    ch.rxCount++;
  } else {
    ch.rxError[2] |= 0x20; // overrun: the byte at the back is the one lost
  }
  switch (ch.wr[1] & WR1_RX_INT_MASK) {
  case WR1_RX_INT_FIRST:
    if (ch.rxFirstCharArmed) {
      ch.rxPending = true;
      ch.rxFirstCharArmed = false;
    }
    break;
  case WR1_RX_INT_ALL:
    ch.rxPending = true;
    break;
  default:
    break;
  }
}

uint8_t IIgsSCC::readData(int channel) {
  Channel &ch = channels_[channel];
  if (ch.rxCount == 0) return ch.rxFifo[0];
  const uint8_t byte = ch.rxFifo[0];
  for (int i = 1; i < 3; i++) {
    ch.rxFifo[i - 1] = ch.rxFifo[i];
    ch.rxError[i - 1] = ch.rxError[i];
  }
  ch.rxCount--;
  // A receive interrupt is held while a byte is waiting; taking the last
  // one lets it go. Interrupt-on-all-characters asks again for each.
  if (ch.rxCount == 0 || (ch.wr[1] & WR1_RX_INT_MASK) != WR1_RX_INT_ALL) {
    ch.rxPending = false;
  }
  return byte;
}

void IIgsSCC::writeData(int channel, uint8_t value) {
  Channel &ch = channels_[channel];
  ch.txBuffer = value;
  ch.txBufferFull = true;
  ch.txPending = false; // writing the buffer satisfies the interrupt
  if (!ch.txBusy) {
    // Straight into the shift register, and the buffer is free again.
    ch.txShift = ch.txBuffer;
    ch.txBufferFull = false;
    ch.txBusy = true;
    ch.txCyclesLeft = cyclesPerCharacter(ch);
    // The buffer emptied: that is the transmit interrupt.
    if (ch.wr[1] & WR1_TX_INT_ENABLE) ch.txPending = true;
  }
}

int32_t IIgsSCC::cyclesPerCharacter(const Channel &ch) const {
  // The transmit clock is whatever WR11 says it is: the crystal on RTxC
  // itself, the baud rate generator (PCLK or the crystal, divided by
  // 2 * (TC + 2)), or the TRxC pin, which is the handshake input and carries
  // no clock on a IIgs — so it is taken as the crystal rather than never
  // finishing. Then the x1/x16/x32/x64 divider in WR4, and ten bit times to
  // a character. The Diagnostic's Serial Crystal Test clocks a byte straight
  // from the crystal at x64 and times its all-sent, which is 174 microseconds
  // and not the generator's rate.
  int64_t clockHz;
  switch ((ch.wr[11] >> 3) & 0x03) {
  case 2: { // the baud rate generator
    const int64_t constant = ch.wr[12] | (ch.wr[13] << 8);
    const int64_t source = (ch.wr[14] & WR14_BRG_SOURCE_PCLK) ? PCLK_HZ : RTXC_HZ;
    clockHz = source / (2 * (constant + 2));
    if (!(ch.wr[14] & WR14_BRG_ENABLE)) clockHz = 9600 * 16; // no clock: do not hang
    break;
  }
  default: // RTxC, TRxC, or the DPLL locked to one of them
    clockHz = RTXC_HZ;
    break;
  }
  static const int divider[4] = {1, 16, 32, 64};
  const int64_t bitHz = clockHz / divider[(ch.wr[4] & WR4_CLOCK_MASK) >> 6];
  const int64_t cycles = MEGA_II_HZ * 10 / (bitHz < 50 ? 50 : bitHz);
  return static_cast<int32_t>(cycles < 8 ? 8 : cycles);
}

int32_t IIgsSCC::cyclesPerZeroCount(const Channel &ch) const {
  // The counter reaches zero every TC + 2 clocks and the output toggles each
  // time, which is where the baud rate's divide-by-two comes from — the zero
  // count itself is twice as often as the output. In the Mega II's cycles.
  // The Diagnostic measures the interval between two of them against a
  // window, and a generator counting the output's period is twice too slow.
  const int64_t constant = ch.wr[12] | (ch.wr[13] << 8);
  const int64_t clocks = constant + 2;
  const int64_t source = (ch.wr[14] & WR14_BRG_SOURCE_PCLK) ? PCLK_HZ : RTXC_HZ;
  const int64_t cycles = clocks * MEGA_II_HZ / source;
  return static_cast<int32_t>(cycles < 1 ? 1 : cycles);
}

void IIgsSCC::advanceGenerator(Channel &ch, uint32_t cycles) {
  // The zero count is a moment, not a state: RR0's bit is up while the
  // counter is at zero and down again as it reloads. What lasts is the
  // ext/status latch it sets on the way through, when WR15 enables it.
  if (!(ch.wr[14] & WR14_BRG_ENABLE)) {
    ch.zeroCount = false;
    ch.brgCyclesLeft = 0;
    return;
  }
  // Switched on: the counter loads and counts a whole period before the
  // first zero count, rather than starting at zero.
  if (ch.brgCyclesLeft <= 0) ch.brgCyclesLeft = cyclesPerZeroCount(ch);
  ch.brgCyclesLeft -= static_cast<int32_t>(cycles);
  if (ch.brgCyclesLeft > 0) return;
  ch.brgCyclesLeft += cyclesPerZeroCount(ch);
  if (ch.brgCyclesLeft <= 0) ch.brgCyclesLeft = cyclesPerZeroCount(ch);
  ch.zeroCount = true;
  extStatusChanged(ch);
  ch.zeroCount = false;
}

void IIgsSCC::advance(uint32_t cycles) {
  for (int c = 0; c < 2; c++) {
    Channel &ch = channels_[c];
    advanceGenerator(ch, cycles);
    if (!ch.txBusy) continue;
    ch.txCyclesLeft -= static_cast<int32_t>(cycles);
    if (ch.txCyclesLeft > 0) continue;

    // The byte has left the shift register. Round the loop it goes, if a
    // loop is switched on — local loopback feeds this channel's receiver.
    const uint8_t sent = ch.txShift;
    if (ch.wr[14] & (WR14_LOCAL_LOOPBACK | WR14_AUTO_ECHO)) {
      receiveByte(ch, sent);
    } else if (cable_) {
      receiveByte(channels_[c == CHANNEL_A ? CHANNEL_B : CHANNEL_A], sent);
    }

    if (ch.txBufferFull) {
      ch.txShift = ch.txBuffer;
      ch.txBufferFull = false;
      ch.txCyclesLeft += cyclesPerCharacter(ch);
      if (ch.wr[1] & WR1_TX_INT_ENABLE) ch.txPending = true;
    } else {
      ch.txBusy = false;
      ch.txCyclesLeft = 0;
      // Nothing to send next: in async mode the underrun latch sets.
      if (!ch.txUnderrun) {
        ch.txUnderrun = true;
        extStatusChanged(ch);
      }
    }
  }
}

void IIgsSCC::setLoopbackCable(bool fitted) {
  cable_ = fitted;
  // Plugging or unplugging moves both CTS lines.
  extStatusChanged(channels_[CHANNEL_A]);
  extStatusChanged(channels_[CHANNEL_B]);
}

bool IIgsSCC::interruptPending() const {
  if (!(channels_[CHANNEL_A].wr[9] & WR9_MIE)) return false;
  return pendingBits() != 0;
}

} // namespace a2e::iigs
