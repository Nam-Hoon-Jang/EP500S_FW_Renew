"""RPi 전용 수동 실물 시험. 플래시/전원/서비스 설정을 변경하지 않는다."""
import argparse
import sys
import time
import serial


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    return crc & 0xFFFF


def frame(number, payload=b''):
    body = bytes([len(payload) + 5, 0x10 | number]) + payload
    return body + crc16(body).to_bytes(2, 'big') + b'\x7e'


def ack_bytes(data):
    """모든 응답의 프레임 무결성을 확인한 뒤 빈 ACK만 비교 대상으로 추출한다.

    진단 명령 계층이 연결되면 기존 시험의 임의 payload에 debug_command_error가 올 수 있다.
    이 시험은 프레임 계층 전용이므로 명령 응답은 분리하고, 값/명령 실행은 command_smoke에서 검사한다.
    """
    cursor = 0
    acknowledgements = bytearray()
    while cursor < len(data):
        length = data[cursor]
        if not 5 <= length <= 64 or cursor + length > len(data):
            raise ValueError('잘렸거나 잘못된 길이의 응답')
        block = data[cursor:cursor + length]
        if block[-1] != 0x7E or block[1] & 0xF0 != 0x10:
            raise ValueError('잘못된 응답 prefix/sync')
        if crc16(block[:-3]) != int.from_bytes(block[-3:-1], 'big'):
            raise ValueError('응답 CRC 불일치')
        if length == 5:
            acknowledgements.extend(block)
        elif block[2] not in (4, 5, 6):
            raise ValueError('알 수 없는 진단 응답 ID')
        cursor += length
    return bytes(acknowledgements)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--reset-wait', type=float, default=10)
    args = parser.parse_args()
    assert crc16(b'123456789') == 0x6F91
    passed = []
    with serial.Serial(args.port, 250000, timeout=0.3) as port:
        print(f'{args.reset_wait:g}초 안에 Due RESET을 눌렀다 놓아주세요. ERASE는 누르지 않습니다.', flush=True)
        time.sleep(args.reset_wait)
        port.reset_input_buffer()

        def check(label, data, expected):
            port.write(data)
            port.flush()
            received = port.read(128)
            ok = ack_bytes(received) == expected
            print(f'{label}: {"PASS" if ok else "FAIL"}; RX={received.hex(" ")}', flush=True)
            passed.append(ok)

        check('정상0', frame(0,b'A'), frame(1))
        check('중복0', frame(0), frame(1))
        check('순서건너뜀2', frame(2), frame(1))
        for number in range(1,16):
            check(f'정상{number}', frame(number), frame((number+1)&15))
        check('순환0', frame(0), frame(1))

        broken=bytearray(frame(1,b'A')); broken[-2] ^= 1
        check('CRC오류 NAK1회', broken, frame(1))
        check('연속오류 응답억제', broken, b'')
        check('CRC복구', frame(1), frame(2))
        check('잘못된길이 재동기화', b'\x00\xaa\xbb\x7e', frame(2))
        check('다음정상2', frame(2), frame(3))

        # 0.3초 read는 부분프레임1초timeout보다짧다. 데이터를조각내도중간ACK가없다.
        partial=frame(3,b'A\x7eB')
        check('부분수신 대기', partial[:-1], b'')
        check('부분수신 완성', partial[-1:], frame(4))
        check('최대64byte', frame(4,b'\x7e'*59), frame(5))
        check('연속2프레임', frame(5)+frame(6), frame(6)+frame(7))

        # 잘린프레임의timeout NAK를 기다린 뒤 sync를별도로주고정상프레임재전송.
        port.write(bytes([10,0x17,0x41])); port.flush()
        time.sleep(1.15)
        received=port.read(128)
        ok=ack_bytes(received) == frame(7)
        print(f'부분프레임 timeout: {"PASS" if ok else "FAIL"}; RX={received.hex(" ")}',flush=True)
        passed.append(ok)
        check('timeout복구', b'\x7e'+frame(7), frame(8))
    print('PASS' if all(passed) else 'FAIL')
    return 0 if all(passed) else 1


if __name__ == '__main__':
    sys.exit(main())

