ENTRY(_start)
SECTIONS {
    . = 0x10000;
    .text : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(16);
    __bss_start = .;
    .bss : { *(COMMON) *(.bss*) }
    __bss_end = .;
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
