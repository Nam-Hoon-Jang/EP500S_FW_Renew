"""RPi의 실제 Klippy VLQ 인코더로 진단 명령을 만들어 Due에 보낸다. 기록/서비스 변경 없음."""
import argparse
from pathlib import Path
import sys
import time
import serial

sys.path.insert(0,str(Path.home()/'klipper/klippy'))
import msgproto

def u(value):
    data=[]
    msgproto.PT_uint32().encode(data,value)
    return bytes(data)

def s(value):
    data=[]
    msgproto.PT_int32().encode(data,value)
    return bytes(data)

def frame(number,payload=b''):
    body=bytes([len(payload)+5,0x10|number])+payload
    return body+bytes(msgproto.crc16_ccitt(body))+b'\x7e'

def error(code,offset,command_id):
    return u(6)+u(code)+u(offset)+u(command_id)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='/dev/ttyACM0')
    args=parser.parse_args()
    results=[]
    number=0
    with serial.Serial(args.port,250000,timeout=0.4) as port:
        print('10초 안에 Due RESET을 눌렀다 놓아주세요. ERASE는 누르지 않습니다.',flush=True)
        time.sleep(10)
        port.reset_input_buffer()

        def check(label,payload,replies):
            nonlocal number
            sent_number=number
            number=(number+1)&15
            expected=b''.join(frame(number,response) for response in replies)+frame(number)
            port.write(frame(sent_number,payload)); port.flush()
            received=port.read(512)
            ok=received==expected
            results.append(ok)
            print(f'{label}: {"PASS" if ok else "FAIL"}; RX={received.hex(" ")}',flush=True)

        check('ping 값300',u(2)+u(300),[u(4)+u(300)])
        check('음수 더하기',u(3)+s(-123)+s(23),[u(5)+s(-100)])
        ping=u(2)+u(0xFFFFFFFF)
        check('한프레임 두명령',ping+u(3)+s(40)+s(2),[u(4)+u(0xFFFFFFFF),u(5)+s(42)])
        check('모르는 ID',u(99),[error(1,0,99)])
        check('잘린 ID',b'\x80',[error(2,0,0)])
        check('누락된 인수',u(2),[error(2,1,2)])
        check('뒤명령 오류시 앞명령 실행차단',ping+u(99),[error(1,len(ping),99)])
        check('signed 덧셈 범위초과',u(3)+s(0x7FFFFFFF)+s(1),[error(5,len(u(3)+s(0x7FFFFFFF)),3)])
        check('오류후 정상복구',u(2)+u(1234),[u(4)+u(1234)])
        # 직전정상프레임을재전송하면응답payload없이ACK만와야한다(명령재실행금지).
        port.write(frame((number-1)&15,u(2)+u(1234))); port.flush()
        received=port.read(512)
        ok=received==frame(number)
        results.append(ok)
        print('중복명령 실행차단:', 'PASS' if ok else 'FAIL',flush=True)
    print('PASS' if all(results) else 'FAIL')
    return 0 if all(results) else 1

if __name__=='__main__':
    sys.exit(main())
