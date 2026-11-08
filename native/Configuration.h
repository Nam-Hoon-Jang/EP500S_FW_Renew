#pragma once

#define HOST_PROTOCOL_KLIPPY 1
#define HOST_PROTOCOL_CUSTOM 2

// 현재 사용할 방식 선택
#define HOST_PROTOCOL HOST_PROTOCOL_KLIPPY

#if HOST_PROTOCOL == HOST_PROTOCOL_KLIPPY

#elif HOST_PROTOCOL == HOST_PROTOCOL_CUSTOM
    #error "Custom host protocol is not ready."
#else
    #error "Invalid HOST_PROTOCOL setting."
#endif