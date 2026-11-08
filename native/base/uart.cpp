#include "uart.h"
#include "board.h"
#include "sam3x8e.h"

namespace {
// 이 드라이버가 소유하는 하드웨어 UART 객체는 하나로 제한한다.
// 실제 UART IRQ를 현재 객체로 전달하는 주소다. 객체는 main()이 끝나지 않는 동안 살아있다.
DueUart* active_uart = nullptr;
constexpr uint32_t kErrorMask = UART_SR_OVRE | UART_SR_FRAME | UART_SR_PARE;

// 단일 코어에서 ISR과 메인 사이에 공유 상태를 갱신하는 아주 짧은 구간만 보호한다.
// 이전 PRIMASK를 복구하므로 이미 꺼져 있던 인터럽트를 임의로 켜지 않는다.
class InterruptGuard final {
public:
    InterruptGuard() : previous_(__get_PRIMASK()) { __disable_irq(); }
    ~InterruptGuard() { __set_PRIMASK(previous_); }
private:
    uint32_t previous_;
};
}

bool DueUart::init(uint32_t master_clock_hz, uint32_t baud)
{
    // 분주값 = MCK/(16*baud). 12MHz/250000은3. 0 나눗셈/overflow/반올림 오차를 거부한다.
    // 임의로 가까운 baud를 고르지 않고, 정확히 표현 가능한 설정만 허용한다.
    if (baud == 0 || baud > UINT32_MAX / 16u || active_uart != nullptr)
        return false;
    const uint32_t denominator = 16u * baud;
    const uint32_t divider = master_clock_hz / denominator;
    if (master_clock_hz % denominator != 0 || divider == 0 || divider > 0xFFFFu)
        return false;

    InterruptGuard guard;
    // PCER0: 1을 쓴 장치만 clock enable. 0을 쓴 다른 장치를 끄는 레지스터가 아니다.
    PMC->PMC_PCER0 = (1u << ID_PIOA) | (1u << ID_UART);
    constexpr uint32_t uart_pins = (1u << 8) | (1u << 9);
    // PIOA는 A포트라는 뜻. ABSR의 Peripheral A/B 선택과 별개 개념이다.
    // &= ~mask는 PA8/9의 선택 비트만0으로 지워 Peripheral A(UART)를 고른다.
    // &는비트AND, ~는전체비트반전이다. 나머지핀의기존선택은그대로보존한다.
    PIOA->PIO_ABSR &= ~uart_pins;
    // GPIO 소유권을 해제하여 선택한 UART 회로가 핀을 구동하도록 넘긴다.
    PIOA->PIO_PDR = uart_pins;

    NVIC_DisableIRQ(UART_IRQn);
    UART->UART_IDR = 0xFFFFFFFFu;
    // 사용하지 않는 DMA(PDC)가 수신/송신 레지스터를 동시에 다루지 않게 명시적으로 해제.
    UART->UART_PTCR = UART_PTCR_RXTDIS | UART_PTCR_TXTDIS;
    UART->UART_CR = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RSTSTA;
    UART->UART_MR = UART_MR_PAR_NO | UART_MR_CHMODE_NORMAL; // 8N1, 일반 모드
    UART->UART_BRGR = divider; // 분주값이며 baud 자체가 아니다.

    head_ = tail_ = 0;
    receive_error_ = false;
    active_uart = this; // C# this와 마찬가지로 현재 객체 주소.
    initialized_ = true;
    NVIC_ClearPendingIRQ(UART_IRQn);
    // RX가 SysTick보다 높은 우선순위. ISR은 빠른 바이트 복사만 수행한다.
    // Cortex-M NVIC에서는 작은 우선순위 숫자가 높은 우선순위를 뜻한다.
    NVIC_SetPriority(UART_IRQn, 2);
    UART->UART_CR = UART_CR_RXEN | UART_CR_TXEN;
    UART->UART_IER = UART_IER_RXRDY | UART_IER_OVRE | UART_IER_FRAME | UART_IER_PARE;
    NVIC_EnableIRQ(UART_IRQn);
    return true;
}

void DueUart::on_interrupt()
{
    const uint32_t status = UART->UART_SR;
    if ((status & kErrorMask) != 0) {
        // OVRE=이전 수신값을 읽기 전에 다음 값 도착, FRAME=stop bit 형식 오류,
        // PARE=패리티 오류. 데이터가 유실/변형됐을 수 있으므로 프레임을 신뢰하지 않는다.
        ++hardware_errors_;
        receive_error_ = true;
        if ((status & UART_SR_RXRDY) != 0)
            (void)UART->UART_RHR;
        UART->UART_CR = UART_CR_RSTSTA;
        return;
    }
    if ((status & UART_SR_RXRDY) == 0)
        return;
    const auto value = static_cast<uint8_t>(UART->UART_RHR);
    // 오류 발생 후 main이 확인할 때까지 추가 바이트도 폐기. 일부를 정상인 것처럼 이어붙이지 않는다.
    if (receive_error_)
        return;
    const uint32_t next = (head_ + 1u) & (kBufferSize - 1u);
    if (next == tail_) {
        ++buffer_overflows_;
        receive_error_ = true;
        return;
    }
    receive_buffer_[head_] = value;
    __DMB(); // 데이터 저장이 head 공개보다 먼저 보이도록 한다.
    head_ = next;
}

DueUart::ReadResult DueUart::try_read(uint8_t& value)
{
    InterruptGuard guard;
    if (receive_error_) {
        // 무결성이 의심되는 ring 전체를 버린다. Protocol에는 Error를 별도로 전달한다.
        tail_ = head_;
        receive_error_ = false;
        return ReadResult::Error;
    }
    if (tail_ == head_)
        return ReadResult::Empty;
    value = receive_buffer_[tail_];
    tail_ = (tail_ + 1u) & (kBufferSize - 1u);
    return ReadResult::Byte;
}

bool DueUart::write(const uint8_t* data, uint32_t length, uint32_t timeout_ms)
{
    // timebase가 진행되지 않는 ISR/인터럽트 차단 구간에서 blocking 송신은 금지한다.
    if (!initialized_ || (length != 0 && data == nullptr)
        || __get_IPSR() != 0 || __get_PRIMASK() != 0)
        return false;
    const uint32_t started = Board::milliseconds();
    for (uint32_t i = 0; i < length; ++i) {
        while ((UART->UART_SR & UART_SR_TXRDY) == 0) {
            if (static_cast<uint32_t>(Board::milliseconds() - started) >= timeout_ms) {
                ++transmit_timeouts_;
                return false;
            }
        }
        UART->UART_THR = data[i];
    }
    // TXRDY는 다음 값을 넣을 수 있다는 뜻이다. 마지막 비트 전송 완료는 flush/TXEMPTY로 별도 확인.
    return true;
}

bool DueUart::flush(uint32_t timeout_ms)
{
    if (!initialized_ || __get_IPSR() != 0 || __get_PRIMASK() != 0)
        return false;
    const uint32_t started = Board::milliseconds();
    while ((UART->UART_SR & UART_SR_TXEMPTY) == 0) {
        if (static_cast<uint32_t>(Board::milliseconds() - started) >= timeout_ms) {
            ++transmit_timeouts_;
            return false;
        }
    }
    return true;
}

DueUart::Statistics DueUart::statistics() const
{
    InterruptGuard guard;
    return {hardware_errors_, buffer_overflows_, transmit_timeouts_};
}

extern "C" void UART_Handler(void)
{
    if (active_uart != nullptr)
        active_uart->on_interrupt();
}

