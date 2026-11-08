"""독립 Klippy 프로세스로 Ready/긴급정지/재시작을 검사한다. Linux/WSL 전용.

--simulate: C++ 명령 계층 + PC 시계/PTY. --port: 실제 Due, 기존 Klippy 중지 필요.
기존 printer.cfg/서비스 설정/펌웨어는 변경하지 않는다. 시험에서 M112/FIRMWARE_RESTART를 실행한다.
"""
import argparse
import json
import os
from pathlib import Path
import pty
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import tty

ROOT = Path(__file__).resolve().parents[1]


def request(path, method, params=None):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(3)
        sock.connect(str(path))
        sock.sendall(json.dumps({'id': 1, 'method': method, 'params': params or {}}).encode()+b'\x03')
        data = b''
        while b'\x03' not in data:
            chunk = sock.recv(65536)
            if not chunk:
                raise RuntimeError('API disconnected')
            data += chunk
        reply = json.loads(data.split(b'\x03')[0])
        if 'error' in reply:
            raise RuntimeError(reply['error'])
        return reply['result']


def wait_state(api, state, timeout=20):
    until = time.monotonic()+timeout
    last = None
    while time.monotonic() < until:
        try:
            last = request(api, 'objects/query', {'objects': {'webhooks': None}})
            if last['status']['webhooks']['state'] == state:
                return last
        except (OSError, RuntimeError):
            pass
        time.sleep(.1)
    raise AssertionError(f'expected {state}; last={last}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument('--simulate', action='store_true')
    mode.add_argument('--port', help='실물 UART 경로. 예: /dev/ttyACM0')
    ap.add_argument('--klippy', type=Path)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    if args.klippy is None:
        args.klippy = ROOT.parent/'reference/klippy/klippy.py' if args.simulate else Path.home()/'klipper/klippy/klippy.py'
    assert args.klippy.is_file(), f'Klippy not found: {args.klippy}'
    bridge = ROOT/'build/ready-bridge'
    if args.simulate:
        subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT), '-I'+str(ROOT/'tests/fake'), '-I'+str(ROOT/'base'), '-I'+str(ROOT/'klippy'),
                    *[str(ROOT/s) for s in ('klippy/protocol.cpp', 'klippy/klippy_codec.cpp',
                      'klippy/klippy_commands.cpp', 'base/mcu_state.cpp', 'base/oid_registry.cpp', 'gpio/digital_output.cpp', 'gpio/output_queue.cpp', 'gpio/gpio.cpp', 'tests/ready_bridge.cpp')],
                        '-o', str(bridge)], check=True)
    else:
        assert Path(args.port).exists(), f'Port not found: {args.port}'
        if not shutil.which('fuser'):
            raise RuntimeError('fuser required for port ownership check')
        used = subprocess.run(['fuser', args.port], capture_output=True)
        assert used.returncode == 1, 'Port busy or ownership check failed; stop existing Klippy first'
    output = (args.output or (ROOT/'build/ready-integration' if args.simulate else Path.cwd()/'ready-results')).resolve()
    output.mkdir(exist_ok=True)
    (output/'result.json').unlink(missing_ok=True) # 이전 성공 결과를 이번 결과로 오인하지 않게 한다.
    with tempfile.TemporaryDirectory(prefix='klipper_host_native_') as temp:
        directory = Path(temp)
        master = slave = None
        if args.simulate:
            master, slave = pty.openpty()
            tty.setraw(slave)
            serial = directory/'mcu'
            serial.symlink_to(os.ttyname(slave))
        else:
            serial = Path(args.port).resolve()
        config = output/'printer.cfg'
        config.write_text('[mcu]\nserial: '+str(serial)+'\n'
                          + ('' if args.simulate else 'baud: 250000\nrestart_method: command\n')
                          + '[printer]\nkinematics: none\nmax_velocity: 100\nmax_accel: 1000\n')
        api = directory/'api.sock'
        processes = []
        try:
            with (output/'console.log').open('w') as log:
                if args.simulate:
                    processes.append(subprocess.Popen([str(bridge), str(master)], pass_fds=(master,), stderr=log))
                processes.append(subprocess.Popen([sys.executable, str(args.klippy), str(config),
                    '-a', str(api), '-I', str(directory/'gcode'), '-l', str(output/'klippy.log')],
                    stdout=log, stderr=log))
                wait_state(api, 'ready')
                print('PASS: Klippy Ready ('+('PTY simulation' if args.simulate else 'RPi/Due UART')+')', flush=True)
                time.sleep(3)
                wait_state(api, 'ready')
                try:
                    request(api, 'gcode/script', {'script': 'RESTART'})
                except RuntimeError:
                    pass
                time.sleep(2)
                wait_state(api, 'ready', timeout=25)
                print('PASS: RESTART -> existing configuration/CRC -> Ready', flush=True)
                request(api, 'gcode/script', {'script': 'M112'})
                wait_state(api, 'shutdown')
                print('PASS: emergency stop -> shutdown', flush=True)
                try:
                    request(api, 'gcode/script', {'script': 'FIRMWARE_RESTART'})
                except RuntimeError:
                    pass
                time.sleep(2)
                wait_state(api, 'ready', timeout=25)
                print('PASS: FIRMWARE_RESTART -> config_reset -> Ready', flush=True)
                (output/'result.json').write_text(json.dumps({'ready': True, 'reconnect': True, 'emergency_stop': True,
                    'firmware_restart': True, 'hardware': not args.simulate}, indent=2))
        finally:
            for process in reversed(processes):
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
            if master is not None:
                os.close(master)
                os.close(slave)


if __name__ == '__main__':
    main()
