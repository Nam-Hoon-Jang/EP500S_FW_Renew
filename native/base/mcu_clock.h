#pragma once
#include <stdint.h>
#include "board.h"

// Klippy 명령 형식과 무관한 MCU 공통 시계.
/// TC: Timer Counter, MCU 안의 H/W Timer. SAM3X8E MCU에는 TC0, TC1, TC2로 3개의 블록이 있고, 각 블록마다 3개의 채널이 있음.
// TC0 채널0은 이 모듈이 전용으로 사용함.
namespace McuClock{

    // TIMER_CLOCK1 = Master Clock / 2.
    // 현재 Master Clock이 12MHz 이므로 초당 6,000,000 틱
    constexpr uint32_t kFrequencyHz = Board::kMasterClockHz / 2u;

    /// @brief MCU Clock 초기화 함수. Board::init() 이후 1회만 호출하며, 동작 중 호출되면 시각이 0으로 돌아가므로 금지.
    void init();

    /// @brief 초기화 이후 경과한 H/W 의 tick
    /// kRequencyHz 기준의 tick이며, ms나 us가 아님. 일반 실행 문맥에서 사용하고, ISR에서는 호출X
    /// @return 
    uint64_t ticks64();

    // TC0 비교 이벤트를 처리할 내부 함수. 실제 예약 callback/context 관리는 PrecisionTimer가 담당.
    using CompareHandler = void (*)();

    void set_compare_handler(CompareHandler handler);

    // 비교 이벤트만 비움. overflow 정보는 유지. PrecisionTimer의 예약 등록/취소에서 사용
    void clear_compare_event();
    
}