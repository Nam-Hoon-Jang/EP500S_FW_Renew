#pragma once
#include <stdint.h>
#include "base/mcu_clock.h"

namespace Gpio
{
    class OutputQueue final
    {
        public:
            static constexpr uint8_t kCapacity = 8;

            static constexpr uint32_t kMaximumDelayTicks = McuClock::kFrequencyHz * 30u;

            static_assert(kMaximumDelayTicks < 0x80000000u, "Queue time range must be less than half a clock cycle");

            struct Event
            {
                uint8_t oid = 0;
                uint32_t clock = 0;
                bool value = false;
            };

            // 큐 예약은 시간순으로 넣는다. 공간부족/과거시각/허용범위초과/순서역전 인 경우 false
            bool push(uint32_t now, const Event& event);

            // 맨 앞 예약의 실행시각 도달 시, 큐에서 꺼내 event에 저장. 아직 실행 시각이 안 됐거나, 큐가 비어 있으면 false
            bool pop_due(uint32_t now, Event& event);

            void clear();

            // 해당 출력의 예약만 큐에서 취소함.
            void cancel(uint8_t oid);

        private:
            
            Event events_[kCapacity] = {};
            uint8_t head_ = 0;      // 가장 먼저 꺼낼 항목 위치
            uint8_t count_ = 0;    // 저장된 항목 수
    };
}