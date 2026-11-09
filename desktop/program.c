#include "os.h"
#include "program_catalog.h"
u8 *program_memory;
static void (*output)(const char *);
static void print_number(u64 n) {
  char reverse[24], value[24];
  unsigned i = 0, j = 0;
  do {
    reverse[i++] = '0' + n % 10;
    n /= 10;
  } while (n);
  while (i)
    value[j++] = reverse[--i];
  value[j] = 0;
  output(value);
}
static u64 memory_mib(void) { return usable_ram / 1048576; }
struct ProgramApi {
  u64 version;
  void (*print)(const char *);
  void (*number)(u64);
  u64 (*memory_mib)(void);
};
#define APP_MAX 16
#define APP_BYTES 65552
static struct App {
  char name[32];
  u32 size;
  u8 data[APP_BYTES];
} apps[APP_MAX];
static int valid_name(const char *name) {
  if (!name || !*name || strlen(name) > 31)
    return 0;
  for (const char *p = name; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_'))
      return 0;
  return 1;
}
static int valid(const u8 *data, u32 size) {
  if (!data || size < 17 || size > APP_BYTES || data[0] != 'B' ||
      data[1] != 'E' || data[2] != 'X' || data[3] != '1')
    return 0;
  u32 n = read_le32(data + 4), entry = read_le32(data + 8),
      version = read_le32(data + 12);
  return version == 1 && n && n <= 65536 && n == size - 16 && entry < n;
}
static int app_slot(const char *name) {
  if (!name || !*name)
    return -1;
  for (int i = 0; i < APP_MAX; i++)
    if (!strcmp(apps[i].name, name))
      return i;
  return -1;
}
int program_install_blob(const char *name, const u8 *data, u32 size,
                         void (*print)(const char *)) {
  if (!print)
    return 0;
  if (!valid_name(name) || !valid(data, size)) {
    print("Invalid app name or BEX executable.\n");
    gui_progress("APP INSTALL FAILED", "Invalid app name or BEX executable.", 1,
                 1);
    return 0;
  }
  int slot = app_slot(name);
  if (slot < 0)
    for (int i = 0; i < APP_MAX; i++)
      if (!apps[i].name[0]) {
        slot = i;
        break;
      }
  if (slot < 0) {
    print("App storage full. Uninstall an app first.\n");
    gui_progress("APP INSTALL FAILED",
                 "App storage full. Uninstall an app first.", 1, 1);
    return 0;
  }
  gui_progress("INSTALLING ASM APP", "Validating native executable", 0, 100);
  memset(apps[slot].name, 0, sizeof(apps[slot].name));
  apps[slot].size = size;
  for (u32 i = 0; i < size; i += 1024) {
    u32 n = size - i;
    if (n > 1024)
      n = 1024;
    memcpy(apps[slot].data + i, data + i, n);
    gui_progress("INSTALLING ASM APP", "Copying executable", 10 + i * 80 / size,
                 100);
  }
  strcopy(apps[slot].name, name, 32);
  gui_progress("APP INSTALLED", name, 100, 100);
  print("Installed: ");
  print(name);
  print("\n");
  return 1;
}
int program_install(const char *name, void (*print)(const char *)) {
  if (!name || !print)
    return 0;
  for (unsigned i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
    if (!strcmp(name, builtins[i].name))
      return program_install_blob(name, builtins[i].start,
                                  builtins[i].end - builtins[i].start, print);
  print("Package not found. Type programs to see available packages.\n");
  gui_progress("APP INSTALL FAILED",
               "Package not found. Check available packages.", 1, 1);
  return 0;
}
int program_remove(const char *name, void (*print)(const char *)) {
  int i = app_slot(name);
  if (i < 0) {
    print("App not installed.\n");
    return 0;
  }
  apps[i].name[0] = 0;
  print("Uninstalled: ");
  print(name);
  print("\n");
  return 1;
}
void program_init(void) {
  program_install("hello", serial);
  gui_progress("", "", 0, 0);
}
u32 program_export(void *buffer, u32 cap) {
  u8 *out = buffer;
  u32 at = 4, count = 0;
  if (!buffer || cap < 4)
    return 0;
  for (int i = 0; i < APP_MAX; i++)
    if (apps[i].name[0]) {
      u32 n = apps[i].size;
      if (at + 36 + n > cap)
        return 0;
      memcpy(out + at, apps[i].name, 32);
      write_le32(out + at + 32, n);
      memcpy(out + at + 36, apps[i].data, n);
      at += 36 + n;
      count++;
    }
  write_le32(out, count);
  return at;
}
int program_import(const void *buffer, u32 size) {
  const u8 *in = buffer;
  if (!buffer || size < 4)
    return 0;
  u32 count = read_le32(in), at = 4;
  if (count > APP_MAX)
    return 0;
  const char *names[APP_MAX];
  for (u32 i = 0; i < count; i++) {
    if (at + 36 > size || in[at + 31])
      return 0;
    if (!valid_name((const char *)in + at))
      return 0;
    names[i] = (const char *)in + at;
    for (u32 j = 0; j < i; j++)
      if (!strcmp(names[j], names[i]))
        return 0;
    u32 n = read_le32(in + at + 32);
    if (n > size - at - 36 || !valid(in + at + 36, n))
      return 0;
    at += 36 + n;
  }
  if (at != size)
    return 0;
  memset(apps, 0, sizeof(apps));
  at = 4;
  for (u32 i = 0; i < count; i++) {
    u32 n = read_le32(in + at + 32);
    strcopy(apps[i].name, (const char *)in + at, 32);
    apps[i].size = n;
    memcpy(apps[i].data, in + at + 36, n);
    at += 36 + n;
  }
  return 1;
}
void program_list(void (*print)(const char *)) {
  print("NATIVE X86-64 PROGRAMS / BEX ABI 1\n");
  for (unsigned i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
    print("  ");
    print(builtins[i].name);
    print(app_slot(builtins[i].name) >= 0 ? " [installed]\n"
                                          : " [available: install NAME]\n");
  }
  print("Installed applications:\n");
  for (int i = 0; i < APP_MAX; i++)
    if (apps[i].name[0]) {
      print("  run ");
      print(apps[i].name);
      print("\n");
    }
}
int program_run(const char *name, void (*print)(const char *)) {
  int slot = app_slot(name);
  if (slot < 0) {
    print("Program not found. Use install NAME or programs.\n");
    return 0;
  }
  const u8 *data = apps[slot].data;
  u32 size = apps[slot].size;
  if (!valid(data, size) || !program_memory) {
    print("Invalid executable.\n");
    return 0;
  }
  u32 bytes = read_le32(data + 4), entry = read_le32(data + 8);
  output = print;
  struct ProgramApi api = {1, print, print_number, memory_mib};
  memcpy(program_memory, data + 16, bytes);
  u32 a = 0, b, c, d;
  __asm__ volatile("mfence; cpuid"
                   : "+a"(a), "=b"(b), "=c"(c), "=d"(d)::"memory");
  serial("PROGRAM ENTRY: ");
  serial(name);
  serial(" @ ");
  serial_num((u64)program_memory + entry);
  serial("\n");
  ((void (*)(const struct ProgramApi *))(program_memory + entry))(&api);
  serial("PROGRAM RETURN: ");
  serial(name);
  serial("\n");
  print("[Program returned]\n");
  return 1;
}
unsigned package_total(void) { return sizeof(builtins) / sizeof(builtins[0]); }
int program_exists(const char *name) { return app_slot(name) >= 0; }
unsigned installed_total(void) {
  unsigned count = 0;
  for (unsigned i = 0; i < APP_MAX; i++)
    count += apps[i].name[0] != 0;
  return count;
}
const char *installed_name(unsigned index) {
  for (unsigned i = 0; i < APP_MAX; i++)
    if (apps[i].name[0] && !index--)
      return apps[i].name;
  return "";
}
const char *package_name(unsigned i) {
  return i < package_total() ? builtins[i].name : "";
}
int package_installed(unsigned i) {
  return i < package_total() && app_slot(builtins[i].name) >= 0;
}
