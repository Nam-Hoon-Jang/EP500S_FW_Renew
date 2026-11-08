"""identify 사전을 받아 실제 Klippy 파서로 확인한다. 기록/서비스 변경 없음."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
import time
import zlib


def validate(data, msgproto):
    dictionary = json.loads(zlib.decompress(data))
    assert dictionary['responses'] == {
        'identify_response offset=%u data=%.*s': 0,
        'debug_pong value=%u': 4, 'debug_sum_result value=%i': 5,
        'debug_command_error code=%c offset=%u command_id=%u': 6,
        'clock clock=%u': 8,
        'uptime high=%u clock=%u': 10,
        'config is_config=%c crc=%u is_shutdown=%c move_count=%hu': 12,
        'shutdown clock=%u static_string_id=%hu': 17,
        'is_shutdown clock=%u static_string_id=%hu': 18,}
    parser = msgproto.MessageParser()
    parser.process_identify(data)
    for fmt, expected in [('identify offset=%u count=%c', 1),
                          ('debug_ping value=%u', 2), ('debug_sum a=%i b=%i', 3),
                          ('get_clock', 7), ('get_uptime', 9), ('get_config', 11),
                          ('allocate_oids count=%c', 13), ('finalize_config crc=%u', 14),
                          ('emergency_stop', 15), ('config_reset', 16)]:
        command = parser.lookup_command(fmt)
        assert command.encode([0] * len(command.param_types))[0] == expected
    assert parser.get_version_info()[0] == 'ep500s-native-ready-v1'
    assert parser.get_constant_float('CLOCK_FREQ') == 6000000
    print(f'PASS: zlib/JSON/Klippy parser; {len(data)} compressed bytes', flush=True)
    return parser


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('--local', action='store_true', help='PC 생성 헤더만 검사; serial 불필요')
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    reference = root.parent/'reference/klippy/msgproto.py' if args.local else Path.home()/'klipper/klippy/msgproto.py'
    spec = importlib.util.spec_from_file_location('msgproto', reference)
    msgproto = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(msgproto)
    if args.local:
        data = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})',
                     (root/'klippy/klippy_dictionary.h').read_text(encoding='utf-8')))
        validate(data, msgproto)
        return 0
    import serial
    number = 0
    def frame(seq, payload=b''):
        body = bytes([len(payload)+5, 0x10|seq])+bytes(payload)
        return body+bytes(msgproto.crc16_ccitt(body))+b'\x7e'
    with serial.Serial(args.port, 250000, timeout=1) as port:
        print('10초 안에 Due RESET을 누르세요. ERASE 금지.', flush=True)
        time.sleep(10)
        port.reset_input_buffer()
        bootstrap = msgproto.MessageParser()
        command = bootstrap.lookup_command('identify offset=%u count=%c')
        def request(offset, count):
            nonlocal number
            sent = frame(number, command.encode([offset, count]))
            number = (number+1)&15
            port.write(sent); port.flush()
            def read_frame():
                first = port.read(1)
                assert first, 'response timeout'
                packet = first+port.read(first[0]-1)
                assert len(packet) == first[0] and 5 <= len(packet) <= 64
                assert packet[1] == 0x10|number and packet[-1] == 0x7e
                assert packet[-3:-1] == bytes(msgproto.crc16_ccitt(packet[:-3]))
                return packet
            reply = read_frame()
            assert read_frame() == frame(number), 'ACK mismatch'
            parsed = bootstrap.parse(reply)
            assert parsed['#name'] == 'identify_response' and parsed['offset'] == offset
            # 같은 프레임 재전송 시 응답을 재실행하지 않고 ACK만 보낸다.
            port.write(sent); port.flush()
            assert read_frame() == frame(number), 'duplicate ACK mismatch'
            return parsed['data']
        saved = None
        for count in (1, 40, 255):
            data = b''
            for _ in range(4096):
                chunk = request(len(data), count)
                if not chunk:
                    break
                assert len(chunk) <= count
                data += chunk
            else:
                raise AssertionError('dictionary did not terminate')
            validate(data, msgproto)
            assert saved is None or saved == data
            saved = data
            print(f'PASS: count={count}, chunk/CRC/ACK/duplicate/end', flush=True)
        assert request(0, 0) == b''
        assert request(len(saved)+1, 40) == b''
        assert request(0xffffffff, 40) == b''
        print('PASS: zero count/out-of-range offset', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
