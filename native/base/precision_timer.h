#pragma once
#include <stdint.h>

/// @brief Timer는 다음 실행 1건만 예약.
/// 여러 건의 순서 정렬 및 저장은 상위 Queue가 담당.
/// 예약 시각은 절대 clock 값을 기준으로 함. 
/// callback은 ISR안에서 실행되므로, "UART 송신/대기/동적 메모리 할당"을 하지 않음
/// callback에서는 짧게 pin을 변경하거나, 다음 실행 예약 정도만 처리.
namespace PrecisionTimer
{
    // 예약 시각에 ISR 안에서 호출하는 함수
    // context는 호출 대상이 필요한 객체의 주소 등을 전달한다.
    // (*) 표현은, "함수 포인터"로 쓰겠다는 뜻. 
    // void(void* context): void포인터(void*)인 'context'를 인수로 받는, void형 반환값을 갖는 "함수".
    // void (*)(void* context): void포인터(void*)인 'context'를 인수로 받는, void형 반환값을 갖는 "함수 포인터".
    using Callback = void (*)(void* context);

    // McuClock::init() 이후 한 번 호출
    // 기존 시계 카운터를 초기화하거나 주파수를 변경하지 않음
    void init();

    // 절대 시각 기준으로 한 건을 예약
    // clock은 McuClock::ticks64()의 하위 32bit 와 같은 단위
    // 성공 시 True, False는 초기화 전/이미 예약 존재/callback이 nullptr/예악 시각이 너무 가까움/예약 시각이 허용 밤위 밖인 경우.
    bool schedule(uint32_t clock, Callback callback, void* context);

    // 대기 중인 예약을 취소. 이미 실행 중인 callback을 되돌리지는 않음.
    void cancel();

    // 아직 실행하지 않은 예약이 있는지 확인.
    bool pending();
}