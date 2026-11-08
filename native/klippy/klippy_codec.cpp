/*
 * Klippy 전용 signed VLQ 구현. 범용 unsigned LEB128/ZigZag와 서로 호환되지 않는다.
 * Klippy 대체 시 제품용 인코딩 규칙을 별도로 설계·확정하고 이 모듈의 교체 여부를 결정한다.
 * 현재는 프레임 payload에 아직 연결하지 않은 독립 코덱이며 identify/명령 실행은 포함하지 않는다.
 */
#include "klippy_codec.h"

namespace {
// 익명 namespace: 이 파일의 보조 함수만 숨긴다. 공개 API는 아래 KlippyCodec namespace에 둔다.
// uint32_t의 최대값과 int32_t의 음수까지 한 번에 다루므로 중간 값은 int64_t를 사용한다.
// Cortex-M3에서64bit 연산은 여러 명령이 될 수 있다. 지금은 안전성/명확성 우선이며,
// 이후 실제 처리량을 측정해 병목일 때만32bit 최적화를 검토한다.
KlippyCodec::Result decode_number(const uint8_t* data, uint32_t length, int64_t& value)
{
    using KlippyCodec::Status;
    if (length == 0)
        return {Status::NeedMoreData, 0};
    if (data == nullptr)
        return {Status::InvalidArgument, 0};

    // 첫7bit가0x60~0x7F이면 -32~-1로 해석한다. 따라서 양수96도1byte가 아니다.
    // 0x60의일반unsigned값은96이지만, Klippy signed 규칙에서는-32라는 의미를 가진다.
    int64_t number = static_cast<int64_t>(data[0] & 0x7Fu);
    if (number >= 0x60)
        number -= 0x80;

    for (uint32_t i = 0; i < KlippyCodec::kMaximumIntegerBytes; ++i) {
        if (i >= length)
            return {Status::NeedMoreData, 0};
        if (i != 0) {
            // 먼저 받은7bit가 상위 그룹이다. 음수를 왼쪽 shift하면 C++17에서 문제가 생길 수
            // 있으므로 곱셈으로 표현한다. 최대5byte이므로 int64_t 중간값은 넘지 않는다.
            number = number * 128 + static_cast<int64_t>(data[i] & 0x7Fu);
        }
        if ((data[i] & 0x80u) == 0) {
            value = number;
            return {Status::Ok, i + 1u};
        }
    }
    // 다섯 번째 byte도계속비트가1이면 더긴 정수. 여섯 번째 byte는 읽지 않는다.
    return {Status::TooLong, 0};
}

KlippyCodec::Result encode_number(int64_t value, uint8_t* output, uint32_t capacity)
{
    using KlippyCodec::Status;
    // n바이트의최소/최대는[-32*128^(n-1), 96*128^(n-1)-1].
    // 먼저필요길이를정해야 공간부족일때버퍼일부만덮어쓰는상황을피할수있다.
    uint32_t count = 1;
    int64_t minimum = -32;
    int64_t maximum = 95;
    while (value < minimum || value > maximum) {
        if (count == KlippyCodec::kMaximumIntegerBytes)
            return {Status::Overflow, 0};
        minimum *= 128;
        maximum = (maximum + 1) * 128 - 1;
        ++count;
    }
    if (capacity < count)
        return {Status::BufferTooSmall, 0};
    if (output == nullptr)
        return {Status::InvalidArgument, 0};

    // 음수→unsigned 변환은2^64로순환하는 C++규칙을이용한다.
    // 음수의오른쪽shift에대한구현의존성을피하고, 각7bit그룹만추출한다.
    const uint64_t bits = static_cast<uint64_t>(value);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t shift = 7u * (count - i - 1u);
        uint8_t part = static_cast<uint8_t>((bits >> shift) & 0x7Fu);
        if (i + 1u < count)
            part = static_cast<uint8_t>(part | 0x80u);
        output[i] = part;
    }
    return {Status::Ok, count};
}
}

namespace KlippyCodec {
Result decode_u32(const uint8_t* data, uint32_t length, uint32_t& value)
{
    int64_t number = 0;
    const Result result = decode_number(data, length, number);
    if (result.status != Status::Ok)
        return result;
    if (number < INT32_MIN || number > UINT32_MAX)
        return {Status::Overflow, 0};
    // Klippy의unsigned해석은음수형태로들어온값도하위32bit로복원한다.
    // 예: 한바이트0x7F는signed -1이며 unsigned로읽으면0xFFFFFFFF다.
    value = static_cast<uint32_t>(number);
    return result;
}

Result decode_i32(const uint8_t* data, uint32_t length, int32_t& value)
{
    int64_t number = 0;
    const Result result = decode_number(data, length, number);
    if (result.status != Status::Ok)
        return result;
    if (number < INT32_MIN || number > INT32_MAX)
        return {Status::Overflow, 0};
    value = static_cast<int32_t>(number);
    return result;
}

Result encode_u32(uint32_t value, uint8_t* output, uint32_t capacity)
{
    return encode_number(value, output, capacity);
}

Result encode_i32(int32_t value, uint8_t* output, uint32_t capacity)
{
    return encode_number(value, output, capacity);
}
} // namespace KlippyCodec
