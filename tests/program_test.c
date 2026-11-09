#include "os.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
u64 usable_ram = 512ull * 1024 * 1024;
void serial(const char *s) { (void)s; }
void serial_num(u64 n) { (void)n; }
static char last_title[64];
void gui_progress(const char *a, const char *b, u32 n, u32 total) {
  snprintf(last_title, sizeof(last_title), "%s", a);
  (void)b;
  (void)n;
  (void)total;
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
static u8 baseline[2 * 1024 * 1024], changed[2 * 1024 * 1024],
    roundtrip[2 * 1024 * 1024];
static u32 rng = 0xbe7a1234;
static u32 random32(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
int main(void) {
  assert(!program_exists(""));
  assert(!program_exists(0));
  program_init();
  assert(program_install("fib", serial));
  assert(program_install("memory", serial));
  u8 tiny[17] = {'B', 'E', 'X', '1'};
  write_le32(tiny + 4, 1);
  write_le32(tiny + 8, 0);
  write_le32(tiny + 12, 1);
  tiny[16] = 0xc3;
  assert(program_install_blob("tiny", tiny, sizeof(tiny), serial));
  assert(!program_install_blob("bad/name", tiny, sizeof(tiny), serial));
  assert(!program_install_blob("", tiny, sizeof(tiny), serial));
  assert(!program_install_blob("bad", 0, 17, serial));
  u32 size = program_export(baseline, sizeof(baseline));
  assert(size > 4 && read_le32(baseline) == 4);
  assert(program_import(baseline, size));
  assert(program_export(roundtrip, sizeof(roundtrip)) == size &&
         !memcmp(baseline, roundtrip, size));
  assert(!program_import(0, 4));
  assert(!program_export(0, 4));
  assert(!program_import(baseline, size - 1));
  memcpy(changed, baseline, size);
  changed[4] = 0;
  assert(!program_import(changed, size));
  memcpy(changed, baseline, size);
  u32 second = 4 + 36 + read_le32(changed + 36);
  memcpy(changed + second, changed + 4, 32);
  assert(!program_import(changed, size));
  for (unsigned iteration = 0; iteration < 5000; iteration++) {
    memcpy(changed, baseline, size);
    unsigned flips = 1 + random32() % 8;
    for (unsigned i = 0; i < flips; i++)
      changed[random32() % size] ^= 1u << (random32() % 8);
    int valid = program_import(changed, size);
    if (!valid) {
      assert(program_export(roundtrip, sizeof(roundtrip)) == size);
      assert(!memcmp(roundtrip, baseline, size));
    }
    assert(program_import(baseline, size));
  }
  for (unsigned i = 0; i < 12; i++) {
    char name[16];
    snprintf(name, sizeof(name), "extra%u", i);
    assert(program_install_blob(name, tiny, sizeof(tiny), serial));
  }
  assert(!program_install_blob("overflow", tiny, sizeof(tiny), serial));
  assert(!strcmp(last_title, "APP INSTALL FAILED"));
  assert(program_remove("extra0", serial));
  assert(program_install_blob("overflow", tiny, sizeof(tiny), serial));
  assert(!strcmp(last_title, "APP INSTALLED"));
  puts("PASS: native app headers, unaligned round trips, transactional "
       "rejection, duplicate/empty names, 5000 mutations and full-storage "
       "recovery");
}
