ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x00080000, LENGTH = 512K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
}
SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .data : {
        . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : {
        . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .;
    } > RAM
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
