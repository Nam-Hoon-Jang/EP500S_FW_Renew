"""WSL에서 빌드한 BIN을 시험 RPi로 전송하고 Due Programming Port에 기록한다.

수동 실행 전용. 이 스크립트를 실행하면 기존 MCU FW를 지우며 Klippy를 중지한다.
기록 후 Klippy는 자동 재시작하지 않는다(현재 FW는 Klippy 명령을 지원하지 않음).
SSH 암호는 정상 SSH 입력으로 받으며 파일/명령 인수에 저장하지 않는다.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='예: demo-user@192.0.2.1')
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--image', type=Path, default=Path('build/uart-echo.bin'))
    args = parser.parse_args()
    # 대상문자열을셸코드로해석시키지않는다. 이도구는일반적인시험user@host형태만지원.
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.-]*@[A-Za-z0-9][A-Za-z0-9_.-]*', args.host):
        parser.error('--host는 user@hostname 또는 user@IPv4 형식이어야 합니다.')
    if not re.fullmatch(r'/dev/(ttyACM[0-9]+|serial/by-id/[A-Za-z0-9_.:+-]+)', args.port):
        parser.error('--port는 /dev/ttyACM번호 또는 by-id 전체 경로여야 합니다.')
    image = args.image.resolve()
    if not image.is_file():
        parser.error(f'BIN 없음: {image}')
    contents = image.read_bytes()
    if not 244 <= len(contents) <= 512*1024:
        parser.error('Due Flash 이미지 크기가 범위를 벗어났습니다.')
    digest = hashlib.sha256(contents).hexdigest()
    # 해시를붙인원격임시디렉터리로전송해기존복구BIN/사용자파일을덮어쓰지않는다.
    remote_dir = subprocess.check_output(
        ['ssh',args.host,'mktemp -d /tmp/ep500s-flash.XXXXXXXX'],text=True).strip()
    if not re.fullmatch(r'/tmp/ep500s-flash\.[A-Za-z0-9]+',remote_dir):
        raise RuntimeError('예상하지 못한 원격 임시 경로')
    remote_file = remote_dir+'/firmware.bin'
    subprocess.run(['scp',str(image),args.host+':'+remote_file],check=True)

    # ssh -t로 sudo비밀번호를터미널에서입력할수있다. 원격코드는python3 -c로전달해
    # stdin과코드입력을공유하는heredoc/input 문제를만들지않는다.
    code = r'''
import hashlib, os, pathlib, re, shutil, subprocess, sys, time
image, expected, port = sys.argv[1:]
if hashlib.sha256(pathlib.Path(image).read_bytes()).hexdigest() != expected:
    raise SystemExit('전송 SHA256 불일치: 지우기 중단')
if not os.path.exists(port):
    raise SystemExit('시리얼 장치 없음: '+port)
bossac=shutil.which('bossac')
if bossac is None:
    raise SystemExit('bossac 없음: sudo apt install bossa-cli')
# 전체시스템설정/부팅자동실행은변경하지않고현재실행중인Klippy만중지한다.
subprocess.run(['systemctl','stop','klipper'],check=True)
if shutil.which('fuser') and subprocess.run(['fuser',port],capture_output=True).returncode == 0:
    raise SystemExit('다른 프로세스가 시리얼 포트를 사용 중: FW 지우기 중단')
try:
    import serial
except ImportError:
    raise SystemExit('RPi klippy-env에 pyserial 없음: FW 지우기 중단')
print('BIN 검증 완료. Due를 지우고 부트로더에 진입합니다.',flush=True)
with serial.Serial(port,1200,timeout=1) as connection:
    connection.dtr=True
    time.sleep(0.1)
    connection.dtr=False
time.sleep(2)
# 지운뒤인식실패시재지우기를반복하지않고오류종료. 결과를사용자가확인한다.
info=subprocess.check_output([bossac,'-p',port,'-i'],text=True)
print(info,flush=True)
if not re.search(r'ATSAM3X8(?:E)?\b',info):
    raise SystemExit('SAM3X8 대상 확인 실패: 기록 중단')
subprocess.run([bossac,'-p',port,'-e','-w','-v','-b','-R',image],check=True)
print('기록/검증 완료. Klippy는 중지 상태로 유지합니다.',flush=True)
'''
    remote_python = '/home/'+args.host.split('@',1)[0]+'/klippy-env/bin/python'
    command = shlex.join(['sudo',remote_python,'-c',code,remote_file,digest,args.port])
    print('기록 대상:',args.host,args.port,'SHA256:',digest,flush=True)
    subprocess.run(['ssh','-t',args.host,command],check=True)
    # 성공한경우에만도구가만든정확한임시파일/빈폴더제거. 복구자료는대상이아니다.
    cleanup=shlex.join(['rm','--',remote_file])+' && '+shlex.join(['rmdir','--',remote_dir])
    subprocess.run(['ssh',args.host,cleanup],check=True)


if __name__ == '__main__':
    main()
