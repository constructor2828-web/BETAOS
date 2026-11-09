"""Pack a BIOS boot sector and flat kernel into a 1.44 MB floppy image."""
import pathlib
import sys

boot, kernel, output = map(pathlib.Path, sys.argv[1:])
image = bytearray(1474560)
image[:512] = boot.read_bytes()
payload = kernel.read_bytes()
image[512:512 + len(payload)] = payload
output.write_bytes(image)
