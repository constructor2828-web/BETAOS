#ifndef BETA_OS_H
#define BETA_OS_H
#define BETA_VERSION "0.8"
#include <stddef.h>
#include <stdint.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
/* Serialized data may be byte-aligned; never cast it to wider C pointer types.
 */
static inline u16 read_le16(const void *buffer) {
  const u8 *p = buffer;
  return (u16)p[0] | ((u16)p[1] << 8);
}
static inline u32 read_le32(const void *buffer) {
  const u8 *p = buffer;
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static inline u64 read_le64(const void *buffer) {
  const u8 *p = buffer;
  return (u64)read_le32(p) | ((u64)read_le32(p + 4) << 32);
}
static inline void write_le16(void *buffer, u16 value) {
  u8 *p = buffer;
  p[0] = value;
  p[1] = value >> 8;
}
static inline void write_le32(void *buffer, u32 value) {
  u8 *p = buffer;
  for (unsigned i = 0; i < 4; i++)
    p[i] = value >> (i * 8);
}
static inline void write_le64(void *buffer, u64 value) {
  u8 *p = buffer;
  write_le32(p, value);
  write_le32(p + 4, value >> 32);
}
#define EFI __attribute__((ms_abi))
static inline void out8(u16 p, u8 v) {
  __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(p));
}
static inline void out16(u16 p, u16 v) {
  __asm__ volatile("outw %0,%1" ::"a"(v), "Nd"(p));
}
static inline void out32(u16 p, u32 v) {
  __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(p));
}
static inline u8 in8(u16 p) {
  u8 v;
  __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
static inline u16 in16(u16 p) {
  u16 v;
  __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
static inline u32 in32(u16 p) {
  u32 v;
  __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
static inline u64 cycles(void) {
  u32 lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((u64)hi << 32) | lo;
}
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);
int starts(const char *, const char *);
void strcopy(char *, const char *, size_t);
void serial(const char *);
void serial_num(u64);
void fatal(const char *);
extern u64 clock_hz;
extern u8 *dma;
extern u8 *program_memory;
extern u8 *boot_image;
extern u64 boot_image_size;
void gui_progress(const char *, const char *, u32, u32);
u32 crc32(const void *, size_t);
void disk_init(void);
void disk_list(void (*)(const char *));
int disk_install(unsigned, void (*)(const char *));
int disk_save(const void *, u32);
int disk_load(void *, u32, u32 *);
int disk_mounted(void);
unsigned disk_total(void);
const char *disk_name(unsigned);
u64 disk_mebibytes(unsigned);
unsigned package_total(void);
const char *package_name(unsigned);
int package_installed(unsigned);
unsigned installed_total(void);
const char *installed_name(unsigned);
int program_exists(const char *);
int desktop_save(void);
void desktop_restore(void);
void program_init(void);
int program_install(const char *, void (*)(const char *));
int program_install_blob(const char *, const u8 *, u32, void (*)(const char *));
int program_remove(const char *, void (*)(const char *));
u32 program_export(void *, u32);
int program_import(const void *, u32);
int assemble(const char *, u8 *, u32, char *, u32);

void program_list(void (*)(const char *));
int program_run(const char *, void (*)(const char *));
extern u64 usable_ram;
extern u8 boot_entropy[64];
extern int boot_entropy_ready;
extern u32 utc_days, utc_seconds;
extern u64 utc_started;
int platform_utc(u32 *, u32 *);
void *memmove(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
int tls_start(const char *);
int tls_ready(void);
int tls_error(void);
size_t tls_feed(const void *, size_t);
size_t tls_plain(void *, size_t);
size_t tls_write(const void *, size_t);
const void *tls_output(size_t *);
void tls_output_ack(size_t);
void tls_stop(void);
void font_init(void);
void font_clip(int, int, int, int);
void font_unclip(void);
int font_label_width(const char *, int);
void font_draw_label(int, int, const char *, u32, int);
int font_draw_ui(int, int, const char *, u32, int);
int font_web_measure(const char *, unsigned, int);
void font_draw_web_mono(int, int, const char *, u32, int);
void font_draw_web(int, int, const char *, u32, int);
int font_web_advance(int);
extern volatile u32 *framebuffer;
extern u32 *gui_canvas;
extern u32 *gui_previous;
extern volatile u64 timer_ticks, input_irq_ticks;
extern int input_irq_enabled;
int gui_animating(void);
extern u32 fb_width, fb_height, fb_stride, fb_format;
void gui_init(void);
void gui_key(int);
void gui_mouse(int, int, int);
void gui_render(void);
void gui_status(const char *);
void input_init(void);
void input_decode(u8);
void input_reset_decoder(void);
void input_poll(void);
unsigned input_layout(void);
int input_set_layout(const char *);
const char *input_layout_name(unsigned);
/* Native xHCI/Bulk-Only USB storage. The public interface has no write path. */
void usb_init(void);
void usb_rescan(void);
unsigned usb_total(void);
const char *usb_status(void);
int usb_read_sector(unsigned, u64, void *);
u64 usb_sectors(unsigned);
u32 usb_generation(unsigned);
struct FatEntry {
  char name[128];
  u32 cluster, size;
  int directory;
};
int fat_mount(unsigned);
int fat_list(u32, struct FatEntry *, unsigned);
int fat_read(const struct FatEntry *, void *, u32, u32 *);
int fat_read_prefix(const struct FatEntry *, void *, u32, u32 *);
u32 fat_root(void);
const char *fat_status(void);
void net_init(void);
void net_poll(void);
void net_stats(void (*)(const char *));
int net_ready(void);
const char *net_status(void);
int browser_load(const char *);
int browser_can_back(void);
int browser_can_forward(void);
int browser_verified(void);
void browser_back(void);
void browser_forward(void);
void browser_home(void);
void gui_wheel(int);
int browser_resource_load(const char *, int, int);
const char *browser_address(void);
int browser_busy(void);
void browser_cancel(void);
const char *browser_status(void);
extern char web_text[16384];
extern char web_title[96];
#endif
