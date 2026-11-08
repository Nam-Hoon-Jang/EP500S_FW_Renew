# 이전 개발 단계 기록 — UART/프로토콜/identify

이 문서는 과거 단계별 설명 보존본이다. 현재 폴더·지원 범위·시험 절차는 루트 README.ko.md를 따른다.

기존 단일 main.cpp는 백업 후 보드·UART·프레임 처리로 분리했다.
현재 범위는 UART·프레임·VLQ·진단 명령 처리다. Klippy Ready에 필요한 identify/명령 사전은 다음 단계다.
이 BIN은 모터/히터가 연결되지 않은 시험 Due에만 사용한다. 원본 Klipper 복구 BIN은 유지한다.

## 읽을 순서

1. `main.cpp`: 보드 초기화 → 지역 DueUart 생성 → Protocol 생성 → 수신 전달/timeout 확인.
2. `uart.h`/`uart.cpp`: PA8/9와 UART 설정, RX 인터럽트, ring buffer, 오류 전달, TX/flush.
3. `protocol.h`/`protocol.cpp`: 프레임 저장, CRC/prefix/sequence 검사, ACK/NAK, 재동기화.
4. `board.h`/`board.cpp`: 외부12MHz와 1ms SysTick. 초기화 실패는 false로 알린다.
5. `startup.c`: RAM 초기화, SysTick/UART 벡터 연결, main 진입.
6. `Makefile`: 각 .cpp를 .o로 만들고 링크. .d 파일로 헤더 변경도 추적.

## 클래스와 C# 연결

- `class DueUart final`: SAM3X 하드웨어 UART 한 개를 소유한다. 제조사 헤더에 이미 `Uart` 타입이 있어 이름을 구분했다. final은 상속 대상이 아니라는 뜻.
- `public`/`private`: 외부에서 사용할 함수와 내부 상태 구분. 접미사 `_`는 멤버라는 이름 관례이지 특별한 C++ 문법은 아니다.
- `enum class ReadResult`: Empty/Byte/Error를 구분한다. bool 하나보다 UART 오류와 단순 수신 없음의 차이를 보존한다.
- `Protocol::Send`: 함수 포인터. C# delegate와 비슷한 용도로 송신 동작을 전달하지만 객체 주소는 `void* context`로 따로 전달한다.
- `Protocol protocol(send_bytes, &uart)`: 생성자로 송신 함수와 UART 객체 주소를 연결. 테스트에서는 UART 대신 수신 결과를 모으는 함수를 전달한다.
- `static_cast<DueUart*>(context)`: 실제로 DueUart 주소를 전달했다는 계약에 따라 타입을 복원. 임의 포인터를 안전하게 검사해주는 동작은 아니다.
- 지역 객체는 main 실행 중 생성되므로 현재 startup에 전역 동적 생성자 실행 기능을 추가할 필요가 없다. 객체는 무한 main 루프 동안 살아있다.

## UART 흐름

1. PMC의 PIOA/UART 클록 활성화. PA8 RX·PA9 TX를 Peripheral A로 선택하고 GPIO 소유권 해제.
2. PDC/DMA 해제, 수신/송신/오류 상태 초기화. MCK/(16*baud)의 정확한 분주값만 허용.
3. NVIC와 UART RX/error 인터럽트 활성화. 벡터의 UART_Handler가 현재 DueUart 객체로 전달.
4. ISR은 수신 바이트를 최대255바이트 ring에 저장한다. CRC/명령 실행/송신은 ISR에서 하지 않는다.
5. 메인에서 바이트를 꺼내 Protocol에 전달. overrun/frame/parity/ring overflow는 별도 Error로 알린다.
6. TX는 최대100ms 제한의 polling. 기다리는 동안 RX ISR은 동작하므로 송신 대기로 수신을 막지 않는다.
7. write 성공은 THR 전달 완료이며 마지막 stop bit 완료가 아니다. 반드시 전송 완료를 기다릴 때만 flush(TXEMPTY)를 호출한다.

`volatile`은 접근 생략 방지일 뿐 일반적인 동기화 해법이 아니다. ISR/메인 공유값은 짧은 PRIMASK 보호 구간과 단일 생산자/소비자 규칙으로 다룬다. 이 드라이버는 인터럽트 안이나 인터럽트를 끈 상태의 blocking TX를 거부한다.

