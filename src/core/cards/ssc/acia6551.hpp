/*
 * acia6551.hpp - MOS 6551 ACIA (Asynchronous Communications Interface Adapter)
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

namespace a2e {

/**
 * ACIA6551 - MOS Technology 6551 UART chip emulation
 *
 * The 6551 provides asynchronous serial communication with the following registers:
 *
 * Register Map (offsets 0-3):
 *   +0: Transmit Data (write) / Receive Data (read)
 *   +1: Programmed Reset (write) / Status Register (read)
 *   +2: Command Register (read/write)
 *   +3: Control Register (read/write)
 *
 * Status Register bits:
 *   Bit 7: IRQ active
 *   Bit 6: DSR (Data Set Ready)
 *   Bit 5: DCD (Data Carrier Detect)
 *   Bit 4: TDRE (Transmit Data Register Empty)
 *   Bit 3: RDRF (Receive Data Register Full)
 *   Bit 2: Overrun error
 *   Bit 1: Framing error
 *   Bit 0: Parity error
 *
 * Command Register bits:
 *   Bits 7-5: Parity mode (000 = disabled)
 *   Bit 4: Echo mode
 *   Bits 3-2: Transmit interrupt control
 *   Bit 1: IRQ disable (0 = enabled, 1 = disabled)
 *   Bit 0: DTR control
 *
 * Control Register bits:
 *   Bit 7: Stop bits (0 = 1 stop bit, 1 = 2 stop bits)
 *   Bits 6-5: Word length (00 = 8, 01 = 7, 10 = 6, 11 = 5)
 *   Bit 4: Clock source (0 = external, 1 = internal baud generator)
 *   Bits 3-0: Baud rate select
 */
class ACIA6551 {
public:
    using TxCallback = std::function<void(uint8_t)>;
    using IRQCallback = std::function<void(bool)>;

    ACIA6551() { reset(); }
    ~ACIA6551() = default;

    // Register access
    uint8_t read(uint8_t reg);
    void write(uint8_t reg, uint8_t value);

    // Peek without side effects (for debugger)
    uint8_t peek(uint8_t reg) const;

    // External data input (from WebSocket)
    void receiveData(uint8_t byte);

    // Lifecycle
    void reset();

    // Callbacks
    void setTxCallback(TxCallback cb) { txCallback_ = std::move(cb); }
    void setIRQCallback(IRQCallback cb) { irqCallback_ = std::move(cb); }

    // State access
    bool isIRQActive() const { return irqActive_; }
    uint8_t getStatusReg() const { return statusReg_; }
    uint8_t getCommandReg() const { return commandReg_; }
    uint8_t getControlReg() const { return controlReg_; }

    // Serialization
    static constexpr size_t STATE_SIZE = 16 + 256 + 4; // registers + rx buffer + indices
    size_t serialize(uint8_t* buffer, size_t maxSize) const;
    size_t deserialize(const uint8_t* buffer, size_t size);

private:
    void updateIRQ();

    // Registers
    uint8_t txData_ = 0;
    uint8_t rxData_ = 0;
    uint8_t statusReg_ = 0x10;   // TDRE=1 initially (ready to transmit)
    uint8_t commandReg_ = 0x00;
    uint8_t controlReg_ = 0x00;

    // Receive FIFO
    static constexpr int RX_BUFFER_SIZE = 256;
    uint8_t rxBuffer_[RX_BUFFER_SIZE] = {};
    int rxHead_ = 0;
    int rxTail_ = 0;
    int rxCount_ = 0;

    // IRQ state
    bool irqActive_ = false;

    // Callbacks
    TxCallback txCallback_;
    IRQCallback irqCallback_;
};

} // namespace a2e
