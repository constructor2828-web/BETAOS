#include "os.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static u8 *image;
static size_t image_size;
static unsigned reads;
u64 clock_hz = 3000000000ull;
static u32 generation = 1;
u64 usb_sectors(unsigned disk) { return disk ? 0 : image_size / 512; }
u32 usb_generation(unsigned disk) { return disk ? 0 : generation; }
int usb_read_sector(unsigned disk, u64 lba, void *out) {
  assert(lba < image_size / 512);
  if (disk)
    return 0;
  reads++;
  memcpy(out, image + lba * 512, 512);
  return 1;
}
void strcopy(char *d, const char *s, size_t cap) {
  size_t i = 0;
  while (i + 1 < cap && s[i]) {
    d[i] = s[i];
    i++;
  }
  if (cap)
    d[i] = 0;
}
u32 crc32(const void *data, size_t n) {
  const u8 *p = data;
  u32 c = ~0u;
  while (n--) {
    c ^= *p++;
    for (unsigned i = 0; i < 8; i++)
      c = (c >> 1) ^ (0xedb88320u & -(c & 1));
  }
  return ~c;
}
static struct FatEntry list[96];
static u8 out[65552];
int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *f = fopen(argv[1], "rb");
  assert(f);
  fseek(f, 0, SEEK_END);
  image_size = ftell(f);
  rewind(f);
  image = malloc(image_size);
  assert(image && fread(image, 1, image_size, f) == image_size);
  fclose(f);
  assert(fat_mount(0));
  int count = fat_list(fat_root(), list, 96);
  assert(count == 4);
  assert(!strcmp(list[0].name, "README.TXT") &&
         !strcmp(list[3].name, "Long filename from USB.txt"));
  u32 size;
  assert(fat_read(&list[0], out, sizeof(out), &size) && size == list[0].size);
  assert(!memcmp(out, "REAL USB STORAGE", 16));
  assert(!fat_read(&list[0], out, 2, &size));
  assert(fat_read_prefix(&list[0], out, 4, &size) && size == 4 &&
         !memcmp(out, "REAL", 4));
  assert(fat_list(list[1].cluster, list, 96) == 1 &&
         !strcmp(list[0].name, "NESTED.TXT"));
  assert(fat_read(&list[0], out, sizeof(out), &size));
  // A new device at the same USB slot cannot reuse a previous FAT mount.
  generation++;
  assert(!fat_read(&list[0], out, sizeof(out), &size));
  assert(fat_mount(0));
  // Malformed LFN order falls back to the actual short name.
  u64 root = (2048 + 257) * 512;
  image[root + 128] = 2;
  assert(fat_list(fat_root(), list, 96) == 4 &&
         !strcmp(list[3].name, "LONGFI~1.TXT"));
  image[root + 128] = 1;
  // A file chain that repeats must fail rather than duplicate data to the
  // requested size.
  assert(fat_mount(0));
  assert(fat_list(fat_root(), list, 96) == 4);
  struct FatEntry bad = list[0];
  bad.size = 2048;
  u64 fat = 2049 * 512;
  write_le16(image + fat + bad.cluster * 2, bad.cluster);
  assert(!fat_read(&bad, out, sizeof(out), &size));
  assert(fat_mount(0));
  bad.cluster = 0xffffffff;
  assert(!fat_read(&bad, out, sizeof(out), &size));
  // Deterministic BPB mutations exercise media-derived arithmetic under
  // ASan/UBSan.
  u8 baseline[512];
  memcpy(baseline, image + 2048 * 512, 512);
  u32 seed = 12345;
  for (unsigned i = 0; i < 5000; i++) {
    memcpy(image + 2048 * 512, baseline, 512);
    seed = seed * 1664525 + 1013904223;
    unsigned at = 11 + seed % 80;
    image[2048 * 512 + at] ^= 1u << (seed % 8);
    reads = 0;
    if (fat_mount(0))
      fat_list(fat_root(), list, 96);
    assert(reads < 2000);
  }
  free(image);
  puts("PASS: FAT16/MBR, LFN, nested directories, bounded previews, "
       "capacity/cycle rejection and 5000 BPB mutations");
}
