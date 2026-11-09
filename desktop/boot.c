#include "os.h"
#include "web.h"
typedef struct {
  u32 a;
  u16 b, c;
  u8 d[8];
} Guid;
typedef struct {
  u64 sig;
  u32 rev, size, crc, reserved;
} Header;
typedef struct {
  u32 version, width, height, format;
  u32 masks[4];
  u32 stride;
} ModeInfo;
typedef struct {
  u32 max, current;
  ModeInfo *info;
  u64 info_size;
  u64 fb, size;
} Mode;
typedef struct Gop {
  u64(EFI *query)(struct Gop *, u32, u64 *, ModeInfo **);
  u64(EFI *set)(struct Gop *, u32);
  void *blt;
  Mode *mode;
} Gop;
typedef struct {
  Header h;
  void *raise, *restore;
  u64(EFI *alloc_pages)(u32, u32, u64, u64 *);
  void *free_pages;
  u64(EFI *memory_map)(u64 *, void *, u64 *, u64 *, u32 *);
  u64(EFI *alloc_pool)(u32, u64, void **);
  u64(EFI *free_pool)(void *);
  void *event[6];
  void *protocol[9];
  void *image[4];
  u64(EFI *exit_boot)(void *, u64);
  void *monotonic, *stall, *watchdog;
  void *connect, *disconnect, *open, *close, *openinfo, *protocols, *locatebuf;
  u64(EFI *locate)(Guid *, void *, void **);
} BootServices;
typedef struct {
  Header h;
  void *vendor;
  u32 revision;
  u32 padding;
  void *con_handle, *con_in, *out_handle, *con_out, *err_handle, *err_out,
      *runtime;
  BootServices *boot;
} SystemTable;
static Guid gop_guid = {0x9042a9de,
                        0x23dc,
                        0x4a38,
                        {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
volatile u32 *framebuffer;
u32 *gui_canvas;
u32 *gui_previous;
volatile u64 timer_ticks, input_irq_ticks;
int input_irq_enabled;
static int timer_ready;
static u32 requested_width = 1024, requested_height = 768;
u32 fb_width, fb_height, fb_stride, fb_format;
u8 *dma;
u64 usable_ram, clock_hz = 2000000000;
u8 boot_entropy[64];
int boot_entropy_ready;
u32 utc_days, utc_seconds;
u64 utc_started;
typedef struct {
  u16 year;
  u8 month, day, hour, minute, second, pad;
  u32 nano;
  int16_t zone;
  u8 daylight, pad2;
} EfiTime;
static void boot_security(SystemTable *system) {
  typedef struct {
    Header h;
    u64(EFI *get_time)(EfiTime *, void *);
  } Runtime;
  EfiTime t;
  Runtime *rt = system->runtime;
  if (rt && !rt->get_time(&t, 0) && t.year >= 2020 && t.year <= 9999 &&
      t.month >= 1 && t.month <= 12 && t.day >= 1 && t.day <= 31 &&
      t.hour < 24 && t.minute < 60 && t.second < 60) {
    u32 days = 0;
    for (unsigned y = 0; y < t.year; y++)
      days += 365 + (!(y % 4) && (y % 100 || !(y % 400)));
    static const u8 md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (unsigned m = 1; m < t.month; m++)
      days += md[m - 1] +
              (m == 2 && !(t.year % 4) && (t.year % 100 || !(t.year % 400)));
    utc_days = days + t.day - 1;
    utc_seconds = t.hour * 3600 + t.minute * 60 + t.second;
  }
  Guid id = {0x3152bca5,
             0xeade,
             0x433d,
             {0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44}};
  typedef struct {
    void *info;
    u64(EFI *get)(void *, void *, u64, void *);
  } Rng;
  Rng *rng = 0;
  if (!system->boot->locate(&id, 0, (void **)&rng) && rng &&
      !rng->get(rng, 0, 64, boot_entropy))
    boot_entropy_ready = 1;
  if (!boot_entropy_ready) {
    u32 a = 1, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (c & (1u << 30)) {
      boot_entropy_ready = 1;
      for (unsigned i = 0; i < 8; i++) {
        u64 value = 0;
        unsigned char ok = 0;
        for (unsigned retry = 0; retry < 32 && !ok; retry++)
          __asm__ volatile("rdrand %0; setc %1" : "=r"(value), "=qm"(ok));
        if (!ok || !value || value == ~0ull)
          boot_entropy_ready = 0;
        write_le64(boot_entropy + i * 8, value);
      }
    }
  }
}
int platform_utc(u32 *days, u32 *seconds) {
  if (!utc_days || !utc_started)
    return 0;
  u64 elapsed = (cycles() - utc_started) / clock_hz + utc_seconds;
  *days = utc_days + elapsed / 86400;
  *seconds = elapsed % 86400;
  return 1;
}
static u8 memory_map[131072] __attribute__((aligned(16)));
void *memcpy(void *d, const void *s, size_t n) {
  u8 *a = d;
  const u8 *b = s;
  for (size_t i = 0; i < n; i++)
    a[i] = b[i];
  return d;
}
void *memset(void *d, int v, size_t n) {
  u8 *a = d;
  for (size_t i = 0; i < n; i++)
    a[i] = (u8)v;
  return d;
}
size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n])
    n++;
  return n;
}
int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (u8)*a - (u8)*b;
}
int starts(const char *a, const char *b) {
  while (*b)
    if (*a++ != *b++)
      return 0;
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
void serial(const char *s) {
  while (*s) {
    unsigned limit = 100000;
    while (!(in8(0x3fd) & 32) && --limit) {
    }
    out8(0x3f8, *s++);
  }
}
void serial_num(u64 n) {
  char b[24];
  unsigned i = 0;
  do {
    b[i++] = '0' + n % 10;
    n /= 10;
  } while (n);
  while (i) {
    char c[2] = {b[--i], 0};
    serial(c);
  }
}
void fatal(const char *s) {
  serial("FATAL: ");
  serial(s);
  serial("\n");
  for (;;)
    __asm__ volatile("cli; hlt");
}
static void calibrate(void) {
  u32 a = 0x15, b, c, d;
  __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
  if (a && b && c) {
    clock_hz = (u64)c * b / a;
    return;
  }
  /* PIT channel 2: 10 ms reference, independent of firmware timers. */
  u8 old = in8(0x61);
  out8(0x61, (old & ~2) | 1);
  out8(0x43, 0xb0);
  out8(0x42, 11932 & 255);
  out8(0x42, 11932 >> 8);
  u64 start = cycles();
  unsigned guard = 10000000;
  while (!(in8(0x61) & 32) && --guard) {
  }
  if (guard)
    clock_hz = (cycles() - start) * 100;
  out8(0x61, old);
}
struct Gate {
  u16 lo, cs;
  u8 ist, attr;
  u16 mid;
  u32 hi, zero;
} __attribute__((packed));
static struct Gate idt[256];
extern void exception64(void), timer_irq64(void), keyboard_irq64(void),
    mouse_irq64(void), spurious7_irq64(void), spurious15_irq64(void);
extern const u64 exception_vectors[32];
struct FaultFrame {
  u64 vector, error, rip, cs, flags, rsp, ss;
};
static void serial_hex(u64 value) {
  static const char digits[] = "0123456789abcdef";
  serial("0x");
  for (int shift = 60; shift >= 0; shift -= 4) {
    char c[2] = {digits[(value >> shift) & 15], 0};
    serial(c);
  }
}
void exception_report(const struct FaultFrame *frame) {
  u64 address;
  __asm__ volatile("mov %%cr2,%0" : "=r"(address));
  serial("\nPANIC: vector=");
  serial_num(frame->vector);
  serial(" error=");
  serial_num(frame->error);
  serial(" rip=");
  serial_hex(frame->rip);
  serial(" cr2=");
  serial_hex(address);
  serial(" flags=");
  serial_hex(frame->flags);
  serial("\nPANIC HALTED\n");
  fatal("CPU fault; diagnostic written to COM1");
}
static void own_cpu(void) {
  __asm__ volatile("cli");
  out8(0x21, 255);
  out8(0xa1, 255);
  u16 cs;
  __asm__ volatile("mov %%cs,%0" : "=r"(cs));
  u64 a = (u64)exception64;
  for (unsigned i = 0; i < 256; i++)
    idt[i] = (struct Gate){a, cs, 0, 0x8e, a >> 16, a >> 32, 0};
  for (unsigned i = 0; i < 32; i++) {
    a = exception_vectors[i];
    idt[i] = (struct Gate){a, cs, 0, 0x8e, a >> 16, a >> 32, 0};
  }
  struct {
    u16 size;
    u64 base;
  } __attribute__((packed)) p = {sizeof(idt) - 1, (u64)idt};
  __asm__ volatile("lidt %0" ::"m"(p));
}
static void timer_init(void) {
  u16 cs;
  __asm__ volatile("mov %%cs,%0" : "=r"(cs));
  u64 address = (u64)timer_irq64;
  idt[32] =
      (struct Gate){address, cs, 0, 0x8e, address >> 16, address >> 32, 0};
  void (*handlers[])(void) = {keyboard_irq64, mouse_irq64, spurious7_irq64,
                              spurious15_irq64};
  unsigned vectors[] = {33, 44, 39, 47};
  for (unsigned i = 0; i < 4; i++) {
    address = (u64)handlers[i];
    idt[vectors[i]] =
        (struct Gate){address, cs, 0, 0x8e, address >> 16, address >> 32, 0};
  }
  out8(0x20, 0x11);
  out8(0xa0, 0x11);
  out8(0x21, 32);
  out8(0xa1, 40);
  out8(0x21, 4);
  out8(0xa1, 2);
  out8(0x21, 1);
  out8(0xa1, 1);
  out8(0x21, 0xfe);
  out8(0xa1, 0xff);
  out8(0x43, 0x36);
  out8(0x40, 1193 & 255);
  out8(0x40, 1193 >> 8);
  u64 until = cycles() + clock_hz / 20;
  __asm__ volatile("sti");
  while (!timer_ticks && cycles() < until)
    __asm__ volatile("pause");
  __asm__ volatile("cli");
  timer_ready = timer_ticks != 0;
  if (!timer_ready)
    out8(0x21, 0xff);
  input_irq_enabled = timer_ready;
  if (timer_ready) {
    out8(0x21, 0xf8);
    out8(0xa1, 0xef);
  }
  serial(timer_ready ? "TIMER: interrupt-driven idle enabled\n"
                     : "TIMER: unavailable / polling fallback\n");
}
typedef struct EfiFile EfiFile;
struct EfiFile {
  u64 revision;
  u64(EFI *open)(EfiFile *, EfiFile **, u16 *, u64, u64);
  u64(EFI *close)(EfiFile *);
  void *remove;
  u64(EFI *read)(EfiFile *, u64 *, void *);
};
typedef struct {
  u64 revision;
  u64(EFI *open)(void *, EfiFile **);
} EfiFs;
typedef struct {
  u32 revision;
  void *parent, *system, *device, *path, *reserved;
  u32 options_size;
  void *options, *base;
  u64 size;
  u32 code_type, data_type;
  void *unload;
} LoadedImage;
static void retain_boot_image(BootServices *bs, void *handle) {
  Guid loaded = {0x5b1b31a1,
                 0x9562,
                 0x11d2,
                 {0x8e, 0x3f, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
  Guid filesystem = {0x964e5b22,
                     0x6459,
                     0x11d2,
                     {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
  u64(EFI * handle_protocol)(void *, Guid *, void **) = (void *)bs->protocol[3];
  LoadedImage *image = 0;
  EfiFs *fs = 0;
  EfiFile *root = 0, *file = 0;
  if (handle_protocol(handle, &loaded, (void **)&image) || !image)
    return;
  if (handle_protocol(image->device, &filesystem, (void **)&fs) || !fs)
    return;
  if (fs->open(fs, &root))
    return;
  u16 config_path[] = {'\\', 'B', 'E', 'T', 'A', '.', 'C', 'F', 'G', 0};
  EfiFile *config_file = 0;
  if (!root->open(root, &config_file, config_path, 1, 0)) {
    char config[128] = {0};
    u64 bytes = sizeof(config) - 1;
    if (!config_file->read(config_file, &bytes, config) &&
        starts(config, "resolution=")) {
      const char *p = config + 11;
      u32 width = 0, height = 0;
      unsigned n = 0;
      while (*p >= '0' && *p <= '9' && n++ < 5)
        width = width * 10 + *p++ - '0';
      if (*p == 'x') {
        p++;
        n = 0;
        while (*p >= '0' && *p <= '9' && n++ < 5)
          height = height * 10 + *p++ - '0';
        if ((*p == 0 || *p == '\r' || *p == '\n') && width >= 800 &&
            width <= 4096 && height >= 600 && height <= 2160) {
          requested_width = width;
          requested_height = height;
        }
      }
    }
    config_file->close(config_file);
  }
  u16 path[] = {'\\', 'E', 'F', 'I', '\\', 'B', 'O', 'O', 'T', '\\', 'B',
                'O',  'O', 'T', 'X', '6',  '4', '.', 'E', 'F', 'I',  0};
  if (root->open(root, &file, path, 1, 0)) {
    root->close(root);
    return;
  }
  u64 address = 0xffffffff;
  if (!bs->alloc_pages(1, 2, 4096, &address)) {
    u64 bytes = 16 * 1024 * 1024;
    if (!file->read(file, &bytes, (void *)address) && bytes >= 512 &&
        *(u16 *)address == 0x5a4d) {
      boot_image = (u8 *)address;
      boot_image_size = bytes;
      serial("BOOT IMAGE RETAINED: ");
      serial_num(bytes);
      serial(" bytes\n");
    }
  }
  file->close(file);
  root->close(root);
}
u64 EFI efi_main(void *image, SystemTable *system) {
  out8(0x3f9, 0);
  out8(0x3fb, 0x80);
  out8(0x3f8, 1);
  out8(0x3f9, 0);
  out8(0x3fb, 3);
  out8(0x3fa, 0xc7);
  out8(0x3fc, 0x0b);
  serial("BETAOS64: UEFI entry\n");
  BootServices *bs = system->boot;
  boot_security(system);
  retain_boot_image(bs, image);
  Gop *gop = 0;
  if (bs->locate(&gop_guid, 0, (void **)&gop) || !gop)
    fatal("GOP unavailable");
  u32 best_mode = 0, best_score = ~0u;
  int found = 0;
  for (u32 m = 0; m < gop->mode->max; m++) {
    ModeInfo *info = 0;
    u64 size = 0;
    if (!gop->query(gop, m, &size, &info)) {
      if (info && size >= sizeof(ModeInfo) && info->format < 2 &&
          info->width >= 800 && info->height >= 600 && info->width <= 4096 &&
          info->height <= 2160 && info->stride >= info->width) {
        u32 score =
            (info->width > requested_width ? info->width - requested_width
                                           : requested_width - info->width) +
            (info->height > requested_height ? info->height - requested_height
                                             : requested_height - info->height);
        if (!found || score < best_score) {
          best_score = score;
          best_mode = m;
          found = 1;
        }
      }
      if (info)
        bs->free_pool(info);
    }
  }
  if (!found || gop->set(gop, best_mode))
    fatal("No usable GOP graphics mode");
  if (gop->mode->info->format > 1 || gop->mode->info->width < 800 ||
      gop->mode->info->height < 600)
    fatal("Requires RGB/BGR GOP >=800x600");
  framebuffer = (volatile u32 *)gop->mode->fb;
  fb_width = gop->mode->info->width;
  fb_height = gop->mode->info->height;
  fb_stride = gop->mode->info->stride;
  fb_format = gop->mode->info->format;
  if (!framebuffer || fb_stride < fb_width ||
      gop->mode->size < (u64)fb_stride * fb_height * 4)
    fatal("Invalid framebuffer bounds");
  serial("GFX: ");
  serial_num(fb_width);
  serial("x");
  serial_num(fb_height);
  serial("\n");
  u64 canvas_address = 0xffffffff;
  u64 canvas_bytes = (u64)fb_stride * fb_height * 4;
  if (canvas_bytes > 64 * 1024 * 1024 ||
      bs->alloc_pages(1, 2, (canvas_bytes * 2 + 4095) / 4096, &canvas_address))
    fatal("Cannot allocate desktop backbuffer");
  gui_canvas = (u32 *)canvas_address;
  gui_previous = (u32 *)(canvas_address + canvas_bytes);
  memset(gui_previous, 255, canvas_bytes);
  u64 address = 0xffffffff;
  if (bs->alloc_pages(1, 2, 256, &address))
    fatal("Cannot allocate DMA below 4GiB");
  dma = (u8 *)address;
  memset(dma, 0, 1048576);
  /* LoaderCode pages remain executable after the firmware handoff. */
  u64 executable = 0xffffffff;
  if (bs->alloc_pages(1, 1, 16, &executable))
    fatal("Cannot allocate native program arena");
  program_memory = (u8 *)executable;
  u64 map_size, key, desc_size;
  u32 version;
  int exited = 0;
  for (unsigned attempt = 0; attempt < 3; attempt++) {
    map_size = sizeof(memory_map);
    if (bs->memory_map(&map_size, memory_map, &key, &desc_size, &version))
      fatal("Memory map failed");
    if (!bs->exit_boot(image, key)) {
      exited = 1;
      break;
    }
  }
  if (!exited)
    fatal("ExitBootServices failed");
  own_cpu();
  for (u64 i = 0; i < map_size; i += desc_size) {
    u8 *d = memory_map + i;
    u32 type = *(u32 *)d;
    if (type == 7)
      usable_ram += *(u64 *)(d + 24) * 4096;
  }
  serial("BETAOS64: ExitBootServices OK; native kernel owns CPU\n");
  calibrate();
  utc_started = cycles();
  timer_init();
  input_init();
  __asm__ volatile("fninit");
  u32 mxcsr = 0x1f80;__asm__ volatile("ldmxcsr %0"::"m"(mxcsr));
  font_init();
  gui_init();
  program_init();
  disk_init();
  usb_init();
  desktop_restore();
  net_init();
  gui_render();
  serial("BETAOS64: DESKTOP READY\n");
  unsigned programmed_rate = 1000;
  for (;;) {
    input_poll();
    net_poll();
    web_poll();
    gui_render();
    if (timer_ready) {
      unsigned rate = gui_animating() ? 1000 : 50;
      if (rate != programmed_rate) {
        unsigned divisor = 1193182 / rate;
        out8(0x43, 0x36);
        out8(0x40, divisor & 255);
        out8(0x40, divisor >> 8);
        programmed_rate = rate;
      }
      __asm__ volatile("sti; hlt; cli" ::: "memory");
    } else
      __asm__ volatile("pause");
  }
}
