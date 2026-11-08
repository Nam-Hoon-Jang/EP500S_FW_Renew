/*
 * startup.c의 Reset_Handler가 메모리를 초기화한 뒤 main()을 호출한다.
 * main은 보드/UART 초기화와 데이터 전달만 담당한다.
 * UART 레지스터는 uart.cpp, 클록/시간은 board.cpp, 프레임 규칙은 protocol.cpp에서 관리한다.
 * 장치 없는 Klippy Ready 기반 시험용이다. 모터/히터/예약 실행은 아직 지원하지 않는다.
 */
#include "Configuration.h"
#include "base/board.h"
#include "base/uart.h"
#include "base/mcu_clock.h"
#include "base/oid_registry.h"
#include "gpio/gpio.h"
#include "gpio/digital_output.h"

#if HOST_PROTOCOL == HOST_PROTOCOL_KLIPPY
#include "klippy/protocol.h"
#include "klippy/klippy_commands.h"
#endif

namespace {
    // Protocol은 특정 MCU/UART를 몰라도 된다. callback이 송신 대상을 연결한다.
    // context에는 main의 DueUart 객체 주소가 들어온다. static_cast로 원래 타입을 복원한다.
    // 함수 포인터 callback은 C# delegate와 용도가 비슷하지만 객체 주소를 따로 전달한다.
    bool send_bytes(void* context, const uint8_t* data, uint32_t length)
    {
        return static_cast<DueUart*>(context)->write(data, length);
    }
}

int main()
{
    // 클록이 준비되기 전에 UART 분주를 설정하면 통신 속도가 달라진다. 실패 시 외부 출력 없이 멈춘다.
    if (!Board::init())
    {
        for (;;) {}
    }

    Gpio::DigitalOutput led;

    // MCU Clock 초기화.
    McuClock::init();

    // 지역 객체의 생성자는 main 실행 중에 호출된다. startup에서 전역 생성자 루틴은 필요 없다.
    DueUart uart;
    if (!uart.init(Board::kMasterClockHz))
        for (;;) {}
    // 수락한 payload를 명령 계층으로 연결한다. callback은 프레임 검증/순서 확인 뒤에만 호출된다.
    #if HOST_PROTOCOL == HOST_PROTOCOL_KLIPPY
        KlippyCommands commands;
        Protocol protocol(send_bytes, &uart, KlippyCommands::receive, &commands);
    #endif

    while (true) {
        // RX ISR이 ring에 모은 byte를 main에서 빠르게 꺼낸다. ISR에서 CRC/ACK를 만들지 않는다.
        uint8_t received = 0;
        const DueUart::ReadResult result = uart.try_read(received);
        if (result == DueUart::ReadResult::Byte)
            protocol.receive_byte(received, Board::milliseconds());
        else if (result == DueUart::ReadResult::Error)
            protocol.on_uart_error();

        // 수신 데이터가 없어도 부분 프레임 timeout이 진행되게 매 반복 호출한다.
        protocol.poll(Board::milliseconds());
        commands.poll(protocol);
    }
}


