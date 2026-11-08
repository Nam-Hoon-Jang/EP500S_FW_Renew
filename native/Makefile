# Klipper reference supplies vendor headers only; no Klipper object is linked.

# VENDOR: Main 내부 제조사/CMSIS 헤더 경로.
VENDOR ?= ../vendor

# CC: command for C compiler
CC := arm-none-eabi-gcc

# CXX: command for C++ compiler
CXX := arm-none-eabi-g++

# COMMON: a variable of compile options collection

# -mcpu=cortex-m3: make codes for Cortex-M3, -mthumb: use Thumb command and match Due's Cortex-M3 usage, -Os: optimize to reduce code size, 
# -g3: create information of source debugging & macro, -Wall: activate warning for frequently occuring mistakes, -Wextra: activate additional warnings not included in -Wall,
# -Werror: set build to fail when warning occurrs.
COMMON := -mcpu=cortex-m3 -mthumb -Os -g3 -Wall -Wextra -Werror

# -ffreestanding: complie for no OS and general runtime environment , -fno-builtin: Disable assumptions that named standard functions are compiler built-ins; this does not ban calling functions.
# fdata-sections: divide data components by section, --function-sections: divide function components by section
# section is a logical code/data region placed by the linker. Dividing functions by sections can make it easy to delete functions not used.
COMMON += -ffreestanding -fno-builtin -fdata-sections -ffunction-sections
# sam3x/include: MCU define header by Atmel, cmsis-core: Cortex-M Core define header by ARM
# -I: set the compiler's searching path of header file.
COMMON += -I$(VENDOR)/sam3x/include -I$(VENDOR)/cmsis-core -I. -Ibase -Iklippy
# CFLAGS: compile options for C. Compile C source using 'COMMON' options and GNU C11(standard of C language) mode. gnu11: mode of GNU expansion enabled C11
CFLAGS := $(COMMON) -std=gnu11
# CXXFLAGS: compile options for C++. Compile C++ source using 'COMMON' options ands C++17 mode, 
# rtti: function to confirm real type of a instance while executing. Generally used for typeid and dynamic_cast. Similar to GetType() in C#
CXXFLAGS := $(COMMON) -std=c++17 -fno-exceptions -fno-rtti
CXXFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
# LDFLAGS: Link options
LDFLAGS := -nostdlib -T base/linker.ld -Wl,--gc-sections,-Map=build/uart-echo.map

# -nostdlib로 자동 연결은 막되, 객체/배열 초기화에 필요한 memset 등은 표준 C 라이브러리에서 연결한다.
# C++ 표준 라이브러리/힙/예외 런타임 전체를 연결하는 것이 아니다. 정적 archive에서 참조된 구현만 포함된다.
LDLIBS := -lc -lgcc

# all: basic target of this Makefile
# need build/uart-echo.bin to complete 'all'
all: build/uart-echo.bin

# make output directory
build:
	mkdir -p build

# build/xxx.o: source compile
# [$<: first input file.], [$@: file for build result ], [-c: Do not link and create only .o file], [-o: set the output file name]
# build/startup.o is the path of output file after build done. | build means, the output path 'build' must be ready before build.
# and the command below needs a TAB before writing the commands.
# $(CC) will be transfered by above variable 'CC' and CFLAGS as well. -> $(): the variable inside () will be transfered with above pre-defined variable
# C compile command, using COMMON options, std=gnu11 setting. Only create .o file 
build/startup.o: base/startup.c Makefile | build
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@


# Read Configuration.h byC++ prebuilder.
# Build stops when Invalid value is set in Configuration.h
HOST_PROTOCOL_SELECTED := $(strip $(shell $(CXX) -E -P -x c++ -include Configuration.h host_protocol.in))

ifeq ($(HOST_PROTOCOL_SELECTED), 1)
HOST_SOURCES := klippy/protocol.cpp klippy/klippy_codec.cpp klippy/klippy_commands.cpp
else
$(error Unsupported HOST_PROTOCOL; check Configuration.h)
endif

# 각 소스는 독립 object로 컴파일. -MMD/-MP는 include 의존성 .d를 생성한다.
# 헤더만 수정했을 때도 관련 object가 다시 컴파일되도록 .d를 아래에서 포함한다.
CPP_SOURCES := main.cpp base/board.cpp base/uart.cpp base/mcu_clock.cpp base/mcu_state.cpp base/oid_registry.cpp gpio/gpio.cpp gpio/digital_output.cpp gpio/output_queue.cpp $(HOST_SOURCES)
CPP_OBJECTS := $(patsubst %.cpp,build/%.o,$(CPP_SOURCES))

