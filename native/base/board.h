#pragma once
#include <stdint.h>

// namespace는 관련 이름을 묶는다. Board::init()처럼 소속을 명시해 호출한다.
// C# static class와 사용 모습은 비슷하지만 C++ namespace에는 객체가 없다.
namespace Board {
constexpr uint32_t kMasterClockHz = 12000000u;

// 외부 크리스털 12MHz와 1ms SysTick을 준비한다. 실패하면 false이며 UART를 시작하지 않는다.
bool init();
// 부팅 후 밀리초. uint32_t이므로 약 49.7일에 순환한다.
uint32_t milliseconds();
}
