#pragma once
#include <stdint.h>

namespace Gpio
{
    class DigitalOutput;
}

namespace OidRegistry
{
    // 현재 등록 가능한 객체 수의 상한선 지정.
    // 실제 장치 수가 늘어나면, 메모리 사용량을 확인해서 값 조정.
    constexpr uint8_t kCapacity = 16;

    // 사용할 OID 갯수를 한 번에 예약한다. count = 0도 가능.
    bool allocate(uint8_t count);

    // 예약한 OID에 출력 객체(DigitalOutput)를 연결. 객체 복사 없이 주소를 저장하므로, 원본은 계속 살아 있어야 함.
    bool register_output(uint8_t oid, Gpio::DigitalOutput& output);

    // 등록된 출력 객체의 주소를 반환. 범위 밖이거나, 등록 안 됐다면 nullptr 반환
    Gpio::DigitalOutput* find_output(uint8_t oid);

    // 예약한 모든 OID에 객체 등록이 됐는지 확인
    bool all_registered();

    // 등록된 모든 출력에 정지값 적용. 일부 실패해도 다른 출력까지 일단 적용을 시도함.
    bool apply_shutdown_all();

    // 각 DigitalOutput 객체 reset한 뒤 등록 정보를 비운다.
    // 실해하면 등록 정보를 보존하고 false 반환
    bool reset();
}