#include "klippy_commands.h"
#include "klippy_codec.h"
#include "klippy_dictionary.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include "mcu_state.h"
#include "mcu_clock.h"

// PC 시험용 시계 대역. 실제 타이머/IRQ는 검증하지 않는다. ARM FW에는 연결되지 않는다.
namespace McuClock {
uint64_t ticks64() { return UINT64_C(0x1234567887654321); }
}

using Bytes=std::vector<uint8_t>;
static Bytes number(int64_t value, bool is_signed=false) {
    uint8_t data[5];
    auto result=is_signed?KlippyCodec::encode_i32(value,data,5):KlippyCodec::encode_u32(value,data,5);
    assert(result.status==KlippyCodec::Status::Ok);
    return Bytes(data,data+result.bytes_used);
}
static Bytes join(Bytes left,const Bytes& right) { left.insert(left.end(),right.begin(),right.end()); return left; }
static Bytes frame(uint8_t seq,const Bytes& payload={}) {
    Bytes result{static_cast<uint8_t>(payload.size()+5),static_cast<uint8_t>(0x10|seq)};
    result=join(result,payload);
    auto crc=Protocol::crc16(result.data(),result.size());
    result.push_back(crc>>8); result.push_back(crc); result.push_back(0x7E);
    return result;
}
static Bytes error(uint32_t code,uint32_t offset,uint32_t id) {
    return join(join(join(number(6),number(code)),number(offset)),number(id));
}
struct Harness {
    Bytes output;
    KlippyCommands commands;
    Protocol protocol;
    Harness():protocol(send,this,KlippyCommands::receive,&commands) {}
    static bool send(void* context,const uint8_t* bytes,uint32_t length) {
        auto& h=*static_cast<Harness*>(context);
        h.output.insert(h.output.end(),bytes,bytes+length);
        return true;
    }
    void send_frame(uint8_t seq,const Bytes& payload,const Bytes& expected) {
        output.clear();
        for (auto byte:frame(seq,payload)) protocol.receive_byte(byte,0);
        assert(output==expected);
    }
};

