#include "board.h"
#include "sam3x8e.h"

namespace {
    // ISR은 언제든 실행될 수 있다. volatile은 실제 메모리를 읽고 쓰도록 하지만
    // 다중 스레드 동기화 도구는 아니다. 여기서는 정렬된 32bit 단일 읽기/쓰기와 단일 ISR을 쓴다.
    volatile uint32_t elapsed_ms = 0;

    // 클록 초기화 전에는 밀리초 타이머가 없으므로 유한한 반복 횟수로 실패를 검출한다.
    // 이 횟수는 정확한 시간 단위가 아닌 시작 실패 보호 장치다. 제품화 때 실제 시간 기준으로 검증한다.
    bool wait_for(uint32_t mask)
    {
        for (uint32_t remaining = 2000000u; remaining != 0; --remaining) {
            if ((PMC->PMC_SR & mask) == mask)
                return true;
        }
        return false;
    }
}

namespace Board {

    bool init()
    {
        // SAM3X8E의 Flash는 총512KB, 256KB 뱅크2개다. EFC0/1은 각 뱅크의 컨트롤러.
        // EEFC_FMR은 Flash Mode Register. FWS(4)는 숫자4를 비트8~11로 옮기는 매크로.
        // 4wait states=기본1+추가4 사이클. 모든 명령이5사이클이라는 의미는 아니다.
        // 현재12MHz에는 여유있는 값이다. 최고 성능용 최적값이 아니며 주파수/전압별 표를 따른다.
        EFC0->EEFC_FMR = EEFC_FMR_FWS(4);
        EFC1->EEFC_FMR = EEFC_FMR_FWS(4);

        // CKGR_MOR: Main Oscillator Register. KEY_PASSWD는 쓰기를 허용하는 고정 키.
        // MOSCXTEN은 외부 수정 발진기를 켜고, MOSCRCEN은 내부RC를 유지한다.
        // MOSCXTST(0xFF)는 사용자가 선택한 시작 대기 카운트255이다. 255ms라는 뜻이 아니며,
        // 제조사가 항상0xFF를 쓰라고 지정한 표준값도 아니다. 하드웨어 slow clock 기준으로 센다.
        constexpr uint32_t oscillator_settings = CKGR_MOR_KEY_PASSWD
            | CKGR_MOR_MOSCXTEN | CKGR_MOR_MOSCRCEN | CKGR_MOR_MOSCXTST(0xFF);
        PMC->CKGR_MOR = oscillator_settings;
        // 먼저 MOSCXTS로 외부 발진기 준비를 기다린다. 준비 전에 소스부터 바꾸지 않는다.
        if (!wait_for(PMC_SR_MOSCXTS))
            return false;

        // MOSCSEL은 발진기 활성화가 아니라 Main Clock의 소스 선택이다.
        // 레지스터 전체에 대입하므로 유지할 비트들도 함께 넣는다.
        PMC->CKGR_MOR = oscillator_settings | CKGR_MOR_MOSCSEL;
        if (!wait_for(PMC_SR_MOSCSELS))
            return false;

        // MCKR: Master Clock Register. 리셋 후 직접 진입하는 시험 FW 경로를 전제한다.
        // 먼저 분주비1을 적용하고 준비를 기다린 다음, Main Clock을 소스로 선택한다.
        // PLL/실행 중 재설정까지 지원하는 범용 클록 드라이버는 아니다.
        PMC->PMC_MCKR = (PMC->PMC_MCKR & ~PMC_MCKR_PRES_Msk) | PMC_MCKR_PRES_CLK_1;
        if (!wait_for(PMC_SR_MCKRDY))
            return false;
        PMC->PMC_MCKR = (PMC->PMC_MCKR & ~PMC_MCKR_CSS_Msk) | PMC_MCKR_CSS_MAIN_CLK;
        if (!wait_for(PMC_SR_MCKRDY))
            return false;

        // SysTick은 Cortex-M 코어의24bit 주기 타이머다. 12,000tick마다1ms 인터럽트.
        // 이 타이머는 통신 timeout만을 위한 것이며 정밀 STEP 예약 타이머가 아니다.
        elapsed_ms = 0;
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
        if (SysTick_Config(kMasterClockHz / 1000u) != 0)
            return false;
        return true;
    }

    uint32_t milliseconds()
    {
        return elapsed_ms;
    }

} // namespace Board

// startup.c가 C 이름으로 이 함수를 참조한다. extern "C"는 C++ 이름 장식을 막는다.
// ISR에서는 카운터 갱신만 수행하며 UART 송신/프레임 해석은 하지 않는다.
extern "C" void SysTick_Handler(void)
{
    ++elapsed_ms;
}

