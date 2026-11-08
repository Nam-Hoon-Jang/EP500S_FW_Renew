#pragma once
#include <stdint.h>

//////////////////////////////////////////////////////////////////////////////////////
//  이 FW에서 사용하는 핀 번호 정의 규칙(SAM3X8E MCU 기준): 포트 번호 x 32 + bit 번호   //
//  PA=0, PB=1 로 계산. ex) PB27 => 1x32 + 27 = 59                                   //
//////////////////////////////////////////////////////////////////////////////////////

/// @brief Pin 제어하는 코드
namespace Gpio
{
    // Arduino DUE 의 D13 핀으로, LED 핀.
    constexpr uint32_t kDueLedPin = 59u;

    /// @brief 입력한 pin을 출력으로 설정하고, 초기값을 적용
    /// @param pin 설정하려는 pin 번호
    /// @param initial_value 적용하려는 초기값
    /// @return 설정 성공 여부
    bool configure_output(uint32_t pin, bool initial_value);

    /// @brief 출력으로 설정한 pin의 값을 변경한다.
    /// @param pin 설정하려는 pin 번호
    /// @param value 적용하려는 값. true=HIGH=1, false=LOW=0
    /// @return 설정 성공 여부
    bool write(uint32_t pin, bool value);
}