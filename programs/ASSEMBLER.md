# BETAOS integrated assembler

F6 opens the editor. The initial example is ready to run with **Ctrl-R**.
Ctrl-S saves; Ctrl-Z/Ctrl-Y undo and redo up to 16 changes. Arrows preserve the
column when moving vertically. Home/End act on the line, Ctrl-Home/Ctrl-End on
the document. Before losing unsaved changes, you can save, discard or cancel.
Ctrl-F searches; Enter finds the next match and Esc returns to editing. Clicking
positions the cursor, and Enter preserves indentation.

Ctrl-B assembles and installs using the filename without its extension, such as
`scratch` for `scratch.asm`. Create a file from the desktop context menu and
double-click it to edit. You can also enter `edit example.asm`, then
`asm example.asm app` in Terminal.

The assembler and loader run inside BETAOS. They do not need GCC, NASM, Python,
Linux or another external compiler inside the OS.

```asm
; Native hello world, Intel syntax
bits 64
start:
    push rbx
    mov rbx, rdi
    lea rdi, [rel message]
    call [rbx+8]
    pop rbx
    ret
message: db "Hello from ASM!", 10, 0
```

The result is a BEX v1 executable. `start` is the entry point if present; otherwise
the first payload byte is used. Code and data share one block. Use labels and
RIP-relative addressing for strings.

## Implemented subset

- Registers `rax rcx rdx rbx rsp rbp rsi rdi r8` through `r15`.
- 64-bit `mov REG, REG` and `mov REG, IMMEDIATE`.
- `add`, `sub`, `cmp` between registers or with a signed 32-bit immediate.
- `xor REG, REG`, `inc REG`, `dec REG`, `push REG`, `pop REG`, `nop`, `ret`.
- `lea REG, [rel label]`.
- `jmp`, `jnz`, `jz`, `call` to labels, using signed 32-bit relative offsets.
- `call [rbx+8]`: print a NUL-terminated string (`rdi` points to the text).
- `call [rbx+16]`: print an unsigned 64-bit decimal integer (value in `rdi`).
- `call [rbx+24]`: return boot-time available RAM in MiB in `rax`.
- `db` with comma-separated decimal/hexadecimal numbers and strings. Strings
  support `\n`, `\r`, `\t`, escaped quotes and backslashes.
- `bits 64`, `;` comments, labels, negative numbers and `0x...` hexadecimal.

There are no macros, includes, sections, arithmetic expressions, other register
widths, SIMD instructions or full NASM syntax. Keep `rel` lowercase. Limits are
128 labels, 128 references and lines shorter than 512 characters. The editor
holds 8191 bytes; executable payloads cannot exceed 65536 bytes.

## ABI

On entry, `rdi` points to the API table described in [README.md](README.md).
Preserve `rbx`, `rbp`, `r12`–`r15`. Align the stack to 16 bytes before calling a
function; the example uses `push rbx` on entry and `pop rbx` before `ret`.
Callbacks may change temporary registers, so keep the API table pointer in a
preserved register.

Programs run directly in kernel mode without isolation or a time limit. An
infinite loop, invalid return or invalid memory access can stop the OS. Installed
apps and saved source files persist on an installed BETA disk; otherwise they
remain in RAM only.
