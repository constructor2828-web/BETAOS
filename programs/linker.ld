ENTRY(bex_start)
SECTIONS {
    . = 0;
    .text : { *(.text*) }
    /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) }
}
