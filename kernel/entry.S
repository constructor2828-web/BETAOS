.section .text.start,"ax"
.global _start
_start:
    cld
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb
    call kernel_main
1:  cli
    hlt
    jmp 1b
.section .text
.global irq_timer, irq_keyboard, exception_stub
irq_timer:
    pushal
    cld
    call timer_handler
    popal
    iret
irq_keyboard:
    pushal
    cld
    call keyboard_handler
    popal
    iret
exception_stub:
    cli
    cld
    call panic
2:  hlt
    jmp 2b
.section .note.GNU-stack,"",@progbits
