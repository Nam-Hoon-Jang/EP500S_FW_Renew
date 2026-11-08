#include "mcu_clock.h"
#include "sam3x8e.h"
#include <cassert>
#include <cstdio>
extern "C" void TC0_Handler();

int main()
{
    McuClock::init();
    assert(McuClock::kFrequencyHz == 6000000u);
    assert(test_pmc.PMC_PCER0 == (1u << ID_TC0));
    auto& timer = test_tc.TC_CHANNEL[0];
    assert(timer.TC_IER == TC_IER_COVFS);
    assert(timer.TC_CCR == (TC_CCR_CLKEN | TC_CCR_SWTRG));
    timer.TC_CV = 0xFFFFFFFEu;
    assert(McuClock::ticks64() == UINT64_C(0xFFFFFFFE));
    timer.TC_CV = 3;
    timer.TC_SR.value = TC_SR_COVFS;
    test_primask = 1;
    assert(McuClock::ticks64() == UINT64_C(0x100000003));
    assert(test_primask == 1); // 호출 전 차단 상태 보존
    TC0_Handler(); // 이미 SR을 읽은 pending IRQ는 중복 증가하지 않는다.
    test_primask = 0;
    assert(McuClock::ticks64() == UINT64_C(0x100000003));
    assert(test_primask == 0);
    timer.TC_SR.value = TC_SR_COVFS;
    TC0_Handler();
    timer.TC_CV = 7;
    assert(McuClock::ticks64() == UINT64_C(0x200000007));
    puts("PASS: TC0 init/overflow/pending IRQ/no double count/PRIMASK (fake MMIO)");
}