## 프레임 및 기존 시험과 달라진 점

`[전체 길이][0x10 | 순서번호][payload][CRC상위][CRC하위][0x7E]`, 전체5~64바이트.
CRC는 initFFFF/reflected8408/xorout0이며 앞부분(length/sequence/payload)만 계산한다.

- 정상 프레임은 다음 기대 번호를 증가시키고 빈5바이트 ACK를 보낸다.
- 중복/순서 불일치는 명령 callback을 다시 호출하지 않고 현재 기대 번호를 알린다.
- 길이/prefix/종료/CRC 오류도 현재 기대 번호를 알리는 NAK를 한 번 보낸다. 연속 잡음에는 반복하지 않는다. 프레임 앞 추가0x7E는 조용히 무시한다.
- 따라서 예전의 '잘못된 프레임은 무조건 응답 없음' 스크립트는 현재 동작과 맞지 않는다. 새 `tools/frame_smoke.py`를 사용한다.
- 정상 프레임 내부0x7E는 허용. 경계를 잃은 경우에만 첫0x7E까지 버린다. 다음 정상 프레임 일부가 손실되면 호스트 재전송으로 복구한다.
- 부분 프레임에1초 동안 추가 바이트가 없으면 상태를 버리고 경계를 찾는다. 1초는 프로젝트 정책으로 Klipper wire 규격의 상수가 아니다.
- send_payload는 응답 프레임을 생성한다. main이 등록한 KlippyCommands 콜백에서 명령 ID/인수를 해석한다.
- ACK는 물리 동작 완료/안전 실행 보장이 아니다. KlippyCodec으로 진단 명령을 해석하며 명령 사전·타이머·OID·설정·정지 명령은 다음 단계다.

프레임 규칙 참고: https://www.klipper3d.org/Protocol.html
기존 GPL 실행 소스를 복사/링크하지 않고 현재 학습 코드를 분리·확장했다. 레지스터 헤더의 기존 저작권/고지는 유지한다. 라이선스 적법성/클린룸 인증을 뜻하지 않는다.

## 빌드와 PC 시험 (WSL Ubuntu)

```bash
cd /mnt/c/Git/Lincsolution/EP500S/EP500S-FW/Main/native
make
python3 check_build.py
make host-test
```

`host-test`는 실제 protocol.cpp를 PC용 g++로 컴파일하여 ASan/UBSan과 함께 실행한다.
기존 CRC 알려진 값, 정상/중복/누락/순환, 최대/최소 프레임, payload sync, CRC/길이/prefix/종료 오류,
재동기화, 부분 timeout, 밀리초 순환, RX 오류 통보, TX 실패 후 중복 방지, 고정 seed 잡음50,000바이트를 검사한다.
UART 레지스터/핀/IRQ 타이밍은 PC 시험으로 확인되지 않으므로 Due에서 회귀 시험해야 한다.

같은 `host-test`는 uart.cpp도 시험용 MMIO 대역과 연결해 ring 순환/용량 초과/오류 전달/
TX timeout/flush/IRQ 문맥 차단을 검사한다. 가상 레지스터 대역이므로 실제 MCU 동작 검증을 대신하지 않는다.
`check_build.py`는 BIN의 SysTick/UART 벡터 주소와 ELF의 실제 처리 함수 주소까지 비교한다.

## RPi 실물 회귀 시험

기존처럼 BIN을 전송하고 Klippy를 중지한 뒤1200baud 진입/기록/검증한다. 소스 분리만으로 RPi에 자동 기록되지는 않는다.
WSL의native에서 시험 스크립트도 전송한다:

```bash
scp build/uart-echo.bin tools/frame_smoke.py demo-user@192.0.2.1:~/
```

기록 성공 후 RPi SSH:

```bash
~/klippy-env/bin/python ~/frame_smoke.py --port /dev/ttyACM0
```

10초 안에 RESET을 누른다(ERASE 아님). 이 스크립트 자체는 flash/서비스 설정을 바꾸지 않는다.
실물 UART 오류/수신 폭주/송신 timeout/온도·부하 등의 시험은 별도다.

## 의도적인 한계

