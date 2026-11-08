# EP500S native FW 기반 / Klippy Ready 시험

현재 작업 위치는 `EP500S-FW/Main/native`다. 헤더는 `../vendor`, PC 시험용 Klippy는
`../reference/klippy`를 사용한다. 원래 main_mcu 경로는 필요하지 않다.
최신 독립 빌드·환경 설정은 [Main 안내](../README.md)를 따른다.
아래 초기 단계 설명의 장치 없는 Ready 범위에서 GPIO LED 출력으로 확장 중이다.

현재 목표는 **장치 없는 최소 설정에서 Klippy Ready 연결**이다.
모터·히터·센서·실제 이동 예약은 아직 지원하지 않는다. 시험 Due 단독으로 사용한다.
원본 Klipper 소스/복구 파일과 기존 사용자 학습 주석은 보존한다.

## 코드 분류

```text
native/
  Configuration.h          컴파일할 호스트 방식 선택
  main.cpp                 초기화와 수신 전달
  base/
    startup.c, linker.ld   부팅·벡터·메모리 배치
    board.h/.cpp           12MHz 보드 초기화, 1ms 통신 timeout 시계
    uart.h/.cpp            Due UART, RX ring, 오류·TX 제한 시간
    mcu_clock.h/.cpp       TC0 채널0의 6MHz 시계, 64bit 확장
    mcu_state.h/.cpp       설정·OID 번호 범위·정지 상태
  klippy/
    protocol.h/.cpp        Klippy 호환 프레임·CRC·sequence·ACK/NAK
    klippy_codec.h/.cpp    Klippy VLQ 규칙
    klippy_commands.h/.cpp 명령 해석·공통 기능 호출·응답
    klippy_dictionary.h    자동 생성한 zlib 명령 사전
  examples/ready-test.cfg  장치 없는 최소 Klippy 설정 예제
  tests/                   PC 시험 및 대역
  tools/                   사전 생성·수동 기록·실물 시험 도구
  docs/                    이전 개발 단계 기록
  build/                   ARM/PC 빌드 결과·통합 시험 로그
```

향후 모터 코드는 `motor/`, 히터 코드는 `heater/`처럼 실제 기능 추가 시 분류한다.
공통 기능에서 Klippy 명령 ID/문자열을 참조하지 않는다. Klippy 어댑터에서 공통 기능을 호출한다.
현재 프레임 구조도 Klippy 호환 규칙이므로 `klippy/`에 둔다.

## 컴파일 선택

`Configuration.h`의 `HOST_PROTOCOL`을 Makefile이 전처리해 읽고 선택된 소스만 컴파일한다.
현재 KLIPPY만 구현되어 있으며 CUSTOM/잘못된 값은 빌드 오류로 막는다.
자체 호스트 추가 시 별도 어댑터 소스 목록과 main 연결 분기를 추가한다.
선택값 변경은 재빌드·FW 기록이 필요하다. 실행 중 전환은 지원하지 않는다.

## 지원하는 연결 흐름

1. identify 분할 수신 → zlib JSON 사전 해석.
2. get_uptime/get_clock → CLOCK_FREQ=6000000 기준 시각 동기화.
3. get_config → allocate_oids count=0 → finalize_config crc=호스트값.
4. get_config에서 설정 완료/CRC/정지 여부 확인 → Ready.
5. emergency_stop → 최초 사유를 보존하고 shutdown 응답.
6. 정지 후 config_reset → 설정/OID 초기화, 시계·transport sequence 유지.
7. FIRMWARE_RESTART 후 다시 연결·설정하여 Ready.

설정 CRC는 호스트가 계산한 설정 식별값을 저장·반환한다. MCU에서 펌웨어 CRC를 계산하는 기능이 아니다.
OID 번호 예약과 실제 장치 등록은 다르다. 현재 count>0 예약 후 finalize는 미지원 장치 오류로 정지한다.
이동 큐가 없으므로 실제 move_count는 0이다. Ready를 얻기 위해 가짜 용량을 보고하지 않는다.
설정 후 잘못된 명령은 정지한다. 정지 중에는 identify/시각/설정 조회와 복구·정지 명령만 허용한다.
현재 출력 드라이버가 없으므로 정지는 상태·명령 차단·호스트 알림 범위다. 향후 출력 드라이버의
실제 안전 차단을 공통 shutdown 경로에 연결해야 한다. 생산 장비용 안전 제어 완성을 뜻하지 않는다.
MCU 통계 프레임은 아직 보내지 않는다. STATS_SUMSQ_BASE는 Klippy 초기화 계약용 상수다.

