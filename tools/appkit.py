#!/usr/bin/env python3
"""Create and build a standalone BEX app for copying to a FAT USB drive."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def validate(data):
    if len(data) < 17:
        raise ValueError('BEX header or payload is missing')
    magic, size, entry, abi = struct.unpack_from('<4sIII', data)
    if magic != b'BEX1' or abi != 1 or not 0 < size <= 65536:
        raise ValueError('Unsupported BEX header')
    if len(data) != size + 16 or entry >= size:
        raise ValueError('Invalid BEX size or entry point')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    new = sub.add_parser('new', help='Create a GNU assembler project')
    new.add_argument('directory', type=Path)
    build = sub.add_parser('build', help='Build and validate app.bex')
    build.add_argument('directory', type=Path)
    args = parser.parse_args()
    project = args.directory.resolve()
    if args.command == 'new':
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,26}', project.name):
            parser.error('Use an app name of 1–27 letters, digits or underscores')
        project.mkdir(parents=True, exist_ok=False)
        (project / 'main.S').write_bytes((ROOT / 'programs/hello.S').read_bytes())
        (project / 'README.txt').write_text(
            f'Build: python3 {ROOT}/tools/appkit.py build {project}\n'
            f'Copy {project.name}.bex to a FAT16/FAT32 USB drive.\n'
            f'BETAOS: usb refresh; usb install {project.name}.bex; run {project.name}\n'
            'Run those commands separately. Keep main.S on the build computer.\n'
            'The integrated editor uses Intel syntax; main.S uses GNU AT&T syntax.\n')
        print(f'Created {project / "main.S"}')
        return
    output = project / (project.name + '.bex')
    # Use the same linker layout as the bundled programs, with no host runtime.
    with tempfile.TemporaryDirectory(prefix='beta-app-') as temp:
        obj, binary = Path(temp) / 'app.o', Path(temp) / 'app.bex'
        subprocess.run(['as', '--64', str(project / 'main.S'), '-o', str(obj)], check=True)
        subprocess.run(['ld', '-m', 'elf_x86_64', '-T', str(ROOT / 'programs/linker.ld'),
                        '--oformat', 'binary', str(obj), '-o', str(binary)], check=True)
        data = binary.read_bytes()
        validate(data)
        output.write_bytes(data)
    print(f'{output}: {len(data)} bytes; BEX v1 validated')


if __name__ == '__main__':
    main()