- UART는 현재 Due Programming Port의 하드웨어 하나만 지원. TX는 bounded polling, RX는 IRQ. DMA/RTOS/CAN 등은 추가하지 않았다.
- CPU는 현재12MHz. 84MHz 성능 시험이나 정밀 STEP 타이머는 아직 구현하지 않았다.
- watchdog은 개발 단계에서 해제하며 Default_Handler는 정지 루프다. 제품의 안전 차단/재부팅 정책을 대신하지 않는다.
- 전역 C++ 동적 생성자·예외·RTTI·힙을 쓰지 않는다. 객체 초기화에 필요한 memset 등의 표준 C 함수만 명시 연결한다.
- 프레임 완성은 Klippy Ready 호환 완료가 아니다. command dictionary/identify/정밀 시각/설정/안전명령 계층이 더 필요하다.

## 한 명령으로 플래시 (명시적으로 실행할 때만)

WSL native에서:

```bash
make flash HOST=demo-user@192.0.2.1
```

빌드 → BIN 전송 → SHA256 검증 → 대상장치/도구 확인 → Klippy 중지 → 포트점유 확인 →
1200baud 부트로더 진입 → bossac 장치정보 확인 → 지우기/기록/검증/리셋 순서다.
전송 전/후 해시 불일치이면 FW를 지우지 않는다. 진입/기록 실패 시 오류를 반환하고 자동 지우기를 반복하지 않는다.
로그와 파일에 암호를 저장하지 않으며 SSH/SCP/sudo 입력은 정상 터미널을 사용한다.
이미지 전송은 Linux/WSL 경로를 사용해 Windows C:를 원격 호스트로 해석하는 문제를 피한다.
기존 복구 BIN은 건드리지 않고 성공한 작업의 /tmp 전송본만 삭제한다.
RPi의 홈 경로는 /home/사용자명/klippy-env 구성을 전제한다.
이 도구를 실행하면 현재 FW가 교체된다. actuator 없는 Due 단독 시험에만 사용한다.
이전 UART·프레임 버전은 사용자 제공 화면에서 실물 PASS를 확인했다. 이번 명령 처리 버전의 실물 시험은 아직 하지 않았다.

```bash
python3 tests/test_flash_tool.py
```

플래시 도구의 SHA256 불일치/포트 부재/포트 점유/타깃 불일치 중단 조건은 mock 시험5개로 검사한다.
이 시험은 실제 SSH나 MCU 지우기/기록을 실행하지 않는다.

## Klippy 전용 VLQ 모듈

`klippy_codec.h/.cpp`, namespace KlippyCodec. 현재 Klippy 시험 호환용이며, 자체 호스트로 대체할 때
제품용 인코딩 규칙을 별도로 설계·확정해야 한다는 설명을 두 파일 상단에 명시했다.
기존 규칙을 유지할지 범용 varint/고정 길이/자체 규칙으로 교체할지도 그때 결정한다.

- decode_u32/decode_i32: 최대5byte의 signed VLQ 규칙으로32bit 값을 복원한다.
- encode_u32/encode_i32: 최소 길이로 인코딩. 공간 부족이면 출력 버퍼를 부분 변경하지 않는다.
- 실패 시 결과변수/소비길이를 진행시키지 않는다. NeedMoreData/TooLong/Overflow/BufferTooSmall 등을 구분한다.
- Protocol의 payload 수신 콜백에서 KlippyCommands가 이 모듈로 명령 ID와 인수를 해석한다.
- make host-test로 경계·오류·40,000개 roundtrip을검사한다.
- python3 tests/check_klippy_codec.py로 지정된 로컬 Klippy msgproto.py와10,049개 값을교차검사한다.
  참조 Python은 시험에만사용하며 MCU FW에포함하지않는다. RPi의실행커밋을직접검사한것은아니다.


## 명령 ID 해석과 처리 함수 연결

`klippy_commands.h/.cpp`의 KlippyCommands가 명령 계층을 담당한다.
main은 객체를 만들고 Protocol 수신 콜백에 연결한다. UART/프레임 코드는 명령을 직접 해석하지 않는다.

흐름: 프레임 검증 → VLQ 명령 ID 읽기 → 정의 표에서 인수 타입/개수와 함수 포인터 찾기 →
인수 해석 → 전체 payload 검증 → 처리 함수 실행 → 응답 payload 송신 → 프레임 ACK.
같은 payload에 명령 여러 개를 연속 배치할 수 있다. 뒤 명령의 형식이 잘못되면 앞 명령도 실행하지 않는다.
이 사전 검증은 향후 모터 명령의 실행 취소나 하드웨어 동작의 원자성까지 보장하지 않는다.

