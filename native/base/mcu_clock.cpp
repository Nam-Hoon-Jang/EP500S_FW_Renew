#include "mcu_clock.h"
#include "sam3x8e.h"

/// TC_SR 읽기 
/// ├ overflow -> 상위 카운터 증가
/// └ RA 비교 -> compare_pending에 보관 -> TC0_Handler에서 처리

/// @brief MCU의 기준 시계를 만들고, Timer Event를 유실하지 않도록 관리하는 기능
/// 현재 시각 조회: H/W 32bit counter를 64bit 시각 데이터로 확장.
/// 비교 Event 전달: 예약 시각 도달 Event를 ISR(Interrupt Service Routine, 인터럽트 함수)에서 처리할 함수로 전달.
/// 구조 흐름:
/// init()후 TC0 채널0은 초당 6,000,000회 증가. 0 -> 1 -> 2 -> ... -> 0xFFFFFFFF -> 0 -> ... 반복
/// 이 값은 timer.TC_CV Register에서 읽으며, CPU가 증량하는 것이 아닌, Timer H/W가 별도로 증가시킴.
/// 32bit라 약 11분 56초마다 0으로 돌아오며, 0으로 돌아오는 것을 overflow라고 함.
/// 64bit의 시각은 아래처럼 조합.
/// high 32bit: overflow_count, low 32bit: TC_CV -> (static_cast<uint64_t>(high) << 32) | low
namespace{

    // TC0 채널의 32bit 카운터가 순환한 횟수
    // ISR과 일반 코드에서 공유하므로, volatile로 선언함.
    volatile uint32_t overflow_count = 0;

    // 아직 처리하지 않은 RA 비교 Event가 있는지 저장(기록)하는 변수
    volatile bool compare_pending = false;

    // 비교 Event 발생 시, 호출할 함수의 address 저장(기록)하는 변수
    McuClock::CompareHandler volatile compare_handler = nullptr;

    /// @brief TC_SR을 읽는 공통 경로. 발생한 Event를 수집하는 역할
    /// IRQ가 차단된 문맥 또는 TC0 ISR(Interrupt Service Routine, 인터럽트 처리 함수)에서 호출함
    /// @return 
    uint32_t capture_status()
    {
        const uint32_t status = TC0->TC_CHANNEL[0].TC_SR;

        if((status & TC_SR_COVFS) != 0)
        {
            ++overflow_count;
        }
        // 상태를 읽어, 하드웨어 이벤트가 지워져도 소프트웨어에 보관하여 비교 이벤트를 잃지 않음.
        if((status & TC_SR_CPAS) != 0)
        {
            compare_pending = true;
        }

        return status;
    }
}

namespace McuClock{

    /// @brief Board::init() 성공 후 한 번 호출
    /// TC0 인터럽트 비활성 -> 주변장치에 Clock 공급 -> Counter 정지, 기존 Event 제거 -> S/W 상태 초기화 -> MCK/2의 연속 증가 모드 설정
    /// -> overflow interrupt 허용 -> Counter 시작 -> TC0 interrupt 활성화
    void init()
    {
        // 초기화 중일 때 TC0 interrupt가 실행되지 않게 함
        NVIC_DisableIRQ(TC0_IRQn);

        // TC0 채널0에 주변장치 Clock 공급. ID_TC0는 27이므로 PCER0 Register 사용.
        PMC->PMC_PCER0 = 1u << ID_TC0;

        auto& timer = TC0->TC_CHANNEL[0];

        // CCR: Channel Control Register, IDR: Interrupt Disable Register, CLKDIS: Counter Clock Disable Command
        timer.TC_CCR = TC_CCR_CLKDIS;
        timer.TC_IDR = 0xFFFFFFFFu;

        // SR: Status Register
        // TC_SR은 읽으면 이벤트 상태가 지워진다. 이전 이벤트를 비우고 새 시계를 시작한다.
        (void)timer.TC_SR;
        overflow_count = 0;

        compare_pending = false;
        compare_handler = nullptr;

        // 내부 MCK/2 Clock, waveform의 연속 증가 모드
        // RC 비교 시, 카운터를 초기화하는 UP_RC 모드는 쓰지 않음.
        // 외부 핀 출력은 설정하지 않음.
        timer.TC_CMR = TC_CMR_TCCLKS_TIMER_CLOCK1 | TC_CMR_WAVE | TC_CMR_WAVSEL_UP;

        NVIC_ClearPendingIRQ(TC0_IRQn);

        // 32bit Counter가 순환할 때만 interrupt를 생성함.
        timer.TC_IER = TC_IER_COVFS;

        // Counter Enable 및 Sofware Trigger로 시작
        timer.TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;

        NVIC_EnableIRQ(TC0_IRQn);
    }

