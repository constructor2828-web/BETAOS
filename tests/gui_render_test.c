#include "os.h"
void font_clip(int x,int y,int w,int h){(void)x;(void)y;(void)w;(void)h;}
void font_unclip(void){}
int font_label_width(const char *s,int size){int n=0;while(s[n])n++;return n*size/2;}
void font_draw_label(int x,int y,const char *s,unsigned c,int z){(void)x;(void)y;(void)s;(void)c;(void)z;}
int browser_can_back(void){return 0;}
int browser_can_forward(void){return 0;}
int browser_verified(void){return 0;}
void browser_home(void){}
void browser_back(void){}
void browser_forward(void){}
int platform_utc(u32 *d,u32 *s){(void)d;(void)s;return 0;}
#include "os.h"
int font_draw_ui(int x,int y,const char *s,u32 c,int scale){(void)x;(void)y;(void)s;(void)c;(void)scale;return 0;}
void web_document(const char *u,const char *h){(void)u;(void)h;}
void web_paint(int x,int y,int w,int h,int s){(void)x;(void)y;(void)w;(void)h;(void)s;}
void web_click(int x,int y){(void)x;(void)y;}
int web_key(int c){(void)c;return 0;}
int web_scroll_limit(int h){(void)h;return 100;}
int web_active(void){return 1;}
int web_focus_scroll(int s,int h){(void)h;return s;}
void web_clear_focus(void){}
void web_cancel_pending(void){}
const char *browser_address(void){return "about:blank";}
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
u64 clock_hz = 1000000000, usable_ram = 512ull * 1024 * 1024;
volatile u64 timer_ticks, input_irq_ticks;
volatile u32 *framebuffer;
u32 *gui_canvas, *gui_previous;
u32 fb_width, fb_height, fb_stride, fb_format;
char web_text[16384] = "Render test", web_title[96] = "Renderer";
void serial(const char *s) { (void)s; }
void serial_num(u64 n) { (void)n; }
void strcopy(char *d, const char *s, size_t cap) {
  size_t i = 0;
  while (i + 1 < cap && s[i]) {
    d[i] = s[i];
    i++;
  }
  if (cap)
    d[i] = 0;
}
int starts(const char *a, const char *b) {
  return strncmp(a, b, strlen(b)) == 0;
}
int disk_mounted(void) { return 0; }
int disk_save(const void *p, u32 n) {
  (void)p;
  (void)n;
  return 0;
}
int disk_load(void *p, u32 n, u32 *size) {
  (void)p;
  (void)n;
  (void)size;
  return 0;
}
unsigned disk_total(void) { return 8; }
const char *disk_name(unsigned n) {
  (void)n;
  return "Test disk";
}
u64 disk_mebibytes(unsigned n) {
  (void)n;
  return 128;
}
void disk_list(void (*p)(const char *)) { (void)p; }
int disk_install(unsigned n, void (*p)(const char *)) {
  (void)n;
  (void)p;
  return 0;
}
unsigned package_total(void) { return 16; }
const char *package_name(unsigned n) {
  (void)n;
  return "package";
}
int package_installed(unsigned n) {
  (void)n;
  return 0;
}
void program_list(void (*p)(const char *)) { (void)p; }
int program_install(const char *n, void (*p)(const char *)) {
  (void)n;
  (void)p;
  return 1;
}
int program_remove(const char *n, void (*p)(const char *)) {
  (void)n;
  (void)p;
  return 1;
}
int program_run(const char *n, void (*p)(const char *)) {
  (void)n;
  (void)p;
  return 1;
}
int program_install_blob(const char *n, const u8 *p, u32 size,
                         void (*out)(const char *)) {
  (void)n;
  (void)p;
  (void)size;
  (void)out;
  return 1;
}
u32 program_export(void *p, u32 size) {
  if (size < 4)
    return 0;
  write_le32(p, 0);
  return 4;
}
int program_import(const void *p, u32 n) {
  (void)p;
  (void)n;
  return 1;
}
int assemble(const char *s, u8 *p, u32 n, char *message, u32 size) {
  (void)s;
  (void)p;
  (void)n;
  if (size)
    message[0] = 0;
  return 0;
}
int net_ready(void) { return 1; }
const char *net_status(void) { return "Connected"; }
void net_stats(void (*p)(const char *)) { (void)p; }
int browser_busy(void) { return 0; }
void browser_cancel(void) {}
int browser_load(const char *p) {
  (void)p;
  return 1;
}
const char *browser_status(void) { return "Ready"; }
unsigned installed_total(void) { return 16; }
const char *installed_name(unsigned n) {
  (void)n;
  return "installed_app";
}
int program_exists(const char *n) {
  (void)n;
  return 0;
}
unsigned input_layout(void) { return 0; }
int input_set_layout(const char *n) {
  (void)n;
  return 1;
}
const char *input_layout_name(unsigned n) {
  (void)n;
  return "us";
}
void usb_rescan(void) {}
unsigned usb_total(void) { return 1; }
const char *usb_status(void) { return "USB test"; }
int fat_mount(unsigned n) {
  (void)n;
  return 1;
}
u32 fat_root(void) { return 2; }
const char *fat_status(void) { return "FAT32 read only"; }
int fat_list(u32 dir, struct FatEntry *out, unsigned cap) {
  (void)dir;
  for (unsigned i = 0; i < cap; i++) {
    memset(&out[i], 0, sizeof(out[i]));
    snprintf(out[i].name, sizeof(out[i].name), "entry%u.txt", i);
    out[i].cluster = i + 2;
    out[i].directory = i % 3 == 0;
  }
  return cap;
}
int fat_read(const struct FatEntry *e, void *p, u32 cap, u32 *got) {
  (void)e;
  if (cap < 5)
    return 0;
  memcpy(p, "hello", 5);
  *got = 5;
  return 1;
}
int fat_read_prefix(const struct FatEntry *e, void *p, u32 cap, u32 *got) {
  return fat_read(e, p, cap, got);
}
#include "../desktop/gui.c"
static u32 *guarded(size_t count) {
  u32 *p = malloc((count + 16) * 4);
  assert(p);
  for (size_t i = 0; i < count + 16; i++)
    p[i] = 0x1a2b3c4d;
  return p;
}
static void guards(u32 *p, size_t n) {
  for (unsigned i = 0; i < 8; i++)
    assert(p[i] == 0x1a2b3c4d && p[n + 8 + i] == 0x1a2b3c4d);
}
int main(int argc, char **argv) {
  assert(argc == 4);
  fb_width = atoi(argv[1]);
  fb_height = atoi(argv[2]);
  fb_format = atoi(argv[3]);
  fb_stride = fb_width + 1;
  size_t count = (size_t)fb_stride * fb_height;
  u32 *a = guarded(count), *b = guarded(count), *c = guarded(count),
      *reference = malloc(count * 4);
  assert(reference);
  gui_canvas = a + 8;
  gui_previous = b + 8;
  framebuffer = c + 8;
  memset(gui_canvas, 0, count * 4);
  memset(gui_previous, 255, count * 4);
  gui_init();
  gui_progress("APP INSTALL FAILED",
               "App storage full. Uninstall an app first.", 1, 1);
  assert(progress_active && progress_failed && progress_percent == 100);
  gui_progress("APP INSTALLED", "Recovered", 1, 1);
  assert(progress_active && !progress_failed);
  gui_progress("", "", 0, 0);
  assert(!progress_active);
  for (int app = -1; app < 8; app++) {
    page = app;
    gears_paused = 1;
    dirty = 1;
    gui_render();
    guards(a, count);
    guards(b, count);
    guards(c, count);
  }
  page = 2;
  usb_choose(0);
  for (int i = 0; i < 10; i++) {
    files_scroll = i * 5;
    dirty = 1;
    gui_render();
    guards(a, count);
    guards(b, count);
    guards(c, count);
  }
  files_usb = 0;
  page = 2;
  selected_file = 0;
  for (unsigned i = 0; i < 60; i++)
    memcpy(files[0].data + i * 5, "line\n", 5);
  files[0].data[300] = 0;
  file_preview_scroll = 10;
  dirty = 1;
  gui_render();
  assert(file_preview_scroll == 10);
  file_preview_scroll = 999;
  dirty = 1;
  gui_render();
  assert(file_preview_scroll > 10 && file_preview_scroll <= 60);
  usb_choose(0);
  file_sort = 1;
  files_build_order();
  assert(file_order_count == 96 && file_order[0] == 0 && file_order[1] == 12);
  file_sort = 0;
  files_usb = 0;
  launcher = 1;
  strcopy(launcher_query, "terminal", sizeof(launcher_query));
  launcher_build();
  assert(launcher_count == 1);
  gui_key('\n');
  assert(page == 1 && !launcher);
  launcher = 1;
  strcopy(launcher_query, "no-such-app", sizeof(launcher_query));
  launcher_build();
  assert(!launcher_count);
  gui_key('\n');
  assert(page == 1 && launcher);
  gui_key(27);
  assert(page == 1 && !launcher);
  launcher_query[0] = 0;
  launcher = 1;
  dirty = 1;
  gui_render();
  guards(a, count);
  guards(b, count);
  guards(c, count);
  launcher = 0;
  page = 4;
  windows[4].maximized = 0;
  dirty = 1;
  gears_paused = 1;
  gui_render();
  for (unsigned iteration = 0; iteration < 40; iteration++) {
    gears_paused = 0;
    dirty = 0;
    last_animation = 0;
    animation_time = cycles() - clock_hz / 60;
    last_second = (cycles() - started) / clock_hz;
    gui_render();
    memcpy(reference, gui_canvas, count * 4);
    gears_paused = 1;
    dirty = 1;
    gui_render();
    int x, y, w, h;
    window_box(&x, &y, &w, &h);
    for (int row = y + 90; row < y + h - 38; row++)
      for (int col = x + 10; col < x + w - 10; col++)
        assert(reference[row * fb_stride + col] ==
               gui_canvas[row * fb_stride + col]);
    guards(a, count);
    guards(b, count);
    guards(c, count);
  }
  assert(animation_damage_frames >= 20);
  for (unsigned i = 0; i < 500; i++) {
    int x = (int)(i * 37 % (fb_width + 100)) - 50,
        y = (int)(i * 23 % (fb_height + 100)) - 50;
    rect(x, y, (i * 19) % 100, (i * 13) % 100, INK);
    rounded(x, y, 30, 30, 8, ACCENT);
  }
  guards(a, count);
  guards(b, count);
  guards(c, count);
  free(a);
  free(b);
  free(c);
  free(reference);
  printf("PASS: %ux%u format=%u odd stride, all app bounds and cached "
         "animation matches full renderer\n",
         fb_width, fb_height, fb_format);
}
