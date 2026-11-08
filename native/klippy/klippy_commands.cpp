/*
 * Klippy 전용 VLQ를 사용한 진단 명령 디스패처.
 * 명령 ID는 payload에서 읽는다. 프레임 length/sequence/CRC/sync를 다시 해석하지 않는다.
 * 장치 없는 Ready 시험에 필요한 시각/설정 확정/정지/설정 초기화를 지원한다.
 * 모터/히터 등 실제 장치 설정과 예약 실행은 아직 없다.
 */
#include "klippy_commands.h"
#include "klippy_codec.h"
#include "klippy_dictionary.h"
#include "mcu_clock.h"
#include "mcu_state.h"

namespace {
// Writer는 한 응답의payload를 만드는 파일 내부 보조 타입이다. 객체별 UART를 소유하지 않는다.
// identify의 동적 byte 배열까지 한 프레임 payload 안에서 구성한다.
struct Writer {
    uint8_t data[Protocol::kMaximumPayloadSize] = {};
    uint32_t length = 0;
    bool append_u32(uint32_t value) {
        const auto result = KlippyCodec::encode_u32(value, data + length, sizeof(data) - length);
        if (result.status != KlippyCodec::Status::Ok)
            return false;
        length += result.bytes_used;
        return true;
    }
    bool append_i32(int32_t value) {
        const auto result = KlippyCodec::encode_i32(value, data + length, sizeof(data) - length);
        if (result.status != KlippyCodec::Status::Ok)
            return false;
        length += result.bytes_used;
        return true;
    }
};

KlippyCommands::Error codec_error(KlippyCodec::Status status)
{
    switch (status) {
    case KlippyCodec::Status::NeedMoreData: return KlippyCommands::Error::TruncatedInteger;
    case KlippyCodec::Status::TooLong: return KlippyCommands::Error::IntegerTooLong;
    case KlippyCodec::Status::Overflow: return KlippyCommands::Error::IntegerOverflow;
    default: return KlippyCommands::Error::InvalidArgument;
    }
}
}

const KlippyCommands::Definition* KlippyCommands::find(uint32_t id)
{
    // constexpr 표의 각 행이 ID/인수 타입/처리 함수를 하나로 묶는다.
    static constexpr Definition definitions[] = {
        {KlippyCommandIds::kIdentify, 2, {ArgumentType::Unsigned32, ArgumentType::Byte}, identify},
        {KlippyCommandIds::kDebugPing, 1, {ArgumentType::Unsigned32, ArgumentType::Unsigned32}, debug_ping},
        {KlippyCommandIds::kDebugSum, 2, {ArgumentType::Signed32, ArgumentType::Signed32}, debug_sum},
        {KlippyCommandIds::kGetClock, 0, {}, get_clock},
        {KlippyCommandIds::kGetUpTime, 0, {}, get_uptime},
        {KlippyCommandIds::kGetConfig, 0, {}, get_config},
        {KlippyCommandIds::kAllocateOids, 1, {ArgumentType::Byte}, allocate_oids},
        {KlippyCommandIds::kFinalizeConfig, 1, {ArgumentType::Unsigned32}, finalize_config},
        {KlippyCommandIds::kEmergencyStop, 0, {}, emergency_stop},
        {KlippyCommandIds::kConfigReset, 0, {}, config_reset},
        {KlippyCommandIds::kConfigDigitalOut, 5, 
            {
                ArgumentType::Byte,
                ArgumentType::Unsigned32,
                ArgumentType::Boolean,
                ArgumentType::Boolean,
                ArgumentType::Unsigned32
            },
            config_digital_out
        },
        {KlippyCommandIds::kUpdateDigitalOut, 2, 
            {
                ArgumentType::Byte,     // oid
                ArgumentType::Boolean   // value
            },
            update_digital_out
        },
        {KlippyCommandIds::kQueueDigitalOut, 3, 
            {
                ArgumentType::Byte,         // oid
                ArgumentType::Unsigned32,   // clock
                ArgumentType::Boolean       // on_ticks: 현재 디지털 출력은 0/1만 지원
            },
            queue_digital_out
        },
    };
    for (const Definition& definition : definitions) {
        if (definition.id == id)
            return &definition;
    }
    return nullptr;
}

