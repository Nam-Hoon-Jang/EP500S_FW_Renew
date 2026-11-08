#include "oid_registry.h"
#include "gpio/digital_output.h"

namespace
{
    Gpio::DigitalOutput* outputs[OidRegistry::kCapacity] = {};
    bool allocated = false;
    uint8_t object_count = 0;
}

namespace OidRegistry
{
    /// @brief 사용할 OID 갯수 설정
    /// @param count 사용할 OID 갯수
    /// @return 
    bool allocate(uint8_t count)
    {
        if(allocated || count > kCapacity)
        {
            return false;
        }

        object_count = count;
        allocated = true;
        return true;
    }

    bool register_output(uint8_t oid, Gpio::DigitalOutput& output)
    {
        if(!allocated || oid >= object_count)
        {
            return false;   
        }

        // 이미 사용 중인 OID 또는 초기화되지 않은 DigitalOutput이면 등록 거부
        if(outputs[oid] != nullptr || !output.configured())
        {
            return false;
        }

        // 같은 객체를 다른 OID에 중복 등록하지 않음.
        for(uint8_t i = 0; i < object_count; ++i)
        {
            if(outputs[i] == &output)
            {
                return false;
            }
        }

        outputs[oid] = &output;
        return true;
    }

    Gpio::DigitalOutput* find_output(uint8_t oid)
    {
        if(!allocated || oid >= object_count)
        {
            return nullptr;
        }

        return outputs[oid];
    }

    bool all_registered()
    {
        if(!allocated)
        {
            return false;
        }

        for(uint8_t i = 0; i < object_count; ++i)
        {
            if(outputs[i] == nullptr)
            {
                return false;
            }
        }
        // count=0면 등록 대상이 없으므로 완료로 판정.
        return true;
    }

    bool reset()
    {
        bool success = true;

        // DigitalOutput 전체의 정지값 적용 및 재설정 수행
        for(auto* output : outputs)
        {
            if(output != nullptr && !output->reset())
            {
                success = false;
            }
        }

        if(!success)
        {
            return false;
        }

        for(auto& output : outputs)
        {
            output = nullptr;
        }

        object_count = 0;
        allocated = false;
        return true;
    }

    bool apply_shutdown_all()
    {
        bool success = true;

        for(auto* output : outputs)
        {
            if(output != nullptr && !output->apply_shutdown())
            {
                success = false;
            }
        }
        return success;
    }
}