## PC 검증 (WSL native 폴더)

```bash
make
python3 check_build.py
make host-test
python3 tools/identify_smoke.py --local
python3 tests/check_klippy_codec.py
```

생성기 변경 시 Makefile이 사전을 재생성한다. 수동 실행도 가능하다.

```bash
python3 tools/generate_dictionary.py
```

호스트 시험은 ASan/UBSan으로 명령·설정·정지·복구·프레임·UART 대역·시계 대역을 검사한다.
시계 시험은 실제 mcu_clock.cpp를 fake MMIO로 실행해 overflow·pending IRQ 중복 집계·PRIMASK를 검사한다.
check_build.py는 실제 ARM BIN의 Reset/SysTick/UART/TC0 벡터를 검사한다.

실제 Klippy 프로세스와 제품 C++ 프레임/명령/상태를 연결하는 PTY 통합 시험:

```bash
# cffi, greenlet, pyserial, jinja2가 설치된 Python 환경에서
python3 tests/test_klippy_ready.py
```

Ready 유지·RESTART 재접속·M112 정지·FIRMWARE_RESTART 후 Ready 복귀를 검사한다.
시계/UART는 PC 대역이므로 실제 Due의 타이밍·전기적 통신 검증과 구분한다.
결과: `build/ready-integration/result.json`, `klippy.log`, `console.log`.

## 실물 시험: 사용자가 기록·실행

PC WSL에서:

```bash
make flash HOST=demo-user@192.0.2.1
scp tools/ready_smoke.py tools/identify_smoke.py tools/command_smoke.py tools/frame_smoke.py demo-user@192.0.2.1:~/
```

기존 flash 도구는 명시 실행 시에만 전송·기록하며 Klippy는 중지 상태로 남긴다.
RPi에서 먼저 기존 세 시험을 각각 실행한다. 매번 시작 후 10초 안에 Due RESET, ERASE 금지.

```bash
~/klippy-env/bin/python ~/identify_smoke.py --port /dev/ttyACM0
~/klippy-env/bin/python ~/command_smoke.py --port /dev/ttyACM0
~/klippy-env/bin/python ~/frame_smoke.py --port /dev/ttyACM0
```

그 뒤 Ready 시험:

```bash
~/klippy-env/bin/python ~/ready_smoke.py --port /dev/ttyACM0
```

이 시험은 RPi의 `~/klipper/klippy/klippy.py`를 독립 프로세스로 실행한다.
기존 printer.cfg·systemd 서비스 설정을 변경하지 않는다. 포트 점유 시 실행을 거부한다.
자체 최소 설정으로 Ready → RESTART → Ready → M112 → FIRMWARE_RESTART → Ready를 확인한 뒤
시험 프로세스를 종료한다. 따라서 시험 완료 후 Mainsail 서비스가 Ready로 유지되는 방식은 아니다.
이번 시험은 RESET 안내가 없다. 먼저 새 FW를 기록하고 기존 시험이 종료된 상태에서 실행한다.
다른 Klippy 경로는 `--klippy /경로/klippy.py`로 지정한다.
결과는 RPi 현재 폴더의 `ready-results/`에 저장된다. 실패하면 그 안의 로그를 확인한다.
생산용 printer.cfg로 전환하거나 Klippy 서비스를 자동 재시작하지 않는다.

## 검증 범위

PC에서 ARM 빌드·호스트 시험·실제 로컬 Klippy의 PTY Ready/정지/복구 확인.
새 Ready 버전의 실제 RPi/Due 기록·시험은 사용자 확인 전까지 미검증이다.
RPi Klippy 커밋과 PC 참조 커밋은 다를 수 있으므로 실물 결과를 별도로 확인한다.
장시간 부하·최대 처리량·물리 오류 주입·모터/히터 안전 검증은 아직 완료하지 않았다.

이전 학습 설명은 [개발 단계 기록](docs/development-notes.ko.md)에 보존했다.
