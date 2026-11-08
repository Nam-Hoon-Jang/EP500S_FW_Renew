#pragma once
#include <stdint.h>

// 바이트 프레임 계층. SAM3X 헤더에 의존하지 않아 PC에서도 같은 소스를 시험할 수 있다.
// 프레임 [length][sequence][payload ...][CRC high][CRC low][0x7E], 전체5~64byte.
// 이 클래스는 명령 ID/VLQ/identify를 해석하지 않는다. 프레임 수락과 명령 실행 완료는 별개다.
class Protocol final {
public:
    static constexpr uint32_t kMaximumFrameSize = 64u;
    static constexpr uint32_t kMaximumPayloadSize = 59u;
    static constexpr uint8_t kFrameSync = 0x7Eu;
    static constexpr uint8_t kSequenceMask = 0x0Fu;
    static constexpr uint8_t kSequencePrefix = 0x10u;
    static constexpr uint32_t kFrameTimeoutMs = 1000u;

    // 함수 포인터는 C# delegate처럼 동작을 전달한다. void*는 그 동작이 사용할 객체 주소.
    // 송신 대상은 main에서 Uart로 연결하고 PC 시험에서는 메모리 수집 함수로 연결한다.
    // 함수가 반환하기 전까지 data를 사용해야 하며 주소를 보관하면 안 된다.
    using Send = bool (*)(void* context, const uint8_t* data, uint32_t length);
    // 수락한 payload의 유효기간도 이 callback 호출 중뿐이다. 중복 프레임에는 호출하지 않는다.
    // 다음 단계 명령 처리기를 붙일 실제 연결점이며 nullptr면 프레임 시험 모드다.
    using Receive = void (*)(void* context, Protocol& protocol,
                            const uint8_t* data, uint32_t length);
    struct Statistics {
        uint32_t accepted = 0;
        uint32_t rejected_format = 0;
        uint32_t rejected_crc = 0;
        uint32_t rejected_sequence = 0;
        uint32_t timeouts = 0;
        uint32_t uart_errors = 0;
        uint32_t transmit_errors = 0;
    };

    Protocol(Send send, void* send_context,
             Receive receive = nullptr, void* receive_context = nullptr);
    void receive_byte(uint8_t value, uint32_t now_ms);
    // 데이터가 안 들어올 때도 main에서 호출해야 잘린 프레임의 timeout을 감지한다.
    void poll(uint32_t now_ms);
    // UART에서 바이트 손실/오류가 있었으면 부분 프레임을 버리고 경계를 다시 찾는다.
    void on_uart_error();
    // 응답은 현재 next_sequence를 사용한다. 응답 수에 따라 순서 번호를 증가시키지 않는다.
    bool send_payload(const uint8_t* data, uint32_t length);
    uint8_t next_sequence() const { return next_sequence_; }
    const Statistics& statistics() const { return statistics_; }
    static uint16_t crc16(const uint8_t* data, uint32_t length);

private:
    void process_buffer();
    void consume(uint32_t length);
    void recover_boundary();
    void report_corruption();
    void send_ack();
    Send send_;
    void* send_context_;
    Receive receive_;
    void* receive_context_;
    uint8_t receive_buffer_[kMaximumFrameSize] = {};
    uint32_t receive_count_ = 0;
    uint32_t last_byte_ms_ = 0;
    uint8_t next_sequence_ = 0;
    bool discard_until_sync_ = false;
    bool corruption_reported_ = false;
    Statistics statistics_{};
};
