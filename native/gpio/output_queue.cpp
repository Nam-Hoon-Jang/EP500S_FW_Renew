#include "output_queue.h"

namespace Gpio
{
    bool OutputQueue::push(uint32_t now, const Event& event)
    {
        if(count_ == kCapacity)
        {
            return false;
        }

        // unsigned 뺄셈으로 32bit 시계 순환을 처리.
        // 약간 지난 시각도 큰 양수 차이가 되므로 이 계산으로 정확하게 걸러낼 수 있음.
        const uint32_t delay = event.clock - now;

        if(delay > kMaximumDelayTicks)
        {
            return false;
        }

        if(count_ != 0)
        {
            const uint8_t last = (head_ + count_ - 1u) % kCapacity;

            // 마지막 예약보다 이전 시각이면 순서 역전. 같은 시각은 허용하며, 등록 순서대로 처리함.
            const uint32_t gap = event.clock - events_[last].clock;

            // 0x80000000u 는 32bit 범위의 1/2 값.
            // 이런 시각 비교는 서로 비교하는 시각 차이가 절반 범위 미만이라는 전제일 때 사용.
            if(gap >= 0x80000000u) 
            {
                return false;
            }
        }

        const uint8_t tail = (head_ + count_) % kCapacity;
        events_[tail] = event;
        ++count_;
        return true;
    }

    bool OutputQueue::pop_due(uint32_t now, Event& event)
    {
        if(count_ == 0)
        {
            return false;
        }

        const uint32_t elapsed = now - events_[head_].clock;

        if(elapsed >= 0x80000000u)
        {
            return false;
        }

        event = events_[head_];
        head_ = (head_ + 1u) % kCapacity;
        --count_;
        return true;
    }

    void OutputQueue::clear()
    {
        head_ = 0;
        count_ = 0;
    }

    void OutputQueue::cancel(uint8_t oid)
    {
        uint8_t kept = 0;

        for(uint8_t i = 0; i < count_; ++i)
        {
            const uint8_t source = (head_ + i) % kCapacity;

            if(events_[source].oid == oid)
            {
                continue;
            }
            const uint8_t destination = (head_ + kept) % kCapacity;
            events_[destination] = events_[source];
            ++kept;
        }

        count_ = kept;
    }
}