    /// @brief 현재 시각 반환.
    /// 기존 IRQ 차단 상태 저장 -> 하위 Counter 읽기 -> capture_status()로 미처리 overflow 반영 -> 상위 값 확보
    /// 비교 Event가 있으면 TC0 IRQ에 처리 요청 -> 기존 IRQ 상태 복원 -> high/low 합쳐서 반환
    /// IRQ를 잠시 차단하는 이유는, high와 low를 읽는 사이에 ISR이 값을 바꾸는 것을 방지하기 위함. Timer H/W 자체는 계속 증가함.
    /// @return 
    uint64_t ticks64()
    {
        // 상위 순환 횟수와 하위 H/W Counter를 하나의 일관된 값으로 읽기 위해 잠시 IRQ 억제.
        // 기존 IRQ 차단 상태는 반환 전에 그대로 복원.
        const uint32_t saved_primask = __get_PRIMASK();
        __disable_irq();

        auto& timer = TC0->TC_CHANNEL[0];

        uint32_t low = timer.TC_CV;
        const uint32_t status = capture_status();

        // H/W는 이미 순환했지만, ISR이 아직 처리하지 못했을 수 있음. 여기서 순환을 반영하고, 이후 하위값을 다시 읽음.
        if((status & TC_SR_COVFS) != 0)
        {
            low = timer.TC_CV;
        }

        const uint32_t high = overflow_count;

        // 일반 함수에서 callback을 직접 실행하지 않고, TC0 interrupt를 pending 상태로 만들어서 ISR이 처리하도록 만듦.
        if(compare_pending && compare_handler != nullptr)
        {
            NVIC_SetPendingIRQ(TC0_IRQn);
        }

        __set_PRIMASK(saved_primask);

        return (static_cast<uint64_t>(high) << 32) | low;
    }

    /// @brief 실행할 내부 처리 함수 등록. 지금 실행하는 것이 아닌, 나중에 실행할 함수를 지정하는 것.
    /// @param handler 
    void set_compare_handler(CompareHandler handler)
    {
        const uint32_t saved_primask = __get_PRIMASK();
        __disable_irq();

        compare_handler = handler;

        __set_PRIMASK(saved_primask);
    }

    /// @brief 이전 비교 Event 제거
    void clear_compare_event()
    {
        const uint32_t saved_primask = __get_PRIMASK();
        __disable_irq();

        // 하드웨어 상태를 수집하면서 overflow는 정상 반영함
        (void)capture_status();

        // 비교 이벤트만 버림
        compare_pending = false;

        // NVIC pending 전체를 지우면, 다른 TC0 이벤트도 놓칠 수 있으므로, 여기서는 지우지 않음.
        __set_PRIMASK(saved_primask);
    }
}

/// @brief 실제 Interrupt 처리 함수
/// @param  
extern "C" void TC0_Handler(void)
{
    (void)capture_status();

    if(compare_pending)
    {
        // handler가 다음 예약을 등록할 수 있도록 먼저 해제함.
        compare_pending = false;

        const auto handler = compare_handler;

        if(handler != nullptr)
        {
            handler();
        }
    }
}