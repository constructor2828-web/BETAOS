"""Validate the guest installer's actual on-disk GPT, FAT and state data."""
import binascii
import pathlib
import struct

root = pathlib.Path(__file__).resolve().parents[1]
image = (root / 'build/installed-os.img').read_bytes()
last = len(image) // 512 - 1
for lba in (1, last):
    header = bytearray(image[lba * 512:(lba + 1) * 512])
    assert header[:8] == b'EFI PART'
    expected = struct.unpack_from('<I', header, 16)[0]
    header[16:20] = bytes(4)
    assert binascii.crc32(header[:92]) == expected
    table, count, size, crc = struct.unpack_from('<QIII', header, 72)
    entries = image[table * 512:table * 512 + count * size]
    assert binascii.crc32(entries) == crc
    assert struct.unpack_from('<QQ', entries, 32) == (2048, 133119)
assert image[2048 * 512 + 82:2048 * 512 + 90] == b'FAT32   '
boot_entry = image[4130 * 512 + 64:4130 * 512 + 96]
assert boot_entry[:11] == b'BOOTX64 EFI'
cluster, length = struct.unpack_from('<HI', boot_entry, 26)
assert cluster == 5
expected = (root / 'build/64/BOOTX64.EFI').read_bytes()
assert image[4131 * 512:4131 * 512 + length] == expected
state_header = image[136192 * 512:136193 * 512]
assert state_header[:12] == b'BETA STATE 1'
length, crc = struct.unpack_from('<II', state_header, 16)
state = image[136193 * 512:136193 * 512 + length]
assert binascii.crc32(state) == crc
assert state[:4] == b'BDS2'
file_bytes=struct.unpack_from('<I',state,4)[0]
config_at=16+file_bytes
assert struct.unpack_from('<iii',state,config_at+4+10*44)==(680,300,0)
assert b'notes2.txt' in state
assert b'native-files' in state and b'scratch.asm' in state and b'BEX1' in state
print('PASS: primary/backup GPT CRCs, FAT32 boot file byte equality and persistent-state CRC')
