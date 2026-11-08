#include "mcu_state.h"
#include "oid_registry.h"
#include "gpio/digital_output.h"
#include "gpio/output_queue.h"
#include "mcu_clock.h"

namespace
{
    // MCU 공통 Status. 통신 명령 ID나 Encoding Rule은 포함X
    // 이후 설정/정지 기능도 이 상태 관리 함수에 추가.
    McuState::Status current_status{};

    // count = 0도 정상이니, allocated 여부는 count와 별개로 저장하고 판단.
    bool oids_allocated = false;
    uint8_t oid_count = 0;

    // 설정 초기화 이후에도 객체 자체는 살아 있어야 함.
    Gpio::DigitalOutput led_output;

    Gpio::OutputQueue output_queue;
}

namespace McuState
{
    Status status()
    {
        return current_status;
    }

    bool allocate_oids(uint8_t count)
    {
        // 이미 예약했거나, 설정이 끝난 상태면 다시 예약X. 실패 시 기존 번호 범위 보존.
        if(current_status.shutdown || current_status.configured || oids_allocated)
        {
            shutdown(StopReason::InvalidState);
            return false;
        }

        if(!OidRegistry::allocate(count))
        {
            shutdown(StopReason::InvalidState);
            return false;
        }

        oid_count = count;
        oids_allocated = true;
        return true;
    }

    bool oid_in_range(uint8_t oid)
    {
        // 번호가 실제 범위에 속하는지만 검사. 실제 등록 확인은 못함.
        return oids_allocated && oid < oid_count;
    }

    void shutdown(StopReason reason)
    {
        if (!current_status.shutdown)
        {
            current_status.shutdown = true;
            current_status.stop_reason = reason;
        }

        output_queue.clear();

        // 실패하더라도 정지 상태를 해제하지 않음. 현재 write()의 반환값은 S/W 처리 여부이고, 실제 핀 전압 측정 결과는 아님.
        (void)OidRegistry::apply_shutdown_all();
    }

    bool finalize_config(uint32_t crc)
    {
        if (current_status.shutdown || current_status.configured || !oids_allocated) {
            shutdown(StopReason::InvalidState);
            return false;
        }
        // 예약한 모든 OID에 실제 객체가 등록돼 있어야 함.
        if (!OidRegistry::all_registered()) 
        {
            shutdown(StopReason::UnsupportedDevice);
            return false;
        }

        current_status.move_count = Gpio::OutputQueue::kCapacity;
        current_status.config_crc = crc;
        current_status.configured = true;
        return true;
    }

    bool config_reset()
    {
        if (!current_status.shutdown) {
            shutdown(StopReason::InvalidState);
            return false;
        }

        output_queue.clear();

        if(!OidRegistry::reset())
        {
            return false;
        }
        current_status = {};
        oids_allocated = false;
        oid_count = 0;
        return true;
    }

    bool configure_digital_output(uint8_t oid, uint32_t pin, bool inintial_value, bool shutdown_value, uint32_t max_duration)
    {
        if(current_status.shutdown 
            || current_status.configured
            || !oid_in_range(oid)
            || OidRegistry::find_output(oid) != nullptr
            || led_output.configured())
            {
                shutdown(StopReason::InvalidState);
                return false;
            }

        if(max_duration != 0)
        {
            shutdown(StopReason::UnsupportedDevice);
            return false;
        }

        if(!led_output.configure(pin, inintial_value, shutdown_value))
        {
            shutdown(StopReason::UnsupportedDevice);
            return false;
        }

        if(!OidRegistry::register_output(oid, led_output))
        {
            (void)led_output.apply_shutdown();
            shutdown(StopReason::InvalidState);
            return false;
        }

        return true;
    }

    bool update_digital_output(uint8_t oid, bool value)
    {
        if(current_status.shutdown || !current_status.configured)
        {
            shutdown(StopReason::InvalidState);
            return false;
        }

        auto* output = OidRegistry::find_output(oid);

        if(output == nullptr)
        {
            shutdown(StopReason::InvalidState);
            return false;
        }
        
        output_queue.cancel(oid);

        if(!output->set(value))
        {
            shutdown(StopReason::InvalidState);
            return false;
        }

        return true;
    }

    bool queue_digital_output(uint8_t oid, uint32_t clock, bool value)
    {
        if(current_status.shutdown || !current_status.configured || OidRegistry::find_output(oid) == nullptr)
        {
            shutdown(StopReason::InvalidState);
            return false;
        }

        const uint32_t now = static_cast<uint32_t>(McuClock::ticks64());

        // 공간 확인 전에 실행 시각에 도달한 예약을 처리한다.
        if(!poll_outputs(now))
        {
            return false;
        }

        // 공간 부족·과거 시각·순서 역전 등을 무시하지 않고 정지한다.
        if(!output_queue.push(now, {oid, clock, value}))
        {
            shutdown(StopReason::InvalidState);
            return false;
        }
        return true;
    }

    bool poll_outputs(uint32_t now)
    {
        if(current_status.shutdown || !current_status.configured)
        {
            return true;
        }

        Gpio::OutputQueue::Event event;

        while(output_queue.pop_due(now, event))
        {
            auto* output = OidRegistry::find_output(event.oid);

            if(output == nullptr || !output->set(event.value))
            {
                shutdown(StopReason::InvalidState);
                return false;
            }
        }

        return true;
    }
}
