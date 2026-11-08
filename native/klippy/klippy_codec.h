/*
 * Klippy 전용 호환 코덱.
 * 현재 시험 호스트인 Klippy와 통신하기 위해 Klipper의 signed VLQ 규칙을 사용한다.
 * 자체 호스트로 Klippy를 대체할 때는 제품용 인코딩 규칙을 별도로 설계·확정해야 한다.
 * 그때 이 규칙을 유지할지, 범용 varint/고정 길이/자체 규칙으로 바꿀지도 다시 결정한다.
 * 이 파일의 형식을 장기 제품 규격으로 자동 채택한 것으로 해석하지 않는다.
 * wire 규칙 호환과 구현 코드의 출처·라이선스는 별개다.
 */
#pragma once
#include <stdint.h>

namespace KlippyCodec {
constexpr uint32_t kMaximumIntegerBytes = 5u;

enum class Status {
    Ok,
    NeedMoreData,     // 마지막 바이트까지 오지 않음. 입력 범위 밖으로 읽지 않는다.
    BufferTooSmall,   // 인코딩 결과가 출력 공간보다 큼. 출력 버퍼를 부분 변경하지 않는다.
    TooLong,          // 정수 하나가5byte를 초과하려 함.
    Overflow,         // 해당32bit 타입이 표현할 범위를 벗어남.
    InvalidArgument  // 유효한 버퍼가 필요한데 nullptr를 전달함.
};

struct Result {
    Status status;
    uint32_t bytes_used; // Ok일 때만 소비/작성한 길이. 실패는 항상0.
};

// 함수는 MCU/UART/프레임을 모르며, 바이트 배열과 숫자만 변환한다.
// const uint8_t*는 입력을 수정하지 않겠다는 뜻이다. 배열 전체를 복사하는 인수는 아니다.
// value는 참조이므로 성공할 때 호출한 쪽 변수에 결과를 저장한다(C# ref와 유사).
// 실패 시 value와 입력 위치는 바뀌지 않는다. 호출자가 bytes_used만큼 커서를 이동한다.
Result decode_u32(const uint8_t* data, uint32_t length, uint32_t& value);
Result decode_i32(const uint8_t* data, uint32_t length, int32_t& value);
Result encode_u32(uint32_t value, uint8_t* output, uint32_t capacity);
Result encode_i32(int32_t value, uint8_t* output, uint32_t capacity);
} // namespace KlippyCodec
