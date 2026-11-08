#include "gpio.h"
#include "sam3x8e.h"

namespace
{
    constexpr uint32_t led_mask = 1u << 27;
    bool led_configured = false;
}

namespace Gpio
{
    bool configure_output(uint32_t pin, bool initial_value)
    {
        // 지원하는 핀인지 먼저 검사. 잘못된 번호로 주변장치 Register를 변경하지 않기 위함.
        if(pin != kDueLedPin)
        {
            return false;
        }

        // PIOB 주변장치에 Clock 공급
        PMC->PMC_PCER0 = 1u << ID_PIOB; // PCER0: Periphral Clock Enable Register. 주변장치 Clock 허용 Register

        // 출력 방향 활성 전에 출력 Latch에 initial_value를 넣는다.
        // 설정 순간에 원하지 않는 값이 출력되는 것을 줄이기 위함.
        if(initial_value)
        {
            PIOB->PIO_SODR = led_mask; // SODR: Set Output Data Register
        }
        else
        {
            PIOB->PIO_CODR = led_mask; // CODR: Clear Output Data Register
        }

        // 내부 pull-up을 끄고, 일반 push-pull 출력으로 사용한다.
        // Multi-drive는 open-drain 동작을 위한 기능이며, 여기서는 OFF
        PIOB->PIO_PUDR = led_mask;  // PUDR: Pull-Up Disable Register.
        PIOB->PIO_MDDR = led_mask;  // MDDR: Multi-Drive Disable Register.

        // 해당 pin의 제어권을 PIO에 넘기고, 출력 방향을 활성한다.
        PIOB->PIO_PER = led_mask;   // PER: PIO Enable Register
        PIOB->PIO_OER = led_mask;   // OER: Output Enable Register

        led_configured = true;
        return true;
    }

    bool write(uint32_t pin, bool value)
    {
        // 잘못된 핀 설정 검사 통과 못했으면 write 취소
        if(pin != kDueLedPin || !led_configured)
        {
            return false;
        }

        // SODR/CODR에 led_mask를 직접 write하므로, 다른 pin 값은 변경하지 않는다.
        // GPIO 핀은 PA 포트, PB 포트가 있고, 각 포트마다 32bit Register가 있음.
        // SODR로 포트의 핀 전원을 켜고, CODR로 포트의 핀 전원을 끔.
        // PA포트의 5번 핀 전원을 켜려면, PIOA->PIO_SODR = 1u << 5; 를 입력해서, 
        // 0000 0000 0000 0000 0000 0000 0000 0000 -> 0000 0000 0000 0000 0000 0000 0001 0000 이렇게 바꿔서 입력하는 것. 오른쪽부터 1번 pin 순서.
        // Register에 |= 연산으로 read-edit-write 할 필요 없음.
        if(value)
        {
            PIOB->PIO_SODR = led_mask;
        }
        else
        {
            PIOB->PIO_CODR = led_mask;
        }

        return true;
    }
}