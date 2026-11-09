.text
.global exception64
exception64:
    pushq $0
    pushq $255
    jmp exception_common
.macro FAULT number, has_error
.global exception_\number
exception_\number:
.if !\has_error
    pushq $0
.endif
    pushq $\number
    jmp exception_common
.endm
FAULT 0,0
FAULT 1,0
FAULT 2,0
FAULT 3,0
FAULT 4,0
FAULT 5,0
FAULT 6,0
FAULT 7,0
FAULT 8,1
FAULT 9,0
FAULT 10,1
FAULT 11,1
FAULT 12,1
FAULT 13,1
FAULT 14,1
FAULT 15,0
FAULT 16,0
FAULT 17,1
FAULT 18,0
FAULT 19,0
FAULT 20,0
FAULT 21,1
FAULT 22,0
FAULT 23,0
FAULT 24,0
FAULT 25,0
FAULT 26,0
FAULT 27,0
FAULT 28,0
FAULT 29,1
FAULT 30,1
FAULT 31,0
exception_common:
    cli
    cld
    movq %rsp,%rdi
    andq $-16,%rsp
    call exception_report
1:  hlt
    jmp 1b
.section .rodata
.balign 8
.global exception_vectors
exception_vectors:
    .quad exception_0
    .quad exception_1
    .quad exception_2
    .quad exception_3
    .quad exception_4
    .quad exception_5
    .quad exception_6
    .quad exception_7
    .quad exception_8
    .quad exception_9
    .quad exception_10
    .quad exception_11
    .quad exception_12
    .quad exception_13
    .quad exception_14
    .quad exception_15
    .quad exception_16
    .quad exception_17
    .quad exception_18
    .quad exception_19
    .quad exception_20
    .quad exception_21
    .quad exception_22
    .quad exception_23
    .quad exception_24
    .quad exception_25
    .quad exception_26
    .quad exception_27
    .quad exception_28
    .quad exception_29
    .quad exception_30
    .quad exception_31
.section .note.GNU-stack,"",@progbits
.text
.global timer_irq64
timer_irq64:
    pushq %rax
    incq timer_ticks(%rip)
    movb $0x20, %al
    outb %al, $0x20
    popq %rax
    iretq
.text
.global keyboard_irq64, mouse_irq64, spurious7_irq64, spurious15_irq64
keyboard_irq64:
    pushq %rax
    incq input_irq_ticks(%rip)
    movb $0x20,%al
    outb %al,$0x20
    popq %rax
    iretq
mouse_irq64:
    pushq %rax
    incq input_irq_ticks(%rip)
    movb $0x20,%al
    outb %al,$0xa0
    outb %al,$0x20
    popq %rax
    iretq
spurious7_irq64:
    pushq %rax
    movb $0x0b,%al
    outb %al,$0x20
    inb $0x20,%al
    testb $0x80,%al
    jz 1f
    movb $0x20,%al
    outb %al,$0x20
1:  popq %rax
    iretq
spurious15_irq64:
    pushq %rax
    movb $0x0b,%al
    outb %al,$0xa0
    inb $0xa0,%al
    testb $0x80,%al
    jz 2f
    movb $0x20,%al
    outb %al,$0xa0
2:  movb $0x20,%al
    outb %al,$0x20
    popq %rax
    iretq
