"""Add GPT + an ESP to ISO system area, retaining the El Torito optical boot path."""
import binascii
import pathlib
import struct
import sys
import uuid

path = pathlib.Path(sys.argv[1])
data = bytearray(path.read_bytes())
esp = pathlib.Path(sys.argv[2]).read_bytes()
# GPT entries live after the ISO volume, preserving its descriptors at LBA 64.
entries_lba = (len(data) + 511) // 512
esp_start = (entries_lba + 32 + 2047) & ~2047
esp_end = esp_start + len(esp) // 512 - 1
backup_entries = esp_end + 1
last_lba = backup_entries + 32
data.extend(bytes((last_lba + 1) * 512 - len(data)))
data[esp_start * 512:(esp_end + 1) * 512] = esp
entries = bytearray(16384)
struct.pack_into('<16s16sQQQ', entries, 0,
                 uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le,
                 uuid.UUID('42455441-0002-4000-8000-000000000001').bytes_le,
                 esp_start, esp_end, 0)
name = 'BETAOS EFI'.encode('utf-16-le')
entries[56:56 + len(name)] = name
crc = binascii.crc32(entries)
disk_id = uuid.UUID('42455441-0002-4000-8000-000000000002').bytes_le


def header(current, alternate, table):
    result = bytearray(512)
    struct.pack_into('<8sIIIIQQQQ16sQIII', result, 0, b'EFI PART', 0x10000, 92, 0, 0,
                     current, alternate, esp_start, esp_end, disk_id, table, 128, 128, crc)
    struct.pack_into('<I', result, 16, binascii.crc32(result[:92]))
    return result


data[:512] = bytes(512)
struct.pack_into('<B3sB3sII', data, 446, 0, b'\x00\x02\x00', 0xee, b'\xff\xff\xff', 1, min(last_lba, 0xffffffff))
data[510:512] = b'\x55\xaa'
data[512:1024] = header(1, last_lba, entries_lba)
data[entries_lba * 512:(entries_lba + 32) * 512] = entries
data[backup_entries * 512:(backup_entries + 32) * 512] = entries
data[last_lba * 512:(last_lba + 1) * 512] = header(last_lba, 1, backup_entries)
path.write_bytes(data)
