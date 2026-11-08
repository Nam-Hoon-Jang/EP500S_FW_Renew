#include "gpio/output_queue.h"
#include <assert.h>
#include <stdio.h>

int main()
{
    Gpio::OutputQueue queue;
    Gpio::OutputQueue::Event event;

    assert(!queue.pop_due(100, event));

    assert(queue.push(100, {0, 200, true}));
    assert(!queue.push(100, {0, 150, false})); // 순서 역전
    assert(!queue.pop_due(199, event));
    assert(queue.pop_due(200, event));
    assert(event.oid == 0 && event.value);
    assert(!queue.pop_due(200, event));

    assert(!queue.push(100, {0, 99, true})); // 과거 시각

    // 32bit 시계 순환 경계
    assert(queue.push(0xFFFFFFF0u, {0, 0x10u, false}));
    assert(!queue.pop_due(0x0Fu, event));
    assert(queue.pop_due(0x10u, event));
    assert(!event.value);

    queue.clear();

    for (uint32_t i = 0; i < Gpio::OutputQueue::kCapacity; ++i)
        assert(queue.push(100, {0, 200 + i, true}));

    assert(!queue.push(100, {0, 300, true})); // 공간 부족

    for (uint32_t i = 0; i < Gpio::OutputQueue::kCapacity; ++i) {
        assert(queue.pop_due(300, event));
        assert(event.clock == 200 + i);
    }

    assert(queue.push(300, {0, 400, true}));
    queue.clear();
    assert(!queue.pop_due(500, event));

    queue.clear();
    assert(queue.push(100, {0, 200, true}));
    assert(queue.push(100, {1, 210, false}));
    assert(queue.push(100, {0, 220, true}));

    queue.cancel(0);

    assert(queue.pop_due(300, event));
    assert(event.oid == 1 && event.clock == 210);
    assert(!queue.pop_due(300, event));

    puts("PASS: output queue timing/order/capacity/wrap/clear");
}