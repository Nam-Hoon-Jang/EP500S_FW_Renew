#pragma once
#include <stdint.h>

// 하나의 SAM3X UART를 관리하는 객체. 상속/가상함수/힙 할당은 필요하지 않다.
// RX는 인터럽트+ring buffer, TX는 시간 제한이 있는 polling이다.
// 이 클래스는 바이트만 다루며 CRC/프레임/ACK는 Protocol이 담당한다.
class DueUart final {
public:
    enum class ReadResult { Empty, Byte, Error };
    struct Statistics {
        uint32_t hardware_errors;
        uint32_t buffer_overflows;
        uint32_t transmit_timeouts;
    };

    // 객체는 main()의 지역변수로 만든다. 전역 동적 생성자 실행을 요구하지 않는다.
    // Board::init()이 성공하고 SysTick이 동작한 다음 호출해야 한다.
    bool init(uint32_t master_clock_hz, uint32_t baud = 250000u);
    ReadResult try_read(uint8_t& value);
    bool write(const uint8_t* data, uint32_t length, uint32_t timeout_ms = 100u);
    // 마지막 stop bit까지 출력 완료가 꼭 필요한 종료/클록 변경 지점에서만 호출.
    bool flush(uint32_t timeout_ms = 100u);
    Statistics statistics() const;
    void on_interrupt();

private:
    static constexpr uint32_t kBufferSize = 256u;
    static_assert((kBufferSize & (kBufferSize - 1u)) == 0, "Ring size must be power of two");
    uint8_t receive_buffer_[kBufferSize] = {};
    volatile uint32_t head_ = 0;
    volatile uint32_t tail_ = 0;
    volatile bool receive_error_ = false;
    volatile uint32_t hardware_errors_ = 0;
    volatile uint32_t buffer_overflows_ = 0;
    uint32_t transmit_timeouts_ = 0;
    bool initialized_ = false;
};

