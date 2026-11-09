# Native assembly programs

BETAOS executes these programs directly as x86-64 machine code. The loader
validates a BEX header, copies the payload into a 64 KiB executable arena,
and calls its entry point. These are synchronous kernel-mode programs, not
isolated user processes: a program that faults or fails to return can stop the OS.
Bundled packages can be installed through the Apps ASM window. The integrated
assembler can also build and install new programs entirely inside BETAOS. Installed
BEX binaries persist in the BETA state partition when a BETA disk is mounted.
External BEX applications can be imported from FAT16/FAT32 USB drives. See
[the App Kit](../docs/APPKIT.md) for creating, building and installing them.

In the BETAOS terminal:

```text
programs
run hello
install fib
run fib
install memory
run memory
```

The desktop's **Programa ASM** shortcut also launches `hello`.

To write your own:

1. Copy `programs/hello.S` to `programs/myapp.S`.
2. Edit its assembly code and strings; keep the BEX header and labels.
3. Run `make iso` on the build machine. The build automatically discovers the new
   source, assembles it, generates its registry entry, and includes it in the ISO.
4. Boot the rebuilt ISO, enter `install myapp`, then `run myapp`.

Use names containing letters, digits and underscores, starting with a letter or
underscore. After removing or renaming a program, run `make clean && make iso`
to clear the previous embedded registry and ISO staging files.

## BEX v1 format

All integers are little endian. The file begins with a 16-byte header:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 4 | ASCII `BEX1` |
| 4 | 4 | Payload size, 1–65536 bytes |
| 8 | 4 | Entry offset relative to payload start |
| 12 | 4 | ABI version, currently 1 |
| 16 | remaining | Position-independent executable payload |

Keep code and string literals in `.text`, before `payload_end`, as the examples
do. Use RIP-relative addresses for local data; the loader does not apply
relocations. The examples use GNU assembler syntax and need no C runtime.

## Calling convention and OS API

The entry function follows the System V AMD64 ABI. `RDI` points to this structure:

```c
struct ProgramApi {
    uint64_t version;                 // byte offset 0, currently 1
    void (*print)(const char *);       // byte offset 8, NUL-terminated string
    void (*number)(uint64_t);         // byte offset 16, decimal output
    uint64_t (*memory_mib)(void);      // byte offset 24, available RAM at boot
};
```

Pass arguments in `RDI`; API return values arrive in `RAX`. Preserve `RBX`, `RBP`,
`R12`–`R15` and `RSP`; other general registers may be changed by API calls. Align
`RSP` to 16 bytes **before each call**; at entry, `RSP % 16 == 8`. Do not depend
on `RDI` surviving a call: save the API pointer in a preserved register. Use
`ret` to return to the terminal. The API pointer is valid only for that invocation.

The arena is reused for each invocation, so programs do not retain writable state
across runs. There is no dynamic linker, syscall instruction interface, scheduling,
or privilege boundary yet. Firmware boot services have already been exited;
programs call BETAOS API functions, not firmware services.

For the native editor and the in-guest Intel-syntax assembler, see [ASSEMBLER.md](ASSEMBLER.md).
