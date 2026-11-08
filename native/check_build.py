"""Run after make: inspect the actual ARM binary's boot addresses."""
from pathlib import Path
import struct
import subprocess

binary = (Path(__file__).parent / 'build/uart-echo.bin').read_bytes()
assert len(binary) >= 61 * 4, 'Vector table missing'
stack, reset = struct.unpack_from('<II', binary)
assert stack == 0x20010000, 'Initial stack not at SRAM0 top'
assert reset & 1, 'Reset vector must have Cortex-M Thumb bit'
assert 0x80000 <= (reset & ~1) < 0x80000 + len(binary), 'Reset outside image'
assert len(binary) <= 512 * 1024, 'Image exceeds flash capacity'
elf = Path(__file__).parent / 'build/uart-echo.elf'
symbols = {}
for line in subprocess.check_output(['arm-none-eabi-nm', '--defined-only', str(elf)], text=True).splitlines():
    parts = line.split()
    if len(parts) == 3:
        symbols[parts[2]] = int(parts[0], 16)
# 새 IRQ를 구현해도 벡터 연결을 빠뜨리면 Default_Handler에 갇힌다. 실제 BIN의 주소를 함께 검사.
for index, name in [(15, 'SysTick_Handler'), (24, 'UART_Handler'), (43, 'TC0_Handler')]:
    address = struct.unpack_from('<I', binary, index * 4)[0]
    assert address & 1, f'{name}: Thumb bit missing'
    assert (address & ~1) == (symbols[name] & ~1), f'{name}: vector not connected'
print(f'PASS: {len(binary)} bytes; stack={stack:#x}, reset={reset:#x}')