bool KlippyCommands::parse_one(const uint8_t* payload, uint32_t length,
                              uint32_t& cursor, Parsed& parsed, Failure& failure)
{
    // cursor는 다음 읽기 위치. 성공한 정수의 bytes_used만큼만 진행한다.
    // 같은 프레임에는 [ID][인수들][다음ID][다음인수들]처럼 여러 명령이 올 수 있다.
    // 인수 개수는 wire에 별도 기재되지 않고 명령 정의에서 알아낸다.
    uint32_t id = 0;
    failure.offset = cursor;
    auto result = KlippyCodec::decode_u32(payload + cursor, length - cursor, id);
    if (result.status != KlippyCodec::Status::Ok) {
        failure.code = codec_error(result.status);
        return false;
    }
    cursor += result.bytes_used;
    failure.command_id = id;
    parsed.definition = find(id);
    if (parsed.definition == nullptr) {
        // 모르는ID이면 인수개수도 알수없다. 추측해서다음명령을찾지않고 payload처리를중단한다.
        failure.code = Error::UnknownCommand;
        return false;
    }
    for (uint32_t i = 0; i < parsed.definition->argument_count; ++i) {
        failure.offset = cursor;
        if (parsed.definition->types[i] != ArgumentType::Signed32) {
            uint32_t value = 0;
            result = KlippyCodec::decode_u32(payload + cursor, length - cursor, value);
            parsed.arguments.values[i] = value;
        } else {
            int32_t value = 0;
            result = KlippyCodec::decode_i32(payload + cursor, length - cursor, value);
            parsed.arguments.values[i] = value;
        }
        if (result.status != KlippyCodec::Status::Ok) {
            failure.code = codec_error(result.status);
            return false;
        }
        cursor += result.bytes_used;
        // %c도 VLQ 정수로 전달되지만 값의 유효 범위는 unsigned8이다.
        if (parsed.definition->types[i] == ArgumentType::Byte && parsed.arguments.values[i] > 255) {
            failure.code = Error::InvalidArgument;
            return false;
        }

        if(parsed.definition->types[i] == ArgumentType::Boolean && parsed.arguments.values[i] > 255)
        {
            failure.code = Error::InvalidArgument;
            return false;
        }
    }
    if (id == KlippyCommandIds::kDebugSum) {
        // signed32끼리 직접더하면 overflow에서C++미정의동작이될수있다.
        // 먼저64bit로계산한뒤 응답의int32범위에들어가는지검증한다.
        const int64_t sum = parsed.arguments.values[0] + parsed.arguments.values[1];
        if (sum < INT32_MIN || sum > INT32_MAX) {
            failure.code = Error::ArithmeticOverflow;
            return false;
        }
    }
    return true;
}

bool KlippyCommands::identify(Protocol& protocol, const Arguments& arguments)
{
    const auto offset = static_cast<uint32_t>(arguments.values[0]);
    auto count = static_cast<uint32_t>(arguments.values[1]);
    Writer response;
    if (!response.append_u32(KlippyCommandIds::kIdentifyResponse) || !response.append_u32(offset))
        return false;
    // offset을 먼저 검사하여 범위 밖 포인터 생성/unsigned 뺄셈 underflow를 막는다.
    const uint32_t remaining = offset < sizeof(KlippyDictionary::data)
        ? sizeof(KlippyDictionary::data) - offset : 0;
    if (count > remaining)
        count = remaining;
    // %.*s는 VLQ 길이가 아니라 raw 1byte 길이 + 원본 bytes다.
    // offset의 VLQ 길이에 따라 달라지는 남은 payload 공간을 계산한다.
    const uint32_t capacity = sizeof(response.data) - response.length - 1;
    if (count > capacity)
        count = capacity;
    response.data[response.length++] = static_cast<uint8_t>(count);
    for (uint32_t i = 0; i < count; ++i)
        response.data[response.length++] = KlippyDictionary::data[offset + i];
    // 끝/범위 밖 offset 또는 count=0이면 빈 data. 요청 offset은 그대로 반환한다.
    return protocol.send_payload(response.data, response.length);
}

bool KlippyCommands::debug_ping(Protocol& protocol, const Arguments& arguments)
{
    // 받은32bit값을그대로반환한다. 문자열로그를protocol UART에섞지않고VLQ응답으로보낸다.
    Writer response;
    return response.append_u32(KlippyCommandIds::kDebugPong)
        && response.append_u32(static_cast<uint32_t>(arguments.values[0]))
        && protocol.send_payload(response.data, response.length);
}