| ID | 형식 | 동작 |
|---|---|---|
| 2 | debug_ping value=%u | ID 4와 같은 unsigned32 값을 응답 |
| 3 | debug_sum a=%i b=%i | ID 5와 signed32 합계를 응답 |
| 6 | debug_command_error code=%c offset=%u command_id=%u | 명령 오류 응답 |

2~6은 프로젝트 진단용 임시 번호다. 기존 Klippy가 이미 아는 명령 번호가 아니다.
identify(1)/identify_response(0), 압축 명령 사전을 지원한다. 아직 Klippy Ready 연결은 불가능하다.
오류 코드는 unknown=1, 잘린 정수=2, 5byte 초과=3, 정수 범위 초과=4, 덧셈 범위 초과=5, 잘못된 인수=6이다.
offset은 payload 시작 기준 byte 위치다. ID 자체를 읽지 못하면 command_id=0으로 보고한다.
유효한 프레임의 명령 오류는 sequence를 되돌리지 않는다. 오류 응답과 ACK는 구분되며,
중복 프레임은 ACK만 재전송하고 처리 함수를 다시 실행하지 않는다.

`make host-test`는 실제 Protocol/codec/commands 연결 상태에서 정상·중복·다중 명령·잘린 입력·
알 수 없는 ID·정수 및 덧셈 overflow·실행 전 전체 검증을 검사한다.

수동 실물 시험은 새 BIN을 기록한 뒤 다음 순서로 실행한다. 이번 구현에서는 보드에 기록하지 않았다.

```bash
# PC의 WSL native에서
make flash HOST=demo-user@192.0.2.1
scp tools/command_smoke.py tools/frame_smoke.py demo-user@192.0.2.1:~/
# RPi SSH에서, 각각 시작 후 10초 안에 Due RESET 누르기
~/klippy-env/bin/python ~/command_smoke.py --port /dev/ttyACM0
~/klippy-env/bin/python ~/frame_smoke.py --port /dev/ttyACM0
```

command_smoke는 RPi에 설치된 Klippy의 msgproto 인코딩으로 요청/응답 값을 비교한다.
frame_smoke는 응답 프레임 CRC를 검사하면서 명령 응답을 분리하고 빈 ACK만 비교한다.
스크립트 자체는 플래시/서비스 설정을 변경하지 않는다.

## identify / 명령 사전

`identify offset=%u count=%c` 요청에 `identify_response offset=%u data=%.*s`로 응답한다.
사전은 zlib 압축 JSON 209byte이며 `tools/generate_dictionary.py`에서 생성한다.
명령 형식을 변경하면 생성기를 수정하고 재실행해야 한다. 생성된 `klippy_dictionary.h`를 보관하므로
일반 ARM 빌드에는 Python/zlib가 필요 없다. MCU는 압축을 풀지 않고 flash의 bytes를 반환한다.

count는 VLQ unsigned8(0~255), data는 raw 1byte 길이 + bytes다. 요청 count/남은 사전/
프레임 공간 중 최솟값만 반환한다. 끝 또는 범위 밖 offset, count=0은 빈 data를 반환한다.
응답 offset은 요청값 그대로다. 중복 transport 프레임은 기존 정책대로 ACK만 반환한다.
호스트는 응답 data 길이만큼 offset을 증가시키고 빈 응답에서 종료한다.

PC 검증: `make`, `python3 check_build.py`, `make host-test`,
`python3 tools/identify_smoke.py --local`. 로컬 Klippy 파서로 압축 사전·명령 ID/응답 형식을 검사한다.

실물은 사용자가 새 BIN을 기록한 뒤 아래 스크립트를 전송·실행한다.

```bash
scp tools/identify_smoke.py demo-user@192.0.2.1:~/
# RPi: 실행 후 10초 안에 Due RESET
~/klippy-env/bin/python ~/identify_smoke.py --port /dev/ttyACM0
```

count=1/40/255 사전 복원, CRC/ACK, 중복 요청, 끝/범위 밖 offset, count=0을 검사한다.
현재 버전은 PC 검증만 완료. 실제 기록/실물 시험은 아직 하지 않았다.
identify만으로 시각 동기화·설정·OID·정지 명령이 생기지 않으며 Klippy Ready는 다음 단계다.

