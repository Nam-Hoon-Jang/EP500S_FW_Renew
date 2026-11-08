"""제품 FW에 넣을 zlib 압축 명령 사전을 생성한다. 외부 패키지 불필요."""
import json
from pathlib import Path
import zlib

root = Path(__file__).resolve().parents[1]
dictionary = {
    'commands': {
        'identify offset=%u count=%c': 1,
        'debug_ping value=%u': 2,
        'debug_sum a=%i b=%i': 3,
        'get_clock': 7,
        'get_uptime': 9,
        'get_config': 11,
        'allocate_oids count=%c': 13,
        'finalize_config crc=%u': 14,
        'emergency_stop': 15,
        'config_reset': 16,
        'config_digital_out oid=%c pin=%u value=%c default_value=%c max_duration=%u': 19,
        'update_digital_out oid=%c value=%c': 20,
        'queue_digital_out oid=%c clock=%u on_ticks=%u': 21,
    },
    'responses': {
        'identify_response offset=%u data=%.*s': 0,
        'debug_pong value=%u': 4,
        'debug_sum_result value=%i': 5,
        'debug_command_error code=%c offset=%u command_id=%u': 6,
        'clock clock=%u': 8,
        'uptime high=%u clock=%u': 10,
        'config is_config=%c crc=%u is_shutdown=%c move_count=%hu': 12,
        'shutdown clock=%u static_string_id=%hu': 17,
        'is_shutdown clock=%u static_string_id=%hu': 18,
    },
    'config': {
        # CPU 주파수가 아니라 응답 clock 값의 초당 증가량.
        'CLOCK_FREQ': 6000000,
        'STATS_SUMSQ_BASE': 256,
        'SERIAL_BAUD': 250000,
        'MCU': 'sam3x8e',
    },
    'enumerations': {
        'static_string_id': {
            'Emergency stop requested': 1,
            'Invalid configuration state': 2,
            'Device configuration is not implemented': 3,
            'Invalid command': 4,
            },
        'pin': {
            'PB27': 59,
            },
    },
    'version': 'ep500s-native-ready-v1',
    'build_versions': 'native C++17',
}
data = zlib.compress(json.dumps(dictionary, separators=(',', ':')).encode(), 9)
lines = [', '.join(f'0x{b:02x}' for b in data[i:i+12]) + ','
         for i in range(0, len(data), 12)]
target = root / 'klippy/klippy_dictionary.h'
target.write_text('// tools/generate_dictionary.py로 생성. 직접 수정하지 않는다.\n'
                  '#pragma once\n#include <stdint.h>\n'
                  '#include "mcu_clock.h"\n'
                  'static_assert(McuClock::kFrequencyHz == 6000000u, "Regenerate dictionary CLOCK_FREQ");\n'
                  'namespace KlippyDictionary {\n'
                  'inline constexpr uint8_t data[] = {\n    '
                  + '\n    '.join(lines) + '\n};\n}\n', encoding='utf-8')
print(f'dictionary: {len(data)} compressed bytes')
