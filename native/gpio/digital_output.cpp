#include "digital_output.h"
#include "gpio.h"

namespace Gpio
{
    /// @brief Digital Output 핀은 모두 TRUE/FALSE 값만 갖는다.
    /// @param pin 
    /// @param initial_value 
    /// @param shutdown_value 
    /// @return 
    bool DigitalOutput::configure(uint32_t pin, bool initial_value, bool shutdown_value)
    {
        // 같은 객체를 다른 핀으로 재설정하지 않음.
        if(configured_)
        {
            return false;
        }

        // 하드웨어 초기화에 성공한 경우에만 객체 상태를 저장함.
        if(!configure_output(pin, initial_value))
        {
            return false;
        }
        
        // 위에서 문제 없을 시, 해당 핀, 종료시 설정값, DO설정 플래그 true로 변경.
        pin_ = pin;
        shutdown_value_ = shutdown_value;
        configured_ = true;
        return true;
    }

    bool DigitalOutput::set(bool value)
    {
        if(!configured_)
        {
            return false;
        }

        return write(pin_, value);
    }

    bool DigitalOutput::apply_shutdown()
    {
        if(!configured_)
        {
            return false;
        }
        return write(pin_, shutdown_value_);
    }

    bool DigitalOutput::reset()
    {
        if(!configured_)
        {
            return true;
        }

        if(!apply_shutdown())
        {
            return false;
        }

        pin_ = 0;
        shutdown_value_ = false;
        configured_ = false;
        return true;
    }
}