bool KlippyCommands::debug_sum(Protocol& protocol, const Arguments& arguments)
{
    Writer response;
    const auto sum = static_cast<int32_t>(arguments.values[0] + arguments.values[1]);
    return response.append_u32(KlippyCommandIds::kDebugSumResult)
        && response.append_i32(sum)
        && protocol.send_payload(response.data, response.length);
}

void KlippyCommands::send_error(Protocol& protocol, const Failure& failure)
{
    // 이 오류응답은프로젝트진단용이다. 사전공개전의Klippy가자동으로알아듣는표준응답이아니다.
    Writer response;
    if (!(response.append_u32(KlippyCommandIds::kDebugError)
        && response.append_u32(static_cast<uint32_t>(failure.code))
        && response.append_u32(failure.offset)
        && response.append_u32(failure.command_id)
        && protocol.send_payload(response.data, response.length)))
        ++statistics_.response_failures;
    // 초기 진단 모드에서는 오류 시험 후 복구할 수 있다. 설정 확정 후에는
    // 알 수 없거나 손상된 제어 명령을 무시한 채 Ready 상태를 유지하지 않는다.
    if (McuState::status().configured && !McuState::status().shutdown) {
        McuState::shutdown(McuState::StopReason::InvalidCommand);
        if (!send_shutdown(protocol, false))
            ++statistics_.response_failures;
    }
}

void KlippyCommands::receive(void* context, Protocol& protocol,
                             const uint8_t* payload, uint32_t length)
{
    if (context != nullptr)
        static_cast<KlippyCommands*>(context)->process(protocol, payload, length);
}

void KlippyCommands::process(Protocol& protocol, const uint8_t* payload, uint32_t length)
{
    // 빈payload는유효한프레임이다. 실행할명령없이기존Protocol이ACK만보내게한다.
    if (length == 0)
        return;
    Failure failure;
    if (payload == nullptr || length > Protocol::kMaximumPayloadSize) {
        failure.code = Error::InvalidArgument;
        ++statistics_.rejected_payloads;
        send_error(protocol, failure);
        return;
    }

    // 첫pass는전체명령의형식/인수를검증만한다. 뒤명령이잘렸는데앞명령부터실행하는것을방지한다.
    // 두진단명령은순수계산이므로이정책을적용하기쉽다. 향후상태변경/실장비명령에대해
    // 자동트랜잭션/rollback까지보장하는것은아니다. 그런안전정책은명령별로정의해야한다.
    uint32_t cursor = 0;
    while (cursor < length) {
        Parsed parsed;
        failure = {};
        if (!parse_one(payload, length, cursor, parsed, failure)) {
            ++statistics_.rejected_payloads;
            send_error(protocol, failure);
            return;
        }
    }

    // 검증통과후같은읽기전용payload를다시해석하여handler함수포인터로연결한다.
    // 최대59byte라저장용동적배열/힙이필요없다. 두pass비용은추후처리량시험에서평가한다.
    cursor = 0;
    while (cursor < length) {
        Parsed parsed;
        failure = {};
        if (!parse_one(payload, length, cursor, parsed, failure))
            return;
        ++statistics_.executed;
        // 정지 상태에서는 조회/복구만 허용한다. 같은 payload 안에서 정지한 뒤의 명령도 검사한다.
        const auto id = parsed.definition->id;
        if (McuState::status().shutdown
            && id != KlippyCommandIds::kIdentify
            && id != KlippyCommandIds::kGetClock && id != KlippyCommandIds::kGetUpTime
            && id != KlippyCommandIds::kGetConfig && id != KlippyCommandIds::kConfigReset
            && id != KlippyCommandIds::kEmergencyStop) {
            if (!send_shutdown(protocol, true))
                ++statistics_.response_failures;
            continue;
        }
        if (!parsed.definition->handler(protocol, parsed.arguments))
            ++statistics_.response_failures;
    }
    // 함수복귀후Protocol이ACK를보낸다. 응답/ACK의sequence는모두현재다음기대번호다.
    // 문법오류여도transport가받은프레임번호는소비된다. 명령오류는별도응답으로보고한다.
}

bool KlippyCommands::get_clock(Protocol& protocol, const Arguments&)
{

    // Clock 응답에는 시계의 하위 32bit만 전달한다.
    // Klippy는 최초 uptime과 이후 clock 응답으로 순환을 추적한다.
    const uint32_t clock = static_cast<uint32_t>(McuClock::ticks64());

    Writer response;
    return response.append_u32(KlippyCommandIds::kClock)
        && response.append_u32(clock)
        && protocol.send_payload(response.data, response.length);
}