# 사전 형식/주파수 선언을 변경하면 자동 재생성한다. 일반 재빌드는 기존 헤더를 사용한다.
klippy/klippy_dictionary.h: tools/generate_dictionary.py base/mcu_clock.h base/board.h
	$(PYTHON) tools/generate_dictionary.py

build/%.o: %.cpp Makefile | build
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# 링크는 ELF 생성. 기존 uart-echo 이름은 기존 전송/기록 절차와의 연속성을 위해 유지한다.
build/uart-echo.elf: build/startup.o $(CPP_OBJECTS) base/linker.ld Makefile
	$(CXX) $(COMMON) build/startup.o $(CPP_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
	arm-none-eabi-size $@

build/uart-echo.bin: build/uart-echo.elf
	arm-none-eabi-objcopy -O binary $< $@

-include $(CPP_OBJECTS:.o=.d) build/startup.d

# host-test는 보드 없이 protocol.cpp 자체를 Linux 실행파일로 시험한다.
# firmware와 다른 g++이며 x86/호스트용이다. 이 실행파일은 MCU에 기록하지 않는다.
HOST_CXX ?= g++
HOST_FLAGS := -std=c++17 -O1 -g -Wall -Wextra -Werror -I. -Ibase -Iklippy -fsanitize=address,undefined -fno-omit-frame-pointer
COMMAND_TEST_SOURCES := klippy/protocol.cpp klippy/klippy_codec.cpp klippy/klippy_commands.cpp base/mcu_state.cpp base/oid_registry.cpp gpio/digital_output.cpp gpio/output_queue.cpp gpio/gpio.cpp
build/protocol-test: klippy/protocol.cpp klippy/protocol.h tests/protocol_test.cpp Makefile | build
	$(HOST_CXX) $(HOST_FLAGS) klippy/protocol.cpp tests/protocol_test.cpp -o $@
build/uart-test: base/uart.cpp base/uart.h base/board.h tests/uart_test.cpp tests/fake/sam3x8e.h Makefile | build
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fake base/uart.cpp tests/uart_test.cpp -o $@
build/klippy-codec-test: klippy/klippy_codec.cpp klippy/klippy_codec.h tests/klippy_codec_test.cpp Makefile | build
	$(HOST_CXX) $(HOST_FLAGS) klippy/klippy_codec.cpp tests/klippy_codec_test.cpp -o $@
build/klippy-commands-test: $(COMMAND_TEST_SOURCES) $(wildcard base/*.h klippy/*.h gpio/*.h) tests/fake/sam3x8e.h tests/klippy_commands_test.cpp Makefile | build
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fake $(COMMAND_TEST_SOURCES) tests/klippy_commands_test.cpp -o $@
build/mcu-clock-test: base/mcu_clock.cpp base/mcu_clock.h base/board.h tests/mcu_clock_test.cpp tests/fake/sam3x8e.h Makefile | build
	$(HOST_CXX) $(HOST_FLAGS) -Itests/fake base/mcu_clock.cpp tests/mcu_clock_test.cpp -o $@
host-test: build/protocol-test build/uart-test build/klippy-codec-test build/klippy-commands-test build/mcu-clock-test
	./build/mcu-clock-test
	./build/klippy-commands-test
	./build/klippy-codec-test
	./build/uart-test
	./build/protocol-test
# 플래시는 명시적으로 make flash HOST=user@RPi 실행할 때만 수행한다.
# all/make 기본 빌드에는 포함하지 않는다. 기존 Klippy는 중지하고 기록 후 재시작하지 않는다.
PYTHON ?= python3
HOST ?=
PORT ?= /dev/ttyACM0
flash: all
	@test -n "$(HOST)" || (echo '사용법: make flash HOST=demo-user@192.0.2.1'; exit 1)
	$(PYTHON) tools/flash_remote.py --host "$(HOST)" --port "$(PORT)" --image build/uart-echo.bin

# .PHONY: define a taskname(not filename)
#'all' is a taskname, not a filename.
.PHONY: all host-test flash






