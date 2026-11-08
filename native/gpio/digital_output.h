#pragma once
#include <stdint.h>

namespace Gpio
{
    class DigitalOutput final
    {
        public:
            // 하나의 객체가 하나의 출력 핀을 관리
            // 실수로 객체를 복사해 같은 핀을 중복 관리하지 않도록 막음.
            // = delete: 이 함수를 사용하는 코드를 컴파일 오류로 처리함.
            DigitalOutput() = default;  // 컴파일러가 기본 생성 동작을 만들어줌.
            
            // 다른 DigitalOutput 객체를, 읽기 전용 참조로 받는 복사 생성자의 사용을 delete로 막음. 아래는 예시.
            // Gpio::DigitalOutput origin;
            // Gpio::DigitalOutput copied = origin; -> 이런 사용을 막기 위함.
            // DigitalOutput 생성자의 생성 시, DigitalOutput 타입을 참조로 받고, 전달 받은 원본을 변경하지 않음.
            DigitalOutput(const DigitalOutput&) = delete; // 이 형식으로 DigitalOutput() 사용 시, 컴파일 오류 발생

            // 복사 대입 금지. 하단에 예시
            // Gpio::DigitalOutput a;
            // Gpio::DigitalOutput b;
            // b = a; -> 이런 사용을 막기 위함.
            // ※ b = a 는 b.operator = (a); 가 되고, 이건 DigitalOutput& operator=(const DigitalOutput&) 와 같은 표현임.
            DigitalOutput& operator = (const DigitalOutput&) = delete; // 이 형식으로 DigitalOutput() 사용 시, 컴파일 오류 발생

            // 핀 설정 함수. 핀/초기값/정지값 입력해서 설정
            bool configure(uint32_t pin, bool initial_value, bool shutdown_value);

            // DigitalOutput ON/OFF 제어
            bool set(bool value);

            // 사전에 설정된 정지값을 출력함.
            // 이후 MCU 정지 처리에서 호출하도록 연결.
            bool apply_shutdown();

            // configure 실행 여부 반환
            bool configured() const{ return configured_;}

            // 정지값 적용하고 객체 설정 정보 리셋. 실제 핀은 정지값 출력 상태로 유지함.
            bool reset();

        private:
            uint32_t pin_ = 0;
            bool configured_ = false;
            bool shutdown_value_ = false;
    };
}