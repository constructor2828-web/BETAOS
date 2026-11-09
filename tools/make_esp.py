"""Create a deterministic FAT16 EFI system partition with no root privileges."""
import pathlib
import struct
import sys

payload = pathlib.Path(sys.argv[1]).read_bytes()
image = bytearray(16 * 1024 * 1024)
sector = 512
fat_sectors = 128
root_start = 1 + 2 * fat_sectors
data_start = root_start + 32
image[:3] = b'\xeb\x3c\x90'
image[3:11] = b'BETAOS  '
struct.pack_into('<HBHBHHBHHHII', image, 11, 512, 1, 1, 2, 512, 32768, 0xf8, fat_sectors, 32, 64, 0, 0)
image[36] = 0x80
image[38] = 0x29
struct.pack_into('<I', image, 39, 0x42455441)
image[43:54] = b'BETAOS EFI '
image[54:62] = b'FAT16   '
image[510:512] = b'\x55\xaa'
fat = bytearray(fat_sectors * sector)
struct.pack_into('<HH', fat, 0, 0xfff8, 0xffff)


def chain(first, count):
    for cluster in range(first, first + count):
        struct.pack_into('<H', fat, cluster * 2, 0xffff if cluster == first + count - 1 else cluster + 1)


def entry(name, attr, cluster, size=0):
    data = bytearray(32)
    data[:11] = name
    data[11] = attr
    struct.pack_into('<H', data, 26, cluster)
    struct.pack_into('<I', data, 28, size)
    return data


def cluster_offset(cluster):
    return (data_start + cluster - 2) * sector


chain(2, 1)
chain(3, 1)
count = (len(payload) + 511) // 512
assert count + 4 < 32768 - data_start
chain(4, count)
image[root_start * sector:root_start * sector + 32] = entry(b'EFI        ', 0x10, 2)
for cluster, parent, child in [(2, 0, entry(b'BOOT       ', 0x10, 3)), (3, 2, entry(b'BOOTX64 EFI', 0x20, 4, len(payload)))]:
    offset = cluster_offset(cluster)
    image[offset:offset + 96] = entry(b'.          ', 0x10, cluster) + entry(b'..         ', 0x10, parent) + child
image[cluster_offset(4):cluster_offset(4) + len(payload)] = payload
if len(sys.argv) > 3:
    import re
    resolution = sys.argv[3]
    if not re.fullmatch(r'[0-9]{3,4}x[0-9]{3,4}', resolution):
        raise SystemExit('Expected WIDTHxHEIGHT')
    config = ('resolution=' + resolution + '\n').encode('ascii')
    config_cluster = 4 + count
    chain(config_cluster, 1)
    image[root_start * sector + 32:root_start * sector + 64] = entry(b'BETA    CFG', 0x20, config_cluster, len(config))
    offset = cluster_offset(config_cluster)
    image[offset:offset + len(config)] = config
image[512:512 + len(fat)] = fat
image[(1 + fat_sectors) * sector:(1 + 2 * fat_sectors) * sector] = fat
pathlib.Path(sys.argv[2]).write_bytes(image)
