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
    movq %rdi,%rbx
    leaq title(%rip),%rdi
    call *8(%rbx)
    call *24(%rbx)             /* API.memory_mib -> RAX */
    movq %rax,%rdi
    call *16(%rbx)
    leaq suffix(%rip),%rdi
    call *8(%rbx)
    popq %rbx
    ret
 title: .asciz "Native program / available memory: "
 suffix: .asciz " MiB\n"
payload_end:
.section .note.GNU-stack,"",@progbits
