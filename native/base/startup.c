#include <stdint.h>
#include "sam3x8e.h"

// extern은 접근 제어(public)가 아니라 외부 정의를 사용하는 선언이다. 이 이름들은 일반 전역변수가 아니라
// linker.ld가 정한 주소 표식이다. &이름으로 표식 주소를 취득하며 그 위치의 uint32_t값을 읽는 용도가 아니다.
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _stack_top;
int main(void);
void Reset_Handler(void);
void SysTick_Handler(void);
void UART_Handler(void);
void TC0_Handler(void);

static void Default_Handler(void)
{
    // 아직 연결하지 않은 예외/IRQ에서 머문다. 이것만으로 모터/히터 안전 정지를 보장하지 않는다.
    // 현재 시험은 액추에이터 없이 Due 단독으로 수행한다. 제품 단계에서 fault/안전 정지 설계가 필요하다.
    for (;;) {}
}

// __attribute__는 표준 C나 헤더 매크로가 아닌 GCC 확장. section은 .o내 구역 이름을 지정한다.
// used도 GCC 속성이며 sam3x8e.h에 정의된 변수가 아니다. 컴파일러가 배열 생성을 생략하지 않게 한다.
// linker.ld의 KEEP는 별도로 링커의 미사용 구역 제거에서 이 벡터를 보존한다.
// CPU는 파일 이름을 읽지 않고 부팅 메모리의 첫 값을 SP, 둘째 값을 reset 함수 주소로 읽는다.
__attribute__((section(".vectors"), used))
const uintptr_t vectors[16 + PERIPH_COUNT_IRQn] = {
    [0] = (uintptr_t)&_stack_top,
    [1] = (uintptr_t)Reset_Handler,
    [2 ... 14] = (uintptr_t)Default_Handler,
    [15] = (uintptr_t)SysTick_Handler,
    [16 ... 16 + UART_IRQn - 1] = (uintptr_t)Default_Handler,
    [16 + UART_IRQn] = (uintptr_t)UART_Handler,
    [16 + UART_IRQn + 1 ... 16 + TC0_IRQn - 1] = (uintptr_t)Default_Handler,
    [16 + TC0_IRQn] = (uintptr_t)TC0_Handler,
    [16 + TC0_IRQn + 1 ... 16 + PERIPH_COUNT_IRQn - 1] = (uintptr_t)Default_Handler,
};

void Reset_Handler(void)
{
    // WDT는 MMIO 구조체 포인터, WDT_MR은 Mode Register, WDDIS는 watchdog 해제 비트다.
    // 통신 개발 단계에서는 watchdog을 해제한다. 완성된 제어 FW의 안전 정책이라는 뜻은 아니다.
    WDT->WDT_MR = WDT_MR_WDDIS;
    // SCB는 Arm CMSIS에서 정의한 코어 제어 구조체. VTOR에 배열 시작 주소를 기록한다.
    // uintptr_t배열이 uint32_t배열로 바뀌는 것이 아니라 주소 포인터를 정수로 변환하는 표현이다.
    SCB->VTOR = (uint32_t)vectors;

    // .data: 초기값을 Flash에 보관하고 실행 중 변수 자리는 RAM에 배치한다.
    // source/dest는 주소를 기억하는 포인터, *dest는 그 주소의 내용, ++는 다음32bit 위치로 이동.
    uint32_t *source = &_sidata;
    for (uint32_t *dest = &_sdata; dest < &_edata; ++dest)
        *dest = *source++;
    // .bss: 전역/static의0 초기화를 보장한다. main의 모든 지역변수까지0으로 만드는 것은 아니다.
    for (uint32_t *dest = &_sbss; dest < &_ebss; ++dest)
        *dest = 0;

    // 현재 설계는 전역 동적 C++ 생성자를 사용하지 않는다. main 지역 객체는 정상 생성된다.
    // (void)는 main의 int 반환값을 무시한다는 뜻. main은 일반적으로 복귀하지 않는다.
    (void)main();
    for (;;) {}
}

