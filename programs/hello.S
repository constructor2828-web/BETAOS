/* BEX v1, position-independent x86-64. RDI points to the BETAOS API. */
.section .text
.global bex_start
bex_start:
    .ascii "BEX1"
    .long payload_end-payload
    .long entry-payload
    .long 1
payload:
entry:
    pushq %rbx                 /* SysV stack is aligned before API calls. */
    movq %rdi, %rbx
    leaq message(%rip), %rdi
    call *8(%rbx)              /* API.print */
    popq %rbx
    ret
message:
    .asciz "Hello from a native x86-64 assembly program!\nThis is machine code executing directly on the CPU.\n"
payload_end:
.section .note.GNU-stack,"",@progbits