int main() 
{
    {
        Harness clock;
        const auto low = number(0x87654321u);
        clock.send_frame(0, number(7), join(frame(1, join(number(8), low)), frame(1)));
        const auto uptime = join(join(number(10), number(0x12345678u)), low);
        clock.send_frame(1, number(9), join(frame(2, uptime), frame(2)));
    }
     Harness config;

    // ID12 + configured0 + crc0 + shutdown0 + move_count0
    const Bytes response{12, 0, 0, 0, 0};

    config.send_frame(
        0,
        number(11),
        join(frame(1, response), frame(1)));

    // 중복 요청은 기존 정책대로 ACK만 반환한다.
    config.send_frame(0, number(11), frame(1));
    assert(config.commands.statistics().executed == 1);

    // 사전을 실제 프레임 경로로 복원한다. 최대 count에서도 프레임 한도를 지킨다.
    for (uint32_t requested : {1u, 40u, 255u}) {
        Harness identify;
        uint32_t offset = 0;
        uint8_t seq = 0;
        while (offset < sizeof(KlippyDictionary::data)) {
            const auto request = join(join(number(1), number(offset)), number(requested));
            auto response = join(number(0), number(offset));
            uint32_t count = requested;
            if (count > sizeof(KlippyDictionary::data) - offset)
                count = sizeof(KlippyDictionary::data) - offset;
            if (count > Protocol::kMaximumPayloadSize - response.size() - 1)
                count = Protocol::kMaximumPayloadSize - response.size() - 1;
            response.push_back(count);
            response.insert(response.end(), KlippyDictionary::data + offset, KlippyDictionary::data + offset + count);
            const auto next = (seq + 1) & 15;
            identify.send_frame(seq, request, join(frame(next, response), frame(next)));
            const auto executed = identify.commands.statistics().executed;
            identify.send_frame(seq, request, frame(next));
            assert(identify.commands.statistics().executed == executed);
            seq = next;
            offset += count;
        }
        for (uint32_t end : {offset, offset + 1, UINT32_MAX}) {
            auto response = join(number(0), number(end)); response.push_back(0);
            identify.send_frame(seq, join(join(number(1),number(end)),number(40)),
                                join(frame((seq+1)&15,response),frame((seq+1)&15)));
            seq = (seq+1)&15;
        }
        identify.send_frame(seq,{1,0,0},join(frame((seq+1)&15,{0,0,0}),frame((seq+1)&15)));
        seq = (seq+1)&15;
        identify.send_frame(seq,join(Bytes{1,0},number(256)),
                            join(frame((seq+1)&15,error(6,2,1)),frame((seq+1)&15)));
        seq = (seq+1)&15;
        identify.send_frame(seq,{1,0},join(frame((seq+1)&15,error(2,2,1)),frame((seq+1)&15)));
    }
    Harness h;
    const auto ping=join(number(2),number(UINT32_MAX));
    const auto pong=join(number(4),number(UINT32_MAX));
    h.send_frame(0,ping,join(frame(1,pong),frame(1)));
    assert(h.commands.statistics().executed==1);
    // 중복 프레임은 처리 함수를 다시 실행하지 않는다.
    h.send_frame(0,ping,frame(1));
    assert(h.commands.statistics().executed==1);
    const auto sum=join(join(number(3),number(-123,true)),number(23,true));
    const auto sum_result=join(number(5),number(-100,true));
    h.send_frame(1,sum,join(frame(2,sum_result),frame(2)));
    // 하나의 payload에 여러 명령을 이어 넣을 수 있다.
    h.send_frame(2,join(ping,sum),join(join(frame(3,pong),frame(3,sum_result)),frame(3)));
    assert(h.commands.statistics().executed==4);
    h.send_frame(3,number(99),join(frame(4,error(1,0,99)),frame(4)));
    h.send_frame(4,{0x80},join(frame(5,error(2,0,0)),frame(5)));
    h.send_frame(5,number(2),join(frame(6,error(2,1,2)),frame(6)));
    h.send_frame(6,{0x80,0x80,0x80,0x80,0x80},join(frame(7,error(3,0,0)),frame(7)));
    h.send_frame(7,{0x90,0x80,0x80,0x80,0},join(frame(8,error(4,0,0)),frame(8)));
    // 뒤의 명령이 잘못됐으면 앞의 정상 명령도 실행하지 않는다.
    h.send_frame(8,join(ping,number(99)),join(frame(9,error(1,ping.size(),99)),frame(9)));
    assert(h.commands.statistics().executed==4);
    auto overflow=join(join(number(3),number(INT32_MAX,true)),number(1,true));
    auto offset=number(3).size()+number(INT32_MAX,true).size();
    h.send_frame(9,overflow,join(frame(10,error(5,offset,3)),frame(10)));
    h.send_frame(10,{},frame(11));
    assert(h.commands.statistics().executed==4);
    assert(h.commands.statistics().rejected_payloads==7);
    h.output.clear();
    h.commands.process(h.protocol,nullptr,1);
    assert(h.output==frame(11,error(6,0,0)));
    {
    Harness allocation;

    // OID 0~2 예약. 별도 응답 없이 ACK만 반환한다.
    allocation.send_frame(0, Bytes{13, 3}, frame(1));

    assert(McuState::oid_in_range(0));
    assert(McuState::oid_in_range(2));
    assert(!McuState::oid_in_range(3));
    assert(!McuState::status().shutdown);
    assert(!McuState::status().configured);

    // 동일 transport 프레임 재전송은 실행되지 않는다.
    allocation.send_frame(0, Bytes{13, 3}, frame(1));
    assert(allocation.commands.statistics().executed == 1);
    assert(!McuState::status().shutdown);

    // 새 sequence로 같은 명령을 다시 보내면 중복 예약이다.
    const auto stopped = join(join(number(17), number(0x87654321u)), number(2));
    allocation.send_frame(1, Bytes{13, 3}, join(frame(2, stopped), frame(2)));
    assert(McuState::status().shutdown);

    // 거부돼도 기존 번호 범위는 유지한다.
    assert(McuState::oid_in_range(2));
    assert(!McuState::oid_in_range(3));
}
    {
        Harness ready;
        ready.send_frame(0, {16}, frame(1)); // 정지 후 설정 초기화
        ready.send_frame(1, {13, 0}, frame(2));
        const uint32_t crc = 0xFEDCBA98u;
        ready.send_frame(2, join(number(14), number(crc)), frame(3));
        ready.send_frame(2, join(number(14), number(crc)), frame(3)); // 재전송은 재확정하지 않는다.
        auto config = join(join(number(12), number(1)), number(crc));
        config = join(config, Bytes{0, 8}); // 실제 GPIO 예약 큐 용량
        ready.send_frame(3, {11}, join(frame(4, config), frame(4)));
        assert(McuState::status().configured);
        // get_config 중복 transport는 응답을 재실행하지 않는다.
        ready.send_frame(3, {11}, frame(4));
        const auto stop = join(join(number(17), number(0x87654321u)), number(1));
        ready.send_frame(4, {15}, join(frame(5, stop), frame(5)));
        const auto already = join(join(number(18), number(0x87654321u)), number(1));
        ready.send_frame(5, {2, 3}, join(frame(6, already), frame(6)));
        ready.send_frame(6, {16}, frame(7));
        assert(!McuState::status().configured && !McuState::status().shutdown);
        // 예약 전 finalize는 거부한다.
        const auto bad = join(join(number(17), number(0x87654321u)), number(2));
        ready.send_frame(7, {14, 0}, join(frame(8, bad), frame(8)));
        ready.send_frame(8, {16}, frame(9));
        ready.send_frame(9, {13, 1}, frame(10));
        const auto unsupported = join(join(number(17), number(0x87654321u)), number(3));
        ready.send_frame(10, {14, 0}, join(frame(11, unsupported), frame(11)));
        assert(!McuState::status().configured);
        assert(McuState::status().stop_reason == McuState::StopReason::UnsupportedDevice);
        ready.send_frame(11, {16}, frame(12));
        ready.send_frame(12, {13, 0}, frame(13));
        ready.send_frame(13, {14, 0}, frame(14)); // CRC=0도 유효한 설정값
        const auto invalid = join(join(number(17), number(0x87654321u)), number(4));
        ready.send_frame(14, number(99), join(join(frame(15, error(1, 0, 99)), frame(15, invalid)), frame(15)));
        assert(McuState::status().shutdown);
        ready.send_frame(15, {16}, frame(0));
        // 정상 상태의 config_reset은 허용하지 않는다.
        ready.send_frame(0, {16}, join(frame(1, bad), frame(1)));
        assert(McuState::config_reset());
        assert(McuState::allocate_oids(0));
        assert(McuState::finalize_config(0));
        assert(!McuState::finalize_config(1)); // 새 명령으로 설정 재확정은 금지
        assert(McuState::status().config_crc == 0);
    }
    puts("PASS: commands/clock/config/finalize/shutdown/reset/OID/duplicate/preflight");
}