bool KlippyCommands::get_uptime(Protocol& protocol, const Arguments&)
{
    // 반드시 한 번 읽은 시각에서 상위/하위를 나눈다.
    // 따로 두 번 읽으면 Counter 순환 경계에서 서로 다른 시각이 섞일 수 있다.
    const uint64_t ticks = McuClock::ticks64();
    const uint32_t high = static_cast<uint32_t>(ticks >> 32);
    const uint32_t clock = static_cast<uint32_t>(ticks);

    Writer response;
    return response.append_u32(KlippyCommandIds::kUpTime)
        && response.append_u32(high)
        && response.append_u32(clock)
        && protocol.send_payload(response.data, response.length);
}

bool KlippyCommands::get_config(Protocol& protocol, const Arguments&)
{

    const auto state = McuState::status();

    Writer response;
    return response.append_u32(KlippyCommandIds::kConfig)
        && response.append_u32(state.configured ? 1u : 0u)
        && response.append_u32(state.config_crc)
        && response.append_u32(state.shutdown ? 1u : 0u)
        && response.append_u32(state.move_count)
        && protocol.send_payload(response.data, response.length);
}

bool KlippyCommands::allocate_oids(Protocol& protocol, const Arguments& arguments)
{
    // Byte 인수의 0-255 범위는 parse_one() 에서 이미 검사한다.
    const auto count = static_cast<uint8_t>(arguments.values[0]);

    // 실패하면 공통 정지 상태와 Klippy shutdown 응답을 함께 전달한다.
    if (!McuState::allocate_oids(count))
        return send_shutdown(protocol, false);

    // 이 명령은 별도 성공 response 없이 Protocol의 ACK만 전송.
    // handler의 false는 응답 전송 실패 통계로 집계되므로, 상태 변경 거부를 false로 반환하지 않음.
    return true;
}

bool KlippyCommands::send_shutdown(Protocol& protocol, bool already_shutdown)
{
    Writer response;
    return response.append_u32(already_shutdown ? KlippyCommandIds::kIsShutdown : KlippyCommandIds::kShutdown)
        && response.append_u32(static_cast<uint32_t>(McuClock::ticks64()))
        && response.append_u32(static_cast<uint32_t>(McuState::status().stop_reason))
        && protocol.send_payload(response.data, response.length);
}

bool KlippyCommands::finalize_config(Protocol& protocol, const Arguments& arguments)
{
    return McuState::finalize_config(static_cast<uint32_t>(arguments.values[0]))
        || send_shutdown(protocol, false);
}

bool KlippyCommands::emergency_stop(Protocol& protocol, const Arguments&)
{
    const bool stopped = McuState::status().shutdown;
    McuState::shutdown(McuState::StopReason::Emergency);
    return send_shutdown(protocol, stopped);
}

bool KlippyCommands::config_reset(Protocol& protocol, const Arguments&)
{
    return McuState::config_reset() || send_shutdown(protocol, false);
}

bool KlippyCommands::config_digital_out(Protocol& protocol, const Arguments& arguments)
{
    const bool success = McuState::configure_digital_output(
        static_cast<uint8_t>(arguments.values[0]),
        static_cast<uint32_t>(arguments.values[1]),
        arguments.values[2] != 0,
        arguments.values[3] != 0,
        static_cast<uint32_t>(arguments.values[4]));

    // 성공은 기존 transport ACK로 전달. 실패는 공통 상태의 정지 사유를 Klippy로 알림.
    return success || send_shutdown(protocol, false);
}

bool KlippyCommands::update_digital_out(Protocol& protocol, const Arguments& arguments)
{
    const bool success = McuState::update_digital_output(static_cast<uint8_t>(arguments.values[0]), arguments.values[1] != 0);

    return success || send_shutdown(protocol, false);
}

bool KlippyCommands::queue_digital_out(Protocol& protocol, const Arguments& arguments)
{
    const bool success = McuState::queue_digital_output(
        static_cast<uint8_t>(arguments.values[0]),
        static_cast<uint32_t>(arguments.values[1]),
        arguments.values[2] != 0);

    return success || send_shutdown(protocol, false);
}

void KlippyCommands::poll(Protocol& protocol)
{
    const uint32_t now = static_cast<uint32_t>(McuClock::ticks64());

    if(!McuState::poll_outputs(now))
    {
        if(!send_shutdown(protocol, false))
        {
            ++statistics_.response_failures;
        }
    }
}