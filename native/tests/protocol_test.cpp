#include "protocol.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

using Bytes = std::vector<uint8_t>;
struct Harness {
    Bytes output;
    std::vector<Bytes> accepted;
    bool fail_send = false;
    Protocol protocol;
    Harness() : protocol(send, this, receive, this) {}
    static bool send(void* p, const uint8_t* data, uint32_t length) {
        auto& h = *static_cast<Harness*>(p);
        if (h.fail_send) return false;
        h.output.insert(h.output.end(), data, data + length);
        return true;
    }
    static void receive(void* p, Protocol&, const uint8_t* data, uint32_t length) {
        static_cast<Harness*>(p)->accepted.emplace_back(data, data + length);
    }
    void feed(const Bytes& data, uint32_t time = 0) {
        for (uint8_t value : data) protocol.receive_byte(value, time);
    }
    void expect(const Bytes& wanted) {
        assert(output == wanted);
        output.clear();
    }
};

// 시험오라클은실제Protocol::crc16을재사용하지않고별도의바이트계산식을쓴다.
// 알려진결과와기존Klippy형식교차시험도Python도구에서수행한다.
static uint16_t reference_crc(const Bytes& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : data) {
        uint8_t x = static_cast<uint8_t>(b ^ crc);
        x = static_cast<uint8_t>(x ^ (x << 4));
        crc = static_cast<uint16_t>((static_cast<uint16_t>(x) << 8)
            ^ (crc >> 8) ^ (x >> 4) ^ (static_cast<uint16_t>(x) << 3));
    }
    return crc;
}
static Bytes frame(uint8_t number, const Bytes& payload = {}) {
    Bytes data{static_cast<uint8_t>(payload.size() + 5u),
               static_cast<uint8_t>(0x10u | number)};
    data.insert(data.end(), payload.begin(), payload.end());
    const auto crc = reference_crc(data);
    data.push_back(static_cast<uint8_t>(crc >> 8));
    data.push_back(static_cast<uint8_t>(crc));
    data.push_back(0x7E);
    return data;
}

int main() {
    const Bytes known{'1','2','3','4','5','6','7','8','9'};
    assert(Protocol::crc16(known.data(), known.size()) == 0x6F91);
    assert(reference_crc(known) == 0x6F91);
    Harness h;
    h.feed(frame(0, {0x41})); h.expect(frame(1));
    h.feed(frame(0)); h.expect(frame(1)); // 중복은callback재실행없음
    h.feed(frame(2)); h.expect(frame(1)); // 누락도기대값유지
    assert(h.accepted.size() == 1);
    for (unsigned i = 1; i < 16; ++i) {
        h.feed(frame(i)); h.expect(frame((i + 1u) & 15u));
    }
    h.feed(frame(0)); h.expect(frame(1));

    // 최대길이와payload안sync. 연속프레임/쪼개진read여부에무관하게byte로수신한다.
    Bytes maximum(59, 0x7E);
    h.feed(frame(1, maximum)); h.expect(frame(2));
    const auto partial = frame(2, {0x41,0x7E,0x42});
    h.feed(Bytes(partial.begin(), partial.end() - 1)); h.expect({});
    h.feed({partial.back()}); h.expect(frame(3));

    // CRC손상은한번NAK하고기대번호유지. 유효프레임후NAK억제상태해제.
    auto bad = frame(3, {0x41}); bad[bad.size()-2] ^= 1;
    h.feed(bad); h.expect(frame(3));
    h.feed(bad); h.expect({});
    h.feed(frame(3)); h.expect(frame(4));
    assert(h.protocol.statistics().rejected_crc == 2);

    // 잘못된길이와잡음, 경계복구, 추가선행sync무시.
    h.feed({0,0xAA,0xBB,0x7E}); h.expect(frame(4));
    h.feed({0x7E,0x7E}); h.expect({});
    h.feed(frame(4)); h.expect(frame(5));
    auto wrong_prefix = frame(5); wrong_prefix[1]=0x20;
    h.feed(wrong_prefix); h.expect(frame(5));
    h.feed(frame(5)); h.expect(frame(6));

    // CRC/종료오류시버퍼안첫sync뒤byte를살려재파싱. 순서번호는여전히6.
    auto wrong_end = frame(6); wrong_end.back()=0;
    h.feed(wrong_end); h.expect(frame(6));
    h.feed({0xAA,0x7E}); h.expect({});
    h.feed(frame(6)); h.expect(frame(7));

    // 부분프레임timeout, unsigned시각순환, 이후재전송.
    h.feed({10,0x17,0x41}, UINT32_MAX-500u);
    h.protocol.poll(498u); h.expect({});
    h.protocol.poll(499u); h.expect(frame(7));
    assert(h.protocol.statistics().timeouts == 1);
    h.feed({0x7E},500); h.feed(frame(7),501); h.expect(frame(8));
    h.protocol.on_uart_error(); h.expect(frame(8));
    h.protocol.on_uart_error(); h.expect({});
    h.feed({0x7E}); h.feed(frame(8)); h.expect(frame(9));

    // 송신실패후호스트재전송시명령중복실행하지않고ACK만다시보낸다.
    const auto count = h.accepted.size();
    h.fail_send=true; h.feed(frame(9)); h.expect({});
    assert(h.protocol.statistics().transmit_errors == 1);
    h.fail_send=false; h.feed(frame(9)); h.expect(frame(10));
    assert(h.accepted.size() == count + 1);
    assert(!h.protocol.send_payload(nullptr,1));
    assert(!h.protocol.send_payload(maximum.data(),60));
    assert(h.protocol.send_payload(maximum.data(),59));
    h.expect(frame(10,maximum));

    // 고정seed잡음 스트레스. sanitizer가메모리범위/미정의연산을함께검사한다.
    uint32_t seed=0x12345678u;
    for (uint32_t i=0;i<50000;++i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        h.protocol.receive_byte(static_cast<uint8_t>(seed), i*2u);
    }
    h.protocol.poll(200000u);
    h.feed({0x7E},200001u);
    h.output.clear();
    const auto expected = h.protocol.next_sequence();
    h.feed(frame(expected),200002u);
    h.expect(frame((expected+1u)&15u));
    puts("PASS: CRC/sequence/ACK/NAK/bounds/resync/timeout/wrap/callback/TX-failure/noise");
}
