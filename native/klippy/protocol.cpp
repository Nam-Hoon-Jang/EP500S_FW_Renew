#include "protocol.h"

Protocol::Protocol(Send send, void* send_context, Receive receive, void* receive_context)
    : send_(send), send_context_(send_context), receive_(receive), receive_context_(receive_context)
{
    // 멤버 초기화는 콜론 뒤 초기화 목록과 헤더의 기본값으로 이미 수행된다.
}

uint16_t Protocol::crc16(const uint8_t* data, uint32_t length)
{
    // init=0xFFFF, reflected polynomial=0x8408, xorout=0.
    // XOR는 거듭제곱이 아니다. 입력 각 비트와 현재 최하위 비트에 따라 shift/XOR를 반복한다.
    // 조건 ? 값1 : 값2는 C#과 같은 삼항 연산자다. 최하위 비트가1이면 다항식을 XOR한다.
    uint16_t crc = 0xFFFFu;
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint16_t>((crc >> 1)
                ^ ((crc & 1u) != 0u ? 0x8408u : 0u));
        }
    }
    return crc;
}

bool Protocol::send_payload(const uint8_t* data, uint32_t length)
{
    if (length > kMaximumPayloadSize || (length != 0 && data == nullptr))
        return false;
    uint8_t frame[kMaximumFrameSize] = {};
    const uint32_t total = length + 5u;
    frame[0] = static_cast<uint8_t>(total);
    frame[1] = static_cast<uint8_t>(kSequencePrefix | next_sequence_);
    for (uint32_t i = 0; i < length; ++i)
        frame[2u + i] = data[i];
    // length/sequence/payload만 CRC 대상. CRC2byte와 sync1byte는 계산에서 제외한다.
    const uint32_t crc_position = total - 3u;
    const uint16_t crc = crc16(frame, crc_position);
    frame[crc_position] = static_cast<uint8_t>(crc >> 8);
    frame[crc_position + 1u] = static_cast<uint8_t>(crc & 0xFFu);
    frame[total - 1u] = kFrameSync;
    if (send_ == nullptr || !send_(send_context_, frame, total)) {
        ++statistics_.transmit_errors;
        return false;
    }
    return true;
}

void Protocol::send_ack()
{
    // 빈 payload의 전체 길이는5. ACK/NAK는 별도 메시지 ID가 아니라 같은 빈 프레임이다.
    // 정상 수신은 기대 번호 증가 후, 중복/순서 오류는 증가 없이 보내 구분한다.
    // ACK 송신 실패여도 이미 수락한 번호를 되돌리지 않는다. 재전송이면 중복 실행 없이 다시 ACK.
    (void)send_payload(nullptr, 0);
}

void Protocol::report_corruption()
{
    // 잡음 byte마다 NAK를 보내면 응답이 폭주할 수 있다. 연속 오류 중에는 한 번만 알리고,
    // CRC/prefix가 유효한 프레임을 받으면 다음 오류 보고를 다시 허용한다.
    if (!corruption_reported_) {
        corruption_reported_ = true;
        send_ack();
    }
}

void Protocol::consume(uint32_t length)
{
    // memmove와 같은 앞당김을 작은 루프로 작성. 최대64byte이며 동적 메모리가 없다.
    // 이전 배열 값을0으로 지우지 않아도 count 이후 영역은 읽지 않으므로 의미가 없다.
    for (uint32_t i = length; i < receive_count_; ++i)
        receive_buffer_[i - length] = receive_buffer_[i];
    receive_count_ -= length;
}

void Protocol::recover_boundary()
{
    // 실패한 프레임 내부에 sync가 있을 수도 있다. 첫 sync까지 버리고 그 뒤 byte는 남겨
    // 다음 프레임 후보로 검사한다. 정상 프레임 내부 sync는 이 경로로 오지 않아 허용된다.
    for (uint32_t i = 0; i < receive_count_; ++i) {
        if (receive_buffer_[i] == kFrameSync) {
            consume(i + 1u);
            discard_until_sync_ = false;
            return;
        }
    }
    receive_count_ = 0;
    discard_until_sync_ = true;
}

