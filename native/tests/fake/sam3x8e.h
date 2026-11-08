// PC 시험용 레지스터 대역. 실제 MCU 주소/시간/핀의 정확성을 검증하는 모델은 아니다.
#pragma once
#include <stdint.h>
struct TestUart {
    uint32_t UART_IDR=0, UART_PTCR=0, UART_CR=0, UART_MR=0, UART_BRGR=0;
    uint32_t UART_IER=0, UART_SR=0, UART_RHR=0, UART_THR=0;
};
struct TestPio {
    uint32_t PIO_ABSR=0xFFFFFFFFu, PIO_PDR=0;
    uint32_t PIO_SODR=0, PIO_CODR=0, PIO_PUDR=0, PIO_MDDR=0, PIO_PER=0, PIO_OER=0;
};
struct TestPmc { uint32_t PMC_PCER0=0; };
inline TestUart test_uart;
inline TestPio test_pio;
inline TestPio test_piob;
inline TestPmc test_pmc;
#define UART (&test_uart)
#define PIOA (&test_pio)
#define PIOB (&test_piob)
#define PMC (&test_pmc)
constexpr unsigned ID_PIOA=11, ID_UART=8, UART_IRQn=8;
constexpr unsigned ID_PIOB=12;
constexpr uint32_t UART_SR_RXRDY=1u, UART_SR_TXRDY=2u, UART_SR_TXEMPTY=512u;
constexpr uint32_t UART_SR_OVRE=32u, UART_SR_FRAME=64u, UART_SR_PARE=128u;
constexpr uint32_t UART_CR_RSTRX=4u, UART_CR_RSTTX=8u, UART_CR_RSTSTA=256u;
constexpr uint32_t UART_CR_RXEN=16u, UART_CR_TXEN=64u;
constexpr uint32_t UART_MR_PAR_NO=2048u, UART_MR_CHMODE_NORMAL=0u;
constexpr uint32_t UART_PTCR_RXTDIS=2u, UART_PTCR_TXTDIS=512u;
constexpr uint32_t UART_IER_RXRDY=1u, UART_IER_OVRE=32u, UART_IER_FRAME=64u, UART_IER_PARE=128u;
inline uint32_t test_primask=0, test_ipsr=0;
inline uint32_t __get_PRIMASK() { return test_primask; }
inline void __disable_irq() { test_primask=1; }
inline void __set_PRIMASK(uint32_t value) { test_primask=value; }
inline uint32_t __get_IPSR() { return test_ipsr; }
inline void __DMB() {}
inline void NVIC_DisableIRQ(unsigned) {}
inline void NVIC_EnableIRQ(unsigned) {}
inline void NVIC_ClearPendingIRQ(unsigned) {}
inline void NVIC_SetPriority(unsigned,unsigned) {}
inline bool test_irq_pending=false;
inline void NVIC_SetPendingIRQ(unsigned) { test_irq_pending=true; }

// TC0 시계 시험용. SR의 읽기 후 지워지는 성질만 모델링한다.
struct TestStatusRegister {
    uint32_t value = 0;
    operator uint32_t() { const auto result = value; value = 0; return result; }
};
struct TestTcChannel {
    uint32_t TC_CCR=0, TC_IDR=0, TC_CMR=0, TC_IER=0, TC_CV=0;
    TestStatusRegister TC_SR;
};
struct TestTc { TestTcChannel TC_CHANNEL[3]; };
inline TestTc test_tc;
#define TC0 (&test_tc)
constexpr unsigned ID_TC0=27, TC0_IRQn=27;
constexpr uint32_t TC_CCR_CLKDIS=2, TC_CCR_CLKEN=1, TC_CCR_SWTRG=4;
constexpr uint32_t TC_CMR_TCCLKS_TIMER_CLOCK1=0, TC_CMR_WAVE=1u<<15, TC_CMR_WAVSEL_UP=0;
constexpr uint32_t TC_SR_COVFS=1, TC_IER_COVFS=1;
constexpr uint32_t TC_SR_CPAS=4;
