# EP500S Main MCU 자체 FW

이 폴더가 새 작업 루트다. `main_mcu`의 소스/헤더를 참조하지 않고 빌드한다.
기존 폴더는 이번 이관에서 삭제·변경하지 않았다. Git 초기화/커밋/원격 업로드도 하지 않았다.

```text
Main/
  native/                  사용자 작성 FW·빌드·시험·업로드 도구
  vendor/sam3x/include/    Atmel SAM3X 제조사 헤더
  vendor/cmsis-core/       ARM CMSIS 코어 헤더
  reference/klippy/       호환 시험용 기존 Klippy 소스 사본
  reference/lib/hub-ctrl/ 참조 Klippy의 선택적 USB 재시작 보조 소스
  reference/COPYING       참조 코드의 원본 라이선스
  .vscode/                Main을 열었을 때의 WSL ARM IntelliSense 설정
```

`native/klippy`는 자체 FW의 Klippy 통신 어댑터다. `reference/klippy`는 PC 시험에서 실행하는 기존
호스트 프로그램이며 FW에 컴파일/링크하지 않는다. vendor와 reference의 저작권·라이선스 주석은 보존했다.
복사 파일 해시는 migration-manifest.json에 기록했다. 이 해시는 최초 복사본 기준이며 이후 개발 변경은 별도다.

## 빌드 / 검사

VS Code에서는 이 Main 폴더를 WSL로 연다. WSL Ubuntu에서:

```bash
cd /mnt/c/Git/Lincsolution/EP500S/EP500S-FW/Main/native
make
python3 check_build.py
make host-test
python3 tools/identify_smoke.py --local
python3 tests/check_klippy_codec.py
python3 tests/test_flash_tool.py
```

ARM 빌드에는 arm-none-eabi-gcc/g++, binutils, newlib 및 make가 필요하다.
PC 시험에는 g++와 Python3가 필요하다. `make host-test`는 실제 GPIO/시계 대신 MMIO 대역을 사용한다.
make의 VENDOR 기본값은 `../vendor`다. 제조사 헤더 위치를 바꿀 때만 `make VENDOR=경로`로 지정한다.

실제 Klippy 프로세스와 C++ FW 명령 계층의 PTY 통합 시험:

```bash
python3 -m venv ../.venv
../.venv/bin/pip install -r ../requirements-test.txt
../.venv/bin/python tests/test_klippy_ready.py
```

참조 소스는 `../reference/klippy`에서 가져온다. 실제 RPi에서는 기존 방식대로
`~/klipper/klippy`를 사용하며, 다른 설치 위치는 ready_smoke의 `--klippy`로 지정한다.
이는 의도한 원격 설치 경로이며 PC의 기존 main_mcu 의존성이 아니다.

## 업로드

```bash
# Main/native에서 사용자 명시 실행 시에만 장비 기록
make flash HOST=demo-user@192.0.2.1
```

이관 검증에서는 실제 장비 전송/기록을 실행하지 않았다. flash 도구는 Main의 BIN을 전송한다.
RPi의 SSH/sudo 권한, klippy-env의 pyserial, bossac와 실제 포트가 필요하다.
자세한 실물 시험은 native/README.ko.md를 참고한다.

## 현재 범위 / 보존 자료

GPIO LED의 OID 등록·즉시/예약 출력 코드는 포함한다. 정밀 타이머는 아직 개발 중이며
precision_timer.h의 선언만으로 예약 실행 구현이 완료된 것은 아니다.
이관 검증을 위해 오래된 PC 시험 소스 목록·대역·큐 용량 기대값을 현재 코드에 맞췄다.
FW 동작 소스 자체는 원본과 동일하게 보존했다.

기존 native/build는 경로가 박힌 .d/ELF를 재사용하지 않도록 native/build.pre-migration에 보관했다.
새 native/build는 Main에서 다시 생성했다. 둘 다 Git에서 제외한다.
기존 main_mcu/recovery와 이전 Git 이력은 자동 이전하지 않았다. 원본 정리 전 별도 보존한다.
main_mcu/src의 원본 Klipper FW 빌드용 scripts/test_klippy.py·test/config·out은 자체 native FW 시험에
사용하지 않아 복사하지 않았다. 재현에 필요한 현재 native의 시험 도구와 참조 호스트는 포함했다.