void Protocol::process_buffer()
{
    while (receive_count_ != 0) {
        // 프로토콜은 프레임 앞 추가 sync도 허용한다. 이 경우 오류 응답 없이 하나 소비한다.
        if (receive_buffer_[0] == kFrameSync) {
            consume(1);
            continue;
        }
        const uint32_t expected_length = receive_buffer_[0];
        if (expected_length < 5u || expected_length > kMaximumFrameSize) {
            ++statistics_.rejected_format;
            report_corruption();
            recover_boundary();
            continue;
        }
        if (receive_count_ < 2u)
            return;
        const uint8_t sequence = receive_buffer_[1];
        // ~는 정수 승격 후 계산되므로8bit로 명시 변환. 하위4bit를 지우고 prefix만 비교한다.
        // kSequenceMask=0000 1111, 반전 후8bit값=1111 0000. &로 상위4bit만 남긴다.
        const uint8_t prefix = static_cast<uint8_t>(
            sequence & static_cast<uint8_t>(~kSequenceMask));
        if (prefix != kSequencePrefix) {
            ++statistics_.rejected_format;
            report_corruption();
            recover_boundary();
            continue;
        }
        if (receive_count_ < expected_length)
            return;
        if (receive_buffer_[expected_length - 1u] != kFrameSync) {
            ++statistics_.rejected_format;
            report_corruption();
            recover_boundary();
            continue;
        }
        const uint32_t crc_position = expected_length - 3u;
        const uint16_t received_crc = static_cast<uint16_t>(
            (static_cast<uint16_t>(receive_buffer_[crc_position]) << 8)
            | receive_buffer_[crc_position + 1u]);
        if (received_crc != crc16(receive_buffer_, crc_position)) {
            ++statistics_.rejected_crc;
            report_corruption();
            recover_boundary();
            continue;
        }

        corruption_reported_ = false;
        const uint8_t number = static_cast<uint8_t>(sequence & kSequenceMask);
        if (number != next_sequence_) {
            ++statistics_.rejected_sequence;
            send_ack();
            consume(expected_length);
            continue;
        }
        // 먼저+1을 계산한 뒤 하위4bit만 유지한다. 15+1=16(0001 0000), &0x0F 결과는0.
        next_sequence_ = static_cast<uint8_t>((next_sequence_ + 1u) & kSequenceMask);
        ++statistics_.accepted;
        // 번호를 증가시킨 다음 응답을 만든다. callback에서 receive_byte를 다시 호출하지 않는다.
        if (receive_ != nullptr)
            receive_(receive_context_, *this, receive_buffer_ + 2u, expected_length - 5u);
        send_ack();
        consume(expected_length);
    }
}

void Protocol::receive_byte(uint8_t value, uint32_t now_ms)
{
    // 새 byte가 도착한 경우에도 이전 부분 프레임의 timeout부터 확인한다.
    poll(now_ms);
    last_byte_ms_ = now_ms;
    if (discard_until_sync_) {
        if (value == kFrameSync)
            discard_until_sync_ = false;
        return;
    }
    if (receive_count_ >= kMaximumFrameSize) {
        ++statistics_.rejected_format;
        report_corruption();
        recover_boundary();
        // 복구 중인 경우 현재 byte도 종료 경계인지 확인 후 버린다. 배열 밖에는 쓰지 않는다.
        if (discard_until_sync_) {
            if (value == kFrameSync)
                discard_until_sync_ = false;
            return;
        }
    }
    receive_buffer_[receive_count_++] = value;
    process_buffer();
}

void Protocol::poll(uint32_t now_ms)
{
    // UART 프레임이 잘려 호스트가 침묵해도 영원히 기다리지 않는다. 1000ms는 프로젝트 정책이며
    // Klipper 프레임 규격이 정한 숫자는 아니다. unsigned 뺄셈으로 millisecond 순환도 처리한다.
    if (receive_count_ != 0
        && static_cast<uint32_t>(now_ms - last_byte_ms_) >= kFrameTimeoutMs) {
        ++statistics_.timeouts;
        receive_count_ = 0;
        discard_until_sync_ = true;
        report_corruption();
    }
}

void Protocol::on_uart_error()
{
    ++statistics_.uart_errors;
    receive_count_ = 0;
    discard_until_sync_ = true;
    report_corruption();
}
