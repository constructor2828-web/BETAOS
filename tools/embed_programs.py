"""Embed native programs and generate their registry; the guest needs no disk driver."""
import pathlib
import re
import sys

output = pathlib.Path(sys.argv[1])
programs = [pathlib.Path(arg) for arg in sys.argv[2:]]
assembly = ['.section .rodata']
header = []
entries = []
for program in programs:
    name = program.stem
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name):
        raise SystemExit(f'Invalid assembly program name: {name}')
    label = f'bex_{name}'
    assembly += [f'.global {label}, {label}_end', f'{label}:',
                 f'.incbin "{program.as_posix()}"', f'{label}_end:']
    header.append(f'extern const u8 {label}[], {label}_end[];')
    entries.append(f'{{"{name}", {label}, {label}_end}}')
assembly.append('.section .note.GNU-stack,"",@progbits')
header += ['static const struct {const char *name; const u8 *start, *end;} builtins[] = {',
           ',\n'.join(entries), '};']
(output / 'program_blobs.S').write_text('\n'.join(assembly) + '\n')
(output / 'program_catalog.h').write_text('\n'.join(header) + '\n')
