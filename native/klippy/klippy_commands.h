/*
 * Klippy 호환 명령 계층의 첫 단계: VLQ 명령ID → 인수 → 처리 함수 → 응답.
 * identify·진단·시각·설정 확정·정지/복구를 지원한다. 실제 장치 제어는 아직 없다.
 * 아래2번 이후 번호는 이 프로젝트의 사전 ID다. 공식 Klipper에 고정된 번호가 아니다.
 * identify_response=0/identify=1만 프로토콜의 고정 ID다.
 * 자체 호스트로 전환할 때는 명령 체계와 인코딩 규칙을 별도로 설계·확정해야 한다.
 */
#pragma once
#include <stdint.h>
#include "protocol.h"

namespace KlippyCommandIds {
constexpr uint32_t kIdentifyResponse = 0;
constexpr uint32_t kIdentify = 1;
constexpr uint32_t kDebugPing = 2;         // debug_ping value=%u
constexpr uint32_t kDebugSum = 3;          // debug_sum a=%i b=%i
constexpr uint32_t kDebugPong = 4;         // debug_pong value=%u
constexpr uint32_t kDebugSumResult = 5;    // debug_sum_result value=%i
constexpr uint32_t kDebugError = 6;        // debug_command_error code=%c offset=%u command_id=%u
constexpr uint32_t kGetClock = 7;          // get_clock
constexpr uint32_t kClock = 8;             // clock clock=%u
constexpr uint32_t kGetUpTime = 9;         // get_uptime
constexpr uint32_t kUpTime = 10;           // uptime high=%u clock=%u
constexpr uint32_t kGetConfig = 11;        // get_config
constexpr uint32_t kConfig = 12;           // config response
constexpr uint32_t kAllocateOids =13;      // allocate_oids count=%c
constexpr uint32_t kFinalizeConfig = 14;
constexpr uint32_t kEmergencyStop = 15;
constexpr uint32_t kConfigReset = 16;
constexpr uint32_t kShutdown = 17;
constexpr uint32_t kIsShutdown = 18;
constexpr uint32_t kConfigDigitalOut = 19;
constexpr uint32_t kUpdateDigitalOut = 20;
constexpr uint32_t kQueueDigitalOut = 21;
}

class KlippyCommands final {
public:
    enum class Error : uint8_t {
        None = 0, UnknownCommand = 1, TruncatedInteger = 2,
        IntegerTooLong = 3, IntegerOverflow = 4, ArithmeticOverflow = 5,
        InvalidArgument = 6
    };
    struct Statistics {
        uint32_t executed = 0;
        uint32_t rejected_payloads = 0;
        uint32_t response_failures = 0;
    };
    // Protocol::Receive와 같은 형식의 static 함수다. this는 자동 전달되지 않으므로
    // context로 받은 객체 주소를 복원한 뒤 일반 멤버 함수 process()를 호출한다.
    static void receive(void* context, Protocol& protocol, const uint8_t* payload, uint32_t length);
    void process(Protocol& protocol, const uint8_t* payload, uint32_t length);
    const Statistics& statistics() const { return statistics_; }
    void poll(Protocol& protocol);

private:
    enum class ArgumentType { Unsigned32, Signed32, Byte, Boolean };
    struct Arguments { int64_t values[5] = {}; };
    // 함수 포인터: 이 구조체에 저장한 handler가 명령별 실제 동작을 정한다.
    // 여기서는 객체별 상태에 직접 접근할 필요 없는 static 처리 함수로 연결한다.
    using Handler = bool (*)(Protocol&, const Arguments&);
    struct Definition {
        uint32_t id;
        uint8_t argument_count;
        ArgumentType types[5];
        Handler handler;
    };
    struct Parsed {
        const Definition* definition = nullptr;
        Arguments arguments;
    };
    struct Failure {
        Error code = Error::None;
        uint32_t offset = 0;    // payload 맨 앞 기준 byte 위치. 원래 프레임 기준 위치가 아니다.
        uint32_t command_id = 0; // ID자체를 읽지 못했으면0. 실제 명령으로 실행하지 않는다.
    };
    static const Definition* find(uint32_t id);
    static bool parse_one(const uint8_t* payload, uint32_t length,
                          uint32_t& cursor, Parsed& parsed, Failure& failure);
    static bool identify(Protocol& protocol, const Arguments& arguments);
    static bool debug_ping(Protocol& protocol, const Arguments& arguments);
    static bool debug_sum(Protocol& protocol, const Arguments& arguments);
    void send_error(Protocol& protocol, const Failure& failure);
    Statistics statistics_{};
    static bool get_clock(Protocol& protocol, const Arguments& arguments);
    static bool get_uptime(Protocol& protocol, const Arguments& arguments);
    static bool get_config(Protocol& protocol, const Arguments& arguments);
    static bool allocate_oids(Protocol& protocol, const Arguments& arguments);
    static bool finalize_config(Protocol& protocol, const Arguments& arguments);
    static bool emergency_stop(Protocol& protocol, const Arguments& arguments);
    static bool config_reset(Protocol& protocol, const Arguments& arguments);
    static bool send_shutdown(Protocol& protocol, bool already_shutdown);
    static bool config_digital_out(Protocol& protocol, const Arguments& arguments);
    static bool update_digital_out(Protocol& protocol, const Arguments& arguments);
    static bool queue_digital_out(Protocol& protocol, const Arguments& arguments);
};
