.section .text
.global bex_start
bex_start:
    .ascii "BEX1"
    .long payload_end-payload
    .long entry-payload
    .long 1
payload:
entry:
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rdi, %rbx
    leaq title(%rip), %rdi
    call *8(%rbx)
    xorq %r12, %r12
    movq $1, %r13
    movq $12, %r14
loop:
    movq %r12, %rdi
    call *16(%rbx)             /* API.number */
    leaq space(%rip), %rdi
    call *8(%rbx)
    movq %r13, %r15
    addq %r12, %r13
    movq %r15, %r12
    decq %r14
    jnz loop
    leaq newline(%rip), %rdi
    call *8(%rbx)
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    ret
 title: .asciz "Fibonacci / native assembly:\n"
 space: .asciz " "
 newline: .asciz "\n"
payload_end:
.section .note.GNU-stack,"",@progbits
