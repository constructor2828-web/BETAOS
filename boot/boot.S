.code16
.section .text
.global start
start:
    ljmp $0, $normalized
normalized:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %ss
    movw $0x7c00, %sp
    sti
    movb %dl, drive
    movw $0x1000, %ax
    movw %ax, %es
    xorw %bx, %bx
    movw $96, %di
    movb $0, cylinder
    movb $0, head
    movb $2, sector
read_sector:
    movw $3, %si
retry:
    movb $2, %ah
    movb $1, %al
    movb cylinder, %ch
    movb sector, %cl
    movb head, %dh
    movb drive, %dl
    int $0x13
    jnc loaded
    xorw %ax, %ax
    movb drive, %dl
    int $0x13
    decw %si
    jnz retry
    movw $error, %si
print_error:
    lodsb
    testb %al, %al
    jz stopped
    movb $0x0e, %ah
    int $0x10
    jmp print_error
stopped:
    cli
    hlt
    jmp stopped
loaded:
    movw %es, %ax
    addw $0x20, %ax
    movw %ax, %es
    incb sector
    cmpb $19, sector
    jne next
    movb $1, sector
    xorb $1, head
    cmpb $0, head
    jne next
    incb cylinder
next:
    decw %di
    jnz read_sector
    cli
    inb $0x92, %al
    orb $2, %al
    outb %al, $0x92
    lgdt gdt_descriptor
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $protected
.code32
protected:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x90000, %esp
    cld
    movl $0x10000, %eax
    jmp *%eax
.balign 8
gdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_descriptor:
    .word 23
    .long gdt
drive: .byte 0
cylinder: .byte 0
head: .byte 0
sector: .byte 2
error: .asciz "BETAOS disk read error"
.org 510
.word 0xaa55
.section .note.GNU-stack,"",@progbits
