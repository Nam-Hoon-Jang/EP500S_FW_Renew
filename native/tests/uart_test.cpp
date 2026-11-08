#include "uart.h"
#include "board.h"
#include "sam3x8e.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>

namespace { uint32_t now=0; }
namespace Board {
uint32_t milliseconds() { return now++; }
} // namespace Board
extern "C" void UART_Handler(void);

int main() {
    DueUart uart;
    assert(!uart.init(12000000,0));
    assert(!uart.init(12000000,UINT32_MAX));
    assert(!uart.init(12000000,250001));
    assert(uart.init(12000000,250000));
    assert(UART->UART_BRGR==3);
    assert((PIOA->PIO_ABSR & ((1u<<8)|(1u<<9)))==0);
    assert(PIOA->PIO_PDR==((1u<<8)|(1u<<9)));
    assert(test_primask==0);
    uint8_t value=123;
    assert(uart.try_read(value)==DueUart::ReadResult::Empty && value==123);
    auto push=[](uint8_t byte) {
        UART->UART_SR=UART_SR_RXRDY;
        UART->UART_RHR=byte;
        UART_Handler();
    };
    for (unsigned round=0;round<3;++round) {
        for (unsigned i=0;i<255;++i) push(static_cast<uint8_t>(i));
        for (unsigned i=0;i<255;++i) {
            assert(uart.try_read(value)==DueUart::ReadResult::Byte);
            assert(value==static_cast<uint8_t>(i));
        }
        assert(uart.try_read(value)==DueUart::ReadResult::Empty);
    }
    for (unsigned i=0;i<256;++i) push(1);
    assert(uart.try_read(value)==DueUart::ReadResult::Error);
    assert(uart.try_read(value)==DueUart::ReadResult::Empty);
    assert(uart.statistics().buffer_overflows==1);
    for (uint32_t error : {UART_SR_OVRE,UART_SR_FRAME,UART_SR_PARE}) {
        UART->UART_SR=error|UART_SR_RXRDY;
        UART_Handler();
        assert(uart.try_read(value)==DueUart::ReadResult::Error);
        assert(UART->UART_CR==UART_CR_RSTSTA);
    }
    assert(uart.statistics().hardware_errors==3);
    push(0xA5);
    assert(uart.try_read(value)==DueUart::ReadResult::Byte && value==0xA5);
    UART->UART_SR=UART_SR_TXRDY|UART_SR_TXEMPTY;
    const uint8_t data[]{0x11,0x22};
    assert(uart.write(data,2) && UART->UART_THR==0x22);
    assert(uart.flush());
    assert(!uart.write(nullptr,1));
    test_ipsr=1;
    assert(!uart.write(data,2));
    assert(!uart.flush());
    test_ipsr=0;
    test_primask=1;
    assert(!uart.write(data,2));
    (void)uart.statistics();
    assert(test_primask==1); // 기존 interrupt차단상태보존
    test_primask=0;
    now=UINT32_MAX-2u;
    UART->UART_SR=0;
    assert(!uart.write(data,2,5));
    assert(!uart.flush(5));
    assert(uart.statistics().transmit_timeouts==2);
    DueUart second;
    assert(!second.init(12000000));
    puts("PASS: UART ring/wrap/overflow/errors/TX-timeout/flush/IRQ-context/clock-validation (fake MMIO)");
}

