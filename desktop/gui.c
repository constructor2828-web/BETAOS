#include "os.h"
#include "web.h"
#include <string.h>
#define BG 0x151922
#define PANEL 0x242932
#define INK 0xe8edf5
#define MUTED 0xa3adbd
#define ACCENT 0x69b3ff
static int page = -1, dirty = 1, mx = 500, my = 380, last_buttons;
static int launcher, browser_scroll, selected_file = -1;
static unsigned launcher_scroll;
static int browser_address_selected, browser_editing;
static char painted_browser_url[2048];
static int drawing_clip, clip_left, clip_top, clip_right, clip_bottom;
static unsigned launcher_selected;
static char launcher_query[32];
static char drafts[8][640];
static int draft_overflow[8];
static int install_target = -1, erase_confirmed;
static char history[32][640], history_draft[640];
static unsigned history_count, history_pos, cursor_pos;
static int progress_active, progress_failed;
static int terminal_scroll;
static int files_scroll, package_scroll, disk_scroll, dialog_from_files,
    last_file = -1;
static u64 last_file_click;
static int cursor_dirty = 1, painted_mx = -1, painted_my = -1;
static u64 gear_frames, animation_damage_frames, last_scene_time;
static u64 rendered_frames, cursor_frames, presented_bytes, last_render_us,
    max_render_us;
static u64 gear_angle, last_animation, animation_time;
static u32 progress_percent;
static char progress_title[64], progress_label[128];
static int gears_paused, gear_speed = 1;

static char editor[8192] =
    "; BETAOS native x86-64 / Intel syntax\nbits 64\nstart:\n  push rbx\n  mov "
    "rbx, rdi\n  lea rdi, [rel msg]\n  call [rbx+8]\n  pop rbx\n  ret\nmsg: db "
    "\"Hello from the built-in assembler!\", 10, 0\n";
static unsigned editor_used, editor_cursor;
static char editor_result[128] = "Ctrl-S save / Ctrl-B assemble / Ctrl-R run";
static u8 assembled[65552];
static void switch_page(int);
static void activate_desktop(int);
static void files_clamp_scroll(void);

static u64 started, last_second;
static int opened[8];
struct WindowState {
  int x, y, maximized, w, h, minimized;
};
static struct WindowState windows[8];
static int window_order[8], window_count, window_resize, resize_w, resize_h,
    resize_x, resize_y, draw_active = 1;
static void window_reveal(void);
static void window_minimize(void);
static int window_drag, window_drag_moved, window_grab_x, window_grab_y,
    window_press_x, window_press_y, last_title_page = -1;
static u64 last_title_click;
static unsigned editor_goal_column = ~0u, editor_column_offset;
static int editor_modified;
static int editor_search;
static char editor_query[96];
struct EditSnapshot {
  char text[8192];
  unsigned cursor;
};
static struct EditSnapshot editor_undo[16], editor_redo[16];
static unsigned undo_head, undo_count, redo_count;
static char editor_saved_text[8192];
static int pending_editor = -2;
static void editor_request_open(int);
static void editor_request_close(void);

struct DesktopIcon {
  int x, y, style;
  char label[32];
};
struct DesktopConfig {
  u32 version;
  struct DesktopIcon icons[25];
  char editor_name[32];
};
_Static_assert(sizeof(struct DesktopIcon) == 44 &&
                   sizeof(struct DesktopConfig) == 1136,
               "Persistent desktop layout changed; migrate its schema");
static struct DesktopConfig desktop_config;
static int desktop_selected = -1, drag_icon = -1, dragging, drag_dx, drag_dy,
           drag_start_x, drag_start_y, last_icon = -1;
static u64 last_icon_click;
static int context_open, context_x, context_y, dialog_mode;
static char dialog_text[32], dialog_error[80];
static char editor_name[32] = "scratch.asm";
static struct WindowState *current_window(void) {
  return &windows[page >= 0 ? page : 0];
}
static void window_box(int *x, int *y, int *w, int *h) {
  struct WindowState *v = current_window();
  if (v->maximized) {
    *x = 0;
    *y = 0;
    *w = fb_width;
    *h = fb_height - 52;
  } else {
    *x = v->x;
    *y = v->y;
    *w = v->w ? v->w : (int)fb_width - 96;
    *h = v->h ? v->h : (int)fb_height - 208;
  }
}
static int hover_zone(void) {
  if (my >= (int)fb_height - 52 && mx >= 100 && mx < 484)
    return 1 + (mx - 100) / 64;
  if (launcher) {
    int top = fb_height - 52 - 388;
    if (mx >= 8 && mx < 348 && my >= top + 66 && my < top + 330)
      return 10 + (my - top - 66) / 44;
    if (mx >= 356 && mx < 696 && my >= top + 52 && my < top + 332)
      return 40 + (my - top - 52) / 40;
  }
  if (context_open && mx >= context_x && mx < context_x + 230 &&
      my >= context_y + 6 && my < context_y + 166)
    return 20 + (my - context_y - 6) / 32;
  return 0;
}
static char input[640], notice[160] = "Bare metal. 64-bit. Assembly + C.";
static unsigned used;
static char terminal[16384] =
    "BETAOS NATIVE TERMINAL\nType help for commands.\n";
static struct {
  char name[32], data[8192];
} files[16];
static int files_usb, usb_selected = -1, usb_entries_count;
static unsigned usb_drive, usb_depth;
static u32 usb_directories[16];
static char usb_path[16][32];
static struct FatEntry usb_entries[96];
static char file_message[160] =
    "Ctrl-F search / Ctrl-Z restore / Ctrl-C copy / Ctrl-V paste";
static char file_clipboard[8192], clipboard_name[32], file_filter[32];
static int file_search;
static unsigned file_sort;
static int file_order[96], file_order_count, file_preview_scroll;
static struct {
  char name[32], data[8192];
  struct DesktopIcon icon;
} deleted_file;
static u8 usb_file_data[65553];
static char usb_preview_text[8192];
static int name_contains(const char *name, const char *filter) {
  if (!*filter)
    return 1;
  for (; *name; name++) {
    const char *a = name, *b = filter;
    while (*a && *b) {
      int ca = (u8)*a, cb = (u8)*b;
      if (ca >= 'A' && ca <= 'Z')
        ca += 32;
      if (cb >= 'A' && cb <= 'Z')
        cb += 32;
      if (ca != cb)
        break;
      a++;
      b++;
    }
    if (!*b)
      return 1;
  }
  return 0;
}
static int file_matches(const char *name) {
  return name_contains(name, file_filter);
}
static const char *file_view_name(int i) {
  return files_usb ? usb_entries[i].name : files[i].name;
}
static u32 file_view_size(int i) {
  return files_usb ? usb_entries[i].size : strlen(files[i].data);
}
static int file_compare(int a, int b) {
  if (files_usb && usb_entries[a].directory != usb_entries[b].directory)
    return usb_entries[a].directory ? -1 : 1;
  if (file_sort == 2 && file_view_size(a) != file_view_size(b))
    return file_view_size(a) > file_view_size(b) ? -1 : 1;
  const u8 *x = (const u8 *)file_view_name(a),
           *y = (const u8 *)file_view_name(b);
  while (*x && *y) {
    int ca = *x++, cb = *y++;
    if (ca >= 'A' && ca <= 'Z')
      ca += 32;
    if (cb >= 'A' && cb <= 'Z')
      cb += 32;
    if (ca != cb)
      return ca - cb;
  }
  return *x - *y;
}
static void files_build_order(void) {
  file_order_count = 0;
  int count = files_usb ? usb_entries_count : 16;
  for (int i = 0; i < count && i < 96; i++)
    if (*file_view_name(i) && file_matches(file_view_name(i))) {
      int at = file_order_count++;
      while (file_sort && at && file_compare(i, file_order[at - 1]) < 0) {
        file_order[at] = file_order[at - 1];
        at--;
      }
      file_order[at] = i;
    }
}
static const char *launcher_builtins[] = {
    "Browser",  "Terminal",   "Files",    "System",
    "Gears", "ASM Editor", "Install OS", "ASM Apps"};
/* Negative values encode built-in pages; nonnegative values index installed
 * apps. */
static int launcher_order[24];
static unsigned launcher_count;
static void launcher_build(void) {
  launcher_count = 0;
  if (*launcher_query)
    for (unsigned i = 0; i < 8; i++)
      if (name_contains(launcher_builtins[i], launcher_query))
        launcher_order[launcher_count++] = -(int)i - 1;
  for (unsigned i = 0; i < installed_total() && launcher_count < 24; i++)
    if (name_contains(installed_name(i), launcher_query))
      launcher_order[launcher_count++] = i;
  if (launcher_selected >= launcher_count)
    launcher_selected = 0;
  if (launcher_scroll >= launcher_count)
    launcher_scroll = 0;
}
static void launcher_toggle(void) {
  launcher = !launcher;
  context_open = 0;
  launcher_query[0] = 0;
  launcher_selected = launcher_scroll = 0;
  launcher_build();
  dirty = 1;
}
static int file_name_equal(const char *a, const char *b) {
  while (*a && *b) {
    int ca = (u8)*a++, cb = (u8)*b++;
    if (ca >= 'A' && ca <= 'Z')
      ca += 32;
    if (cb >= 'A' && cb <= 'Z')
      cb += 32;
    if (ca != cb)
      return 0;
  }
  return !*a && !*b;
}
static const u8 glyphs[][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14},      {7, 2, 2, 2, 18, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}};
static u8 glyph(char c, unsigned y) {
  unsigned char latin = c;
  if (latin == 0xf1 || latin == 0xd1)
    return y == 0 ? 10 : y == 1 ? 5 : glyph(latin == 0xf1 ? 'n' : 'N', y);
  if (latin >= 0xc0) {
    const char *base =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    if ((unsigned)(latin - 0xc0) < strlen(base))
      return y == 0 ? 4 : glyph(base[latin - 0xc0], y);
  }
  static const u8 lowercase[26][7] = {
      {0, 0, 14, 1, 15, 17, 15},    {16, 16, 22, 25, 17, 17, 30},
      {0, 0, 14, 17, 16, 17, 14},   {1, 1, 13, 19, 17, 17, 15},
      {0, 0, 14, 17, 31, 16, 14},   {6, 9, 8, 28, 8, 8, 8},
      {0, 15, 17, 17, 15, 1, 14},   {16, 16, 22, 25, 17, 17, 17},
      {4, 0, 12, 4, 4, 4, 14},      {2, 0, 6, 2, 2, 18, 12},
      {16, 16, 18, 20, 24, 20, 18}, {12, 4, 4, 4, 4, 4, 14},
      {0, 0, 26, 21, 21, 21, 21},   {0, 0, 22, 25, 17, 17, 17},
      {0, 0, 14, 17, 17, 17, 14},   {0, 30, 17, 17, 30, 16, 16},
      {0, 15, 17, 17, 15, 1, 1},    {0, 0, 22, 25, 16, 16, 16},
      {0, 0, 15, 16, 14, 1, 30},    {8, 8, 28, 8, 8, 9, 6},
      {0, 0, 17, 17, 17, 19, 13},   {0, 0, 17, 17, 17, 10, 4},
      {0, 0, 17, 17, 21, 21, 10},   {0, 0, 17, 10, 4, 10, 17},
      {0, 17, 17, 17, 15, 1, 14},   {0, 0, 31, 2, 4, 8, 31}};
  if (c >= 'a' && c <= 'z')
    return lowercase[c - 'a'][y];
  if (c >= 'A' && c <= 'Z')
    return glyphs[c - 'A'][y];
  if (c >= '0' && c <= '9')
    return glyphs[26 + c - '0'][y];
  switch (c) {
  case '.':
    return y == 6 ? 4 : 0;
  case ',':
    return y == 5 ? 4 : y == 6 ? 8 : 0;
  case ':':
    return y == 2 || y == 5 ? 4 : 0;
  case ';':
    return y == 2 ? 4 : y == 5 ? 4 : y == 6 ? 8 : 0;
  case '-':
    return y == 3 ? 14 : 0;
  case '_':
    return y == 6 ? 31 : 0;
  case '/':
    return (y < 5) ? 1 << (y) : 16;
  case '\\':
    return y < 5 ? 16 >> y : 1;
  case '[':
    return y == 0 || y == 6 ? 14 : 8;
  case ']':
    return y == 0 || y == 6 ? 14 : 2;
  case '(':
    return y == 0 || y == 6 ? 2 : 4;
  case ')':
    return y == 0 || y == 6 ? 8 : 4;
  case '>':
    return y == 1 || y == 5 ? 8 : y == 2 || y == 4 ? 4 : y == 3 ? 2 : 0;
  case '<':
    return y == 1 || y == 5 ? 2 : y == 2 || y == 4 ? 4 : y == 3 ? 8 : 0;
  case '=':
    return y == 2 || y == 4 ? 31 : 0;
  case '+':
    return y == 3 ? 31 : y > 0 && y < 6 ? 4 : 0;
  case '!':
    return y < 4 || y == 6 ? 4 : 0;
  case '?':
    return y == 0 ? 14 : y == 1 ? 17 : y == 2 ? 2 : y == 3 || y == 5 ? 4 : 0;
  case '\'':
    return y < 2 ? 4 : 0;
  case '"':
    return y < 2 ? 10 : 0;
  case '#':
    return y == 2 || y == 4 ? 31 : 10;
  case '*':
    return y == 2 ? 21 : y == 3 ? 14 : y == 4 ? 21 : 0;
  case '|':
    return 4;
  case '&':
    return y == 0   ? 12
           : y == 1 ? 18
           : y == 2 ? 20
           : y == 3 ? 8
           : y == 4 ? 21
           : y == 5 ? 18
                    : 13;
  case '@':
    return y == 0 || y == 6 ? 14 : y == 1 || y == 5 ? 17 : 23;
  default:
    return 0;
  }
}
static u32 native(u32 c) {
  return fb_format == 0 ? ((c & 255) << 16) | (c & 0xff00) | (c >> 16) : c;
}
static void rect(int x, int y, int w, int h, u32 c) {
  if (drawing_clip) {
    if (x < clip_left) {
      w -= clip_left - x;
      x = clip_left;
    }
    if (y < clip_top) {
      h -= clip_top - y;
      y = clip_top;
    }
    if (x + w > clip_right)
      w = clip_right - x;
    if (y + h > clip_bottom)
      h = clip_bottom - y;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > (int)fb_width)
    w = fb_width - x;
  if (y + h > (int)fb_height)
    h = fb_height - y;
  u32 p = native(c);
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++)
      gui_canvas[(y + j) * fb_stride + x + i] = p;
}
static void text(int x, int y, const char *s, u32 c, int scale) {
  if (font_draw_ui(x, y, s, c, scale))
    return;
  while (*s) {
    for (unsigned j = 0; j < 7; j++) {
      u8 bits = glyph(*s, j);
      for (unsigned i = 0; i < 5; i++)
        if (bits & (16 >> i))
          rect(x + i * scale, y + j * scale, scale, scale, c);
    }
    x += 6 * scale;
    s++;
  }
}
static void text_fit(int x, int y, const char *s, u32 color, int scale,
                     int width) {
  int limit = width / (6 * scale);
  for (int i = 0; s[i] && i < limit; i++) {
    char c[2] = {s[i], 0};
    text(x + i * 6 * scale, y, c, color, scale);
  }
}
static void block_view(int x, int y, int w, int h, const char *s, u32 c,
                       int skip) {
  int px = x, py = y - skip * 18;
  for (; *s && py + 14 < y + h; s++) {
    if (*s == '\r')
      continue;
    if (*s == '\n') {
      px = x;
      py += 18;
      continue;
    }
    if (px + 12 > x + w) {
      px = x;
      py += 18;
    }
    if (py + 14 >= y + h)
      break;
    char ch[2] = {*s, 0};
    if (py >= y)
      text(px, py, ch, c, 2);
    px += 12;
  }
}
static void block(int x, int y, int w, int h, const char *s, u32 c) {
  block_view(x, y, w, h, s, c, 0);
}
static void blend_pixel(int x, int y, u32 color, unsigned alpha) {
  if (x < 0 || y < 0 || x >= (int)fb_width || y >= (int)fb_height || !alpha)
    return;
  if (drawing_clip &&
      (x < clip_left || x >= clip_right || y < clip_top || y >= clip_bottom))
    return;
  u32 source = native(color), old = gui_canvas[y * fb_stride + x], value = 0;
  if (alpha > 255)
    alpha = 255;
  for (unsigned shift = 0; shift < 24; shift += 8)
    value |= ((((old >> shift) & 255) * (255 - alpha) +
               ((source >> shift) & 255) * alpha + 127) /
              255)
             << shift;
  gui_canvas[y * fb_stride + x] = value;
}
static void rounded(int x, int y, int w, int h, int radius, u32 color) {
  if (w <= 0 || h <= 0)
    return;
  if (radius < 0)
    radius = 0;
  if (radius > w / 2)
    radius = w / 2;
  if (radius > h / 2)
    radius = h / 2;
  if (radius > 128)
    radius = 128;
  int first =
      y<0 ? -y : 0, last = y + h>(int) fb_height ? (int)fb_height - y : h;
  if (drawing_clip) {
    if (y + first < clip_top)
      first = clip_top - y;
    if (y + last > clip_bottom)
      last = clip_bottom - y;
  }
  for (int row = first; row < last; row++) {
    int mirror = row < h / 2 ? row : h - row - 1;
    if (mirror >= radius || !radius) {
      rect(x, y + row, w, 1, color);
      continue;
    }
    rect(x + radius, y + row, w - 2 * radius, 1, color);
    for (int col = 0; col < radius; col++) {
      unsigned samples = 0;
      for (int sy = 1; sy < 8; sy += 2)
        for (int sx = 1; sx < 8; sx += 2) {
          int dx = 8 * radius - (8 * col + sx),
              dy = 8 * radius - (8 * mirror + sy);
          if (dx * dx + dy * dy <= 64 * radius * radius)
            samples++;
        }
      if (samples) {
        blend_pixel(x + col, y + row, color, (samples * 255 + 8) / 16);
        blend_pixel(x + w - 1 - col, y + row, color, (samples * 255 + 8) / 16);
      }
    }
  }
}
static void ui_label(int x, int y, const char *s, u32 color, int size,
                     int width) {
  if (width <= 0)
    return;
  char visible[256] = {0};
  unsigned n = 0;
  while (s[n] && n + 1 < sizeof(visible)) {
    visible[n] = s[n];
    visible[n + 1] = 0;
    if (font_label_width(visible, size) > width) {
      visible[n] = 0;
      break;
    }
    n++;
  }
  font_draw_label(x, y, visible, color, size);
}
static void icon(int app, int x, int y, u32 color) {
  if (app == 6) {
    rounded(x + 3, y, 22, 28, 2, color);
    rect(x + 6, y + 4, 16, 20, PANEL);
    for (int i = 0; i < 3; i++)
      rect(x + 9, y + 8 + i * 5, 10, 2, color);
    return;
  }
  if (app == 4) {
    for (int i = 0; i < 28; i++)
      for (int j = 0; j < 28; j++) {
        int a = i - 14, b = j - 14, d = a * a + b * b;
        if ((d > 45 && d < 120) ||
            ((i < 3 || i > 24 || j < 3 || j > 24) && d < 200))
          rect(x + i, y + j, 1, 1, color);
      }
    return;
  }
  if (app == 5) {
    text(x, y + 5, "</>", color, 2);
    return;
  }
  if (app == -1) {
    for (int j = 0; j < 2; j++)
      for (int i = 0; i < 2; i++)
        rounded(x + i * 14, y + j * 14, 10, 10, 2, color);
  } else if (app == 0) {
    for (int j = -13; j <= 13; j++)
      for (int i = -13; i <= 13; i++) {
        int d = i * i + j * j;
        if ((d >= 140 && d <= 169) ||
            (d <= 169 && (i == 0 || j == 0 || i == 7 || i == -7)))
          rect(x + 13 + i, y + 13 + j, 1, 1, color);
      }
  } else if (app == 1) {
    rounded(x, y, 28, 24, 3, color);
    rect(x + 2, y + 2, 24, 20, PANEL);
    text(x + 5, y + 7, ">_", color, 1);
  } else if (app == 2) {
    rounded(x, y + 4, 29, 21, 3, color);
    rounded(x + 1, y, 12, 9, 2, color);
    rect(x + 4, y + 11, 21, 2, PANEL);
  } else {
    for (int i = 0; i < 3; i++) {
      rect(x + 2, y + 4 + i * 8, 25, 2, color);
      rounded(x + 6 + (i % 2) * 10, y + i * 8, 6, 10, 2, color);
    }
  }
}
static void uptime_text(char *result, u64 second) {
  unsigned h = (second / 3600) % 100, m = (second / 60) % 60, sec = second % 60;
  result[0] = '0' + h / 10;
  result[1] = '0' + h % 10;
  result[2] = ':';
  result[3] = '0' + m / 10;
  result[4] = '0' + m % 10;
  result[5] = ':';
  result[6] = '0' + sec / 10;
  result[7] = '0' + sec % 10;
  result[8] = 0;
}
static void append(const char *s) {
  serial(s);
  terminal_scroll = 0;
  size_t n = strlen(terminal), m = strlen(s);
  if (n + m >= sizeof(terminal) - 1) {
    terminal[0] = 0;
    n = 0;
  }
  strcopy(terminal + n, s, sizeof(terminal) - n);
}
static void term_number(u64 n) {
  char b[24], r[24];
  unsigned i = 0, j = 0;
  do {
    b[i++] = '0' + n % 10;
    n /= 10;
  } while (n);
  while (i)
    r[j++] = b[--i];
  r[j] = 0;
  append(r);
}
static char *word(char *s) {
  while (*s && *s != ' ')
    s++;
  if (*s)
    *s++ = 0;
  while (*s == ' ')
    s++;
  return s;
}

void gui_progress(const char *title, const char *label, u32 current,
                  u32 total) {
  progress_active = total != 0;
  progress_failed = starts(title, "INSTALLATION FAILED") ||
                    starts(title, "APP INSTALL FAILED") ||
                    starts(title, "ASSEMBLY ERROR") ||
                    starts(title, "SAVE FAILED");
  if (progress_active && progress_failed) {
    serial("UI ERROR: ");
    serial(title);
    serial(": ");
    serial(label);
    serial("\n");
  }
  if (total) {
    progress_percent = (u64)current * 100 / total;
    if (progress_percent > 100)
      progress_percent = 100;
    strcopy(progress_title, title, sizeof(progress_title));
    strcopy(progress_label, label, sizeof(progress_label));
  }
  dirty = 1;
  gui_render();
}
static int sine(int phase) {
  phase &= 1023;
  int sign = 1;
  if (phase > 512) {
    sign = -1;
    phase -= 512;
  }
  int64_t n = (int64_t)phase * (512 - phase);
  return sign * (16 * n * 16384 / (5 * 512 * 512 - 4 * n));
}
static void triangle(int ax, int ay, int bx, int by, int cx, int cy,
                     u32 color) {
  int xs[3] = {ax, bx, cx}, ys[3] = {ay, by, cy};
  int top = ay < by ? ay : by;
  if (cy < top)
    top = cy;
  int bottom = ay > by ? ay : by;
  if (cy > bottom)
    bottom = cy;
  for (int y = top; y < bottom; y++) {
    int64_t low = 0, high = 0;
    unsigned hits = 0;
    for (int i = 0; i < 3; i++) {
      int j = (i + 1) % 3;
      if ((ys[i] <= y && ys[j] > y) || (ys[j] <= y && ys[i] > y)) {
        int64_t x = (int64_t)xs[i] * 65536 + (int64_t)(xs[j] - xs[i]) *
                                                 (2 * (y - ys[i]) + 1) * 32768 /
                                                 (ys[j] - ys[i]);
        if (!hits)
          low = high = x;
        else {
          if (x < low)
            low = x;
          if (x > high)
            high = x;
        }
        hits++;
      }
    }
    if (hits >= 2) {
      int left = (low + 32767) >> 16, right = (high - 32768) >> 16;
      if (right >= left)
        rect(left, y, right - left + 1, 1, color);
    }
  }
}
static void disc(int x, int y, int radius, u32 color) {
  int extent = radius;
  for (int row = 0; row <= radius; row++) {
    while (extent > 0 && extent * extent + row * row > radius * radius)
      extent--;
    rect(x - extent, y + row, extent * 2 + 1, 1, color);
    if (row)
      rect(x - extent, y - row, extent * 2 + 1, 1, color);
  }
}
static void gear(int x, int y, int radius, int teeth, int phase, u32 color) {
  disc(x + 4, y + 7, radius, 0x080f1c);
  disc(x, y, radius - 7, color);
  for (int i = 0; i < teeth; i++) {
    int a = phase + i * 1024 / teeth, b = a + 1024 / teeth / 2;
    int ax = x + (int64_t)sine(a + 256) * (radius - 10) / 16384,
        ay = y + (int64_t)sine(a) * (radius - 10) / 16384;
    int bx = x + (int64_t)sine(a + 256) * (radius + 8) / 16384,
        by = y + (int64_t)sine(a) * (radius + 8) / 16384;
    int cx = x + (int64_t)sine(b + 256) * (radius + 8) / 16384,
        cy = y + (int64_t)sine(b) * (radius + 8) / 16384;
    int dx = x + (int64_t)sine(b + 256) * (radius - 10) / 16384,
        dy = y + (int64_t)sine(b) * (radius - 10) / 16384;
    triangle(ax, ay, bx, by, cx, cy, color);
    triangle(ax, ay, cx, cy, dx, dy, color);
  }
  disc(x, y, radius * 2 / 3, 0x24344b);
  disc(x, y, radius * 2 / 3 - 4, PANEL);
  for (int i = 0; i < 4; i++) {
    int a = phase + i * 256;
    int px = x + sine(a + 256) * radius / 3 / 16384,
        py = y + sine(a) * radius / 3 / 16384;
    disc(px, py, 8, color);
  }
  disc(x, y, 17, color);
  disc(x, y, 8, 0x0b1424);
}
static void draw_gear_shapes(int x, int y, int w, int h) {
  int phase = gear_angle / 65536;
  int radius = h < 500 ? 65 : 90, small = h < 500 ? 50 : 70;
  int cx = x + w / 2, cy = y + h / 2 + 10, top = cy - radius / 2;
  gear(cx - radius, top, radius, 18, phase, 0xff788b);
  gear(cx + radius + 2, top, radius, 18, -phase + 29, 0x54d6ce);
  gear(cx - radius, top + radius + small + 4, small, 14, -phase * 18 / 14 + 18,
       0xffcc66);
}
static u8 state_data[2 * 1024 * 1024];
int desktop_save(void) {
  if (!disk_mounted())
    return 0;
  memset(state_data, 0, 16);
  memcpy(state_data, "BDS2", 4);
  write_le32(state_data + 4, sizeof(files));
  memcpy(state_data + 16, files, sizeof(files));
  desktop_config.version = 1 | (input_layout() << 16);
  strcopy(desktop_config.editor_name, editor_name, sizeof(editor_name));
  write_le32(state_data + 12, sizeof(desktop_config));
  memcpy(state_data + 16 + sizeof(files), &desktop_config,
         sizeof(desktop_config));
  u32 bytes = program_export(
      state_data + 16 + sizeof(files) + sizeof(desktop_config),
      sizeof(state_data) - 16 - sizeof(files) - sizeof(desktop_config));
  if (!bytes)
    return 0;
  write_le32(state_data + 8, bytes);
  return disk_save(state_data,
                   16 + sizeof(files) + sizeof(desktop_config) + bytes);
}
static void translate_default_icons(void) {
  static const char *old_names[] = {
      "Navegador", "Terminal", "Archivos", "Sistema", "Engranajes",
      "Editor ASM", "Programa ASM", "Apps ASM", "Instalar SO"};
  static const char *new_names[] = {
      "Browser", "Terminal", "Files", "System", "Gears",
      "ASM Editor", "ASM Program", "ASM Apps", "Install OS"};
  /* Only migrate the old defaults; keep names the user chose. */
  for (unsigned i = 0; i < 9; i++)
    if (!strcmp(desktop_config.icons[i].label, old_names[i]))
      strcopy(desktop_config.icons[i].label, new_names[i], 32);
}
void desktop_restore(void) {
  if (editor_modified) {
    append("Restore blocked: save or discard editor changes first.\n");
    return;
  }
  u32 bytes = 0;
  if (!disk_load(state_data, sizeof(state_data), &bytes) ||
      bytes < 16 + sizeof(files))
    return;
  if (state_data[0] != 'B' || state_data[1] != 'D' || state_data[2] != 'S' ||
      state_data[3] != '2' || read_le32(state_data + 4) != sizeof(files))
    return;
  u32 apps = read_le32(state_data + 8);
  if (read_le32(state_data + 12) != sizeof(desktop_config) ||
      bytes < 16 + sizeof(files) + sizeof(desktop_config) ||
      apps != bytes - 16 - sizeof(files) - sizeof(desktop_config))
    return;
  for (unsigned i = 0; i < 16; i++) {
    const u8 *f = state_data + 16 + i * sizeof(files[0]);
    if (f[31] || f[sizeof(files[0]) - 1])
      return;
  }
  const u8 *config_bytes = state_data + 16 + sizeof(files);
  u32 config_version = read_le32(config_bytes);
  if ((config_version & 65535) != 1 || (config_version >> 16) >= 4 ||
      config_bytes[sizeof(desktop_config) - 1])
    return;
  for (unsigned i = 0; i < 25; i++)
    if (config_bytes[4 + i * sizeof(struct DesktopIcon) + 43])
      return;
  if (!program_import(state_data + 16 + sizeof(files) + sizeof(desktop_config),
                      apps))
    return;
  memcpy(files, state_data + 16, sizeof(files));
  memcpy(&desktop_config, state_data + 16 + sizeof(files),
         sizeof(desktop_config));
  input_set_layout(input_layout_name(config_version >> 16));
  for (int i = 0; i < 25; i++) {
    struct DesktopIcon *v = &desktop_config.icons[i];
    if (v->x < 0 || v->x > (int)fb_width - 100)
      v->x = 16 + (i / 6) * 112;
    if (v->y < 0 || v->y > (int)fb_height - 126)
      v->y = 20 + (i % 6) * 80;
    if (v->style < 0 || v->style > 6)
      v->style = 6;
    v->label[31] = 0;
  }
  translate_default_icons();
  desktop_config.editor_name[31] = 0;
  if (desktop_config.editor_name[0])
    strcopy(editor_name, desktop_config.editor_name, sizeof(editor_name));
  serial("DESKTOP LAYOUT RESTORED\n");
  serial("PERSISTENT STATE RESTORED\n");
  for (unsigned i = 0; i < 16; i++)
    if (!strcmp(files[i].name, editor_name)) {
      strcopy(editor, files[i].data, sizeof(editor));
      editor_used = strlen(editor);
      editor_cursor = editor_used;
      strcopy(editor_saved_text, editor, sizeof(editor));
      editor_modified = 0;
    }
}
static int file_find(const char *name) {
  if (!name || !*name)
    return -1;
  for (int i = 0; i < 16; i++)
    if (files[i].name[0] && !strcmp(files[i].name, name))
      return i;
  return -1;
}
static int file_name_valid(const char *name) {
  if (!name || !*name || strlen(name) > 31 || !strcmp(name, ".") ||
      !strcmp(name, ".."))
    return 0;
  for (const u8 *p = (const u8 *)name; *p; p++)
    if (*p < 32 || *p == 127 || *p == '/' || *p == '\\')
      return 0;
  return 1;
}
static int file_put(const char *name, const char *data) {
  if (!file_name_valid(name) || strlen(data) >= sizeof(files[0].data))
    return 0;
  int i = file_find(name);
  if (i < 0)
    for (int j = 0; j < 16; j++)
      if (!files[j].name[0]) {
        i = j;
        break;
      }
  if (i < 0)
    return 0;
  strcopy(files[i].name, name, 32);
  strcopy(files[i].data, data, sizeof(files[i].data));
  return 1;
}
static void files_message(const char *s) {
  strcopy(file_message, s, sizeof(file_message));
  serial("FILES: ");
  serial(s);
  serial("\n");
  dirty = 1;
}
static void file_delete(int f) {
  if (f < 0 || f >= 16 || !files[f].name[0])
    return;
  strcopy(deleted_file.name, files[f].name, sizeof(deleted_file.name));
  strcopy(deleted_file.data, files[f].data, sizeof(deleted_file.data));
  deleted_file.icon = desktop_config.icons[9 + f];
  if (!strcmp(editor_name, files[f].name)) {
    editor_modified = 1;
    strcopy(editor_result, "File deleted; Ctrl-Z in Files restores it.",
            sizeof(editor_result));
  }
  serial("FILE DELETED: ");
  serial(files[f].name);
  serial("\n");
  files[f].name[0] = 0;
  if (selected_file == f)
    selected_file = -1;
  if (desktop_selected == 9 + f)
    desktop_selected = -1;
  files_message("File deleted. Ctrl-Z in Local restores the last deletion.");
}
static void file_undelete(void) {
  if (files_usb && page == 2) {
    files_message("USB is read-only; restore in Local.");
    return;
  }
  if (!deleted_file.name[0]) {
    files_message("No deleted file to restore in this session.");
    return;
  }
  if (file_find(deleted_file.name) >= 0) {
    files_message("That name already exists. Rename it before restoring.");
    return;
  }
  if (!file_put(deleted_file.name, deleted_file.data)) {
    files_message("Not enough space to restore the file.");
    return;
  }
  selected_file = file_find(deleted_file.name);
  desktop_config.icons[9 + selected_file] = deleted_file.icon;
  if (!strcmp(editor_name, deleted_file.name))
    editor_modified = strcmp(editor, editor_saved_text) != 0;
  deleted_file.name[0] = 0;
  if (disk_mounted())
    desktop_save();
  file_filter[0] = 0;
  file_preview_scroll = files_scroll = 0;
  files_message("File restored.");
}
static void usb_directory(void) {
  file_preview_scroll = 0;
  file_filter[0] = 0;
  usb_selected = -1;
  files_scroll = 0;
  usb_preview_text[0] = 0;
  usb_entries_count = fat_list(usb_directories[usb_depth], usb_entries, 96);
  if (usb_entries_count < 0) {
    usb_entries_count = 0;
    files_message(fat_status());
  } else {
    files_message(fat_status());
    for (int i = 0; i < usb_entries_count; i++) {
      serial("USB FILE: ");
      serial(usb_entries[i].name);
      serial("\n");
    }
  }
}
static void usb_choose(unsigned drive) {
  file_preview_scroll = 0;
  files_usb = 1;
  usb_drive = drive;
  usb_depth = 0;
  file_filter[0] = 0;
  usb_selected = -1;
  usb_entries_count = 0;
  usb_preview_text[0] = 0;
  strcopy(usb_path[0], "USB", 32);
  if (!fat_mount(drive)) {
    files_message(fat_status());
    return;
  }
  usb_directories[0] = fat_root();
  strcopy(usb_path[0], "USB", 32);
  usb_directory();
}
static int usb_read_selected(unsigned limit, u32 *size) {
  if (usb_selected < 0 || usb_selected >= usb_entries_count)
    return 0;
  if (!fat_read(&usb_entries[usb_selected], usb_file_data, limit, size)) {
    files_message(fat_status());
    return 0;
  }
  usb_file_data[*size] = 0;
  return 1;
}
static int file_text(const u8 *p, unsigned size) {
  for (unsigned i = 0; i < size; i++)
    if ((p[i] < 32 && p[i] != '\n' && p[i] != '\r' && p[i] != '\t') ||
        (p[i] >= 127 && p[i] < 160))
      return 0;
  return 1;
}
static void usb_preview(void) {
  usb_preview_text[0] = 0;
  if (usb_selected < 0)
    return;
  const struct FatEntry *e = &usb_entries[usb_selected];
  if (e->directory) {
    strcopy(usb_preview_text,
            "Folder / double-click to open.\nBack returns to the previous "
            "directory.",
            sizeof(usb_preview_text));
    return;
  }
  u32 size;
  if (!fat_read_prefix(e, usb_file_data, 8191, &size)) {
    strcopy(usb_preview_text, fat_status(), sizeof(usb_preview_text));
    return;
  }
  usb_file_data[size] = 0;
  if (file_text(usb_file_data, size)) {
    strcopy(usb_preview_text, (char *)usb_file_data, sizeof(usb_preview_text));
    if (e->size > size)
      files_message("Preview limited to 8191 bytes; original unchanged.");
  } else {
    strcopy(usb_preview_text, "BINARY FILE / HEX\n",
            sizeof(usb_preview_text));
    unsigned used = strlen(usb_preview_text), limit = size < 512 ? size : 512;
    const char *hex = "0123456789ABCDEF";
    for (unsigned i = 0; i < limit; i++) {
      usb_preview_text[used++] = hex[usb_file_data[i] >> 4];
      usb_preview_text[used++] = hex[usb_file_data[i] & 15];
      usb_preview_text[used++] = i % 16 == 15 ? '\n' : ' ';
    }
    usb_preview_text[used] = 0;
  }
}
static void usb_action(unsigned action) {
  if (action == 3) {
    if (usb_depth) {
      usb_depth--;
      usb_directory();
    }
    return;
  }
  if (usb_selected < 0 || usb_selected >= usb_entries_count)
    return;
  const struct FatEntry *entry = &usb_entries[usb_selected];
  if (entry->directory) {
    if (action != 0) {
      files_message("Open the folder to select a file.");
      return;
    }
    if (usb_depth == 15) {
      files_message("Maximum folder depth: 15.");
      return;
    }
    u32 cluster = entry->cluster;
    char name[32];
    strcopy(name, entry->name, sizeof(name));
    usb_depth++;
    usb_directories[usb_depth] = cluster;
    strcopy(usb_path[usb_depth], name, 32);
    usb_directory();
    return;
  }
  if (!action) {
    usb_preview();
    return;
  }
  u32 size;
  if (!usb_read_selected(action == 2 ? 65552 : 8191, &size))
    return;
  char name[32];
  strcopy(name, entry->name, sizeof(name));
  if (action == 2) {
    for (unsigned i = 0; name[i]; i++)
      if (name[i] == '.') {
        name[i] = 0;
        break;
      }
    if (program_exists(name)) {
      files_message("App already exists; uninstall it before replacing it.");
      return;
    }
    if (program_install_blob(name, usb_file_data, size, append)) {
      if (disk_mounted())
        desktop_save();
      files_message("ASM app installed; available in Start and by name.");
    }
  } else {
    if (!file_text(usb_file_data, size)) {
      files_message("Local storage accepts text; use Install for BEX.");
      return;
    }
    if (file_find(name) >= 0) {
      files_message("Local name already exists; rename it before copying.");
      return;
    }
    if (editor_modified && !strcmp(editor_name, name)) {
      files_message(
          "Save or discard editor changes before importing that name.");
      return;
    }
    if (!file_put(name, (char *)usb_file_data)) {
      files_message("Local storage full or invalid name.");
      return;
    }
    if (disk_mounted())
      desktop_save();
    files_message("Copied to local files. USB original unchanged.");
  }
}
static void file_copy(void) {
  if (files_usb) {
    usb_action(1);
    return;
  }
  if (selected_file >= 0 && files[selected_file].name[0]) {
    strcopy(file_clipboard, files[selected_file].data, sizeof(file_clipboard));
    strcopy(clipboard_name, files[selected_file].name, sizeof(clipboard_name));
    files_message("File copied to clipboard. Ctrl-V to duplicate it.");
  }
}
static void file_paste(void) {
  if (files_usb) {
    files_message("USB is read-only. Paste in Local.");
    return;
  }
  if (!*clipboard_name) {
    files_message("Clipboard empty.");
    return;
  }
  char name[32];
  for (unsigned i = 1; i <= 99; i++) {
    strcopy(name, "copy00_", sizeof(name));
    name[4] = '0' + i / 10;
    name[5] = '0' + i % 10;
    strcopy(name + 7, clipboard_name, sizeof(name) - 7);
    if (file_find(name) < 0 &&
        !(editor_modified && !strcmp(editor_name, name))) {
      if (!file_put(name, file_clipboard))
        files_message("Local storage full.");
      else {
        selected_file = file_find(name);
        if (disk_mounted())
          desktop_save();
        files_message("Copy created.");
      }
      return;
    }
  }
  files_message("No available name for the copy.");
}
static void editor_reset_history(void) {
  undo_head = undo_count = redo_count = 0;
  editor_goal_column = ~0u;
  editor_column_offset = 0;
  editor_search = 0;
}
static void editor_remember(struct EditSnapshot *state) {
  strcopy(state->text, editor, sizeof(editor));
  state->cursor = editor_cursor;
}
static void editor_push_undo(void) {
  editor_remember(&editor_undo[undo_head]);
  undo_head = (undo_head + 1) % 16;
  if (undo_count < 16)
    undo_count++;
}
static void editor_checkpoint(void) {
  editor_push_undo();
  redo_count = 0;
}
static void editor_refresh_modified(void) {
  editor_modified =
      strcmp(editor, editor_saved_text) != 0 || file_find(editor_name) < 0;
}
static void editor_restore_snapshot(const struct EditSnapshot *state) {
  strcopy(editor, state->text, sizeof(editor));
  editor_used = strlen(editor);
  editor_cursor = state->cursor < editor_used ? state->cursor : editor_used;
  editor_goal_column = ~0u;
  editor_refresh_modified();
}
static int editor_save(void) {
  if (!file_put(editor_name, editor)) {
    strcopy(editor_result, "Cannot save: storage full.",
            sizeof(editor_result));
    gui_progress("SAVE FAILED", editor_result, 1, 1);
    return 0;
  }
  int saved = !disk_mounted() || desktop_save();
  strcopy(editor_saved_text, editor, sizeof(editor_saved_text));
  editor_modified = 0;
  strcopy(editor_result,
          saved ? "File saved / Ctrl-B to assemble"
                : "Saved in RAM; disk save failed.",
          sizeof(editor_result));
  serial("EDITOR SAVED\n");
  return 1;
}
static void editor_load_file(int f) {
  strcopy(editor_name, files[f].name, sizeof(editor_name));
  strcopy(editor, files[f].data, sizeof(editor));
  editor_used = strlen(editor);
  editor_cursor = editor_used;
  strcopy(editor_saved_text, editor, sizeof(editor));
  editor_modified = 0;
  editor_reset_history();
  switch_page(5);
}
static void editor_request_open(int f) {
  if (!strcmp(editor_name, files[f].name) && editor_modified) {
    switch_page(5);
    return;
  }
  if (editor_modified) {
    pending_editor = f;
    dialog_mode = 4;
    dialog_error[0] = 0;
    dirty = 1;
    return;
  }
  editor_load_file(f);
}
static void editor_request_close(void) {
  if (editor_modified) {
    pending_editor = -1;
    dialog_mode = 4;
    dialog_error[0] = 0;
    dirty = 1;
  } else {
    opened[5] = 0;
    window_reveal();
  }
}
static void files_clamp_scroll(void) {
  int x, y, w, h;
  window_box(&x, &y, &w, &h);
  files_build_order();
  int count = file_order_count, rows = (h - 190) / 22;
  int limit = count > rows ? count - rows : 0;
  if (files_scroll < 0)
    files_scroll = 0;
  if (files_scroll > limit)
    files_scroll = limit;
}
static const char *editor_find_text(const char *text, const char *query) {
  for (; *text; text++) if (starts(text, query)) return text;
  return 0;
}
static const char *editor_app_name(void) {
  static char name[32];
  strcopy(name, editor_name, sizeof(name));
  for (unsigned i = 0; name[i]; i++)
    if (name[i] == '.') { name[i] = 0; break; }
  return name;
}
static int build_source(const char *source, const char *name) {
  gui_progress("ASSEMBLING X86-64", "Resolving instructions and labels", 0,
               100);
  int n = assemble(source, assembled, sizeof(assembled), editor_result,
                   sizeof(editor_result));
  if (!n) {
    append(editor_result);
    append("\n");
    gui_progress("ASSEMBLY ERROR", editor_result, 1, 1);
    return 0;
  }
  if (!program_install_blob(name, assembled, n, append))
    return 0;
  if (disk_mounted())
    desktop_save();
  append("BUILD OK: ");
  append(name);
  append("\n");
  serial("ASSEMBLER BUILD OK\n");
  return 1;
}
static void open_app(int p) {
  if (page >= 0)
    draft_overflow[page] = 0;
  used = cursor_pos = 0;
  input[0] = 0;
  switch_page(p);
}
static void launcher_launch(unsigned index) {
  launcher_build();
  if (index >= launcher_count)
    return;
  int app = launcher_order[index];
  launcher = 0;
  if (app < 0)
    switch_page(-app - 1);
  else {
    char name[32];
    strcopy(name, installed_name(app), sizeof(name));
    switch_page(1);
    program_run(name, append);
  }
}
static int editor_conflict(const char *name) {
  return editor_modified && !strcmp(editor_name, name);
}
static void editor_sync_file(int f) {
  if (f >= 0 && !editor_modified && !strcmp(files[f].name, editor_name)) {
    strcopy(editor, files[f].data, sizeof(editor));
    strcopy(editor_saved_text, editor, sizeof(editor_saved_text));
    editor_used = strlen(editor);
    if (editor_cursor > editor_used)
      editor_cursor = editor_used;
    editor_reset_history();
  }
}
static void execute(void) {
  int file_changed = 0;
  append("\n> ");
  append(input);
  append("\n");
  serial("CMD: ");
  serial(input);
  serial("\n");
  strcopy(history[history_count % 32], input, sizeof(input));
  history_count++;
  history_pos = history_count;
  char *args = word(input);
  if (!strcmp(input, "install")) {
    if (program_install(args, append) && disk_mounted())
      desktop_save();
  } else if (!strcmp(input, "uninstall")) {
    if (program_remove(args, append) && disk_mounted())
      desktop_save();
  } else if (!strcmp(input, "usb")) {
    char *name = word(args);
    if (!strcmp(args, "refresh")) {
      usb_rescan();
      usb_choose(usb_drive);
    } else if (!strcmp(args, "mount")) {
      if (name[0] >= '0' && name[0] <= '3' && !name[1])
        usb_choose(name[0] - '0');
      else
        files_message("Usage: usb mount 0..3");
    } else if (!strcmp(args, "back"))
      usb_action(3);
    else if (!*args || !strcmp(args, "ls")) {
      if (!files_usb)
        usb_choose(0);
      for (int i = 0; i < usb_entries_count; i++) {
        append(usb_entries[i].directory ? "[DIR] " : "      ");
        append(usb_entries[i].name);
        append("\n");
      }
      append(usb_status());
      append("\n");
    } else {
      if (!files_usb)
        usb_choose(0);
      int found = -1;
      for (int i = 0; i < usb_entries_count; i++)
        if (file_name_equal(usb_entries[i].name, name))
          found = i;
      if (found < 0)
        files_message("USB file not found.");
      else {
        usb_selected = found;
        if (!strcmp(args, "cd")) {
          if (usb_entries[found].directory)
            usb_action(0);
          else
            files_message("Not a folder.");
        } else if (!strcmp(args, "cat")) {
          usb_preview();
          append(usb_preview_text);
          append("\n");
        } else if (!strcmp(args, "cp"))
          usb_action(1);
        else if (!strcmp(args, "install"))
          usb_action(2);
        else
          files_message(
              "usb ls | cd NAME | back | cat NAME | cp NAME | install NAME");
      }
    }
    append(file_message);
    append("\n");
  } else if (!strcmp(input, "installer"))
    open_app(6);
  else if (!strcmp(input, "apps"))
    open_app(7);
  else if (!strcmp(input, "disks"))
    disk_list(append);
  else if (!strcmp(input, "keymap")) {
    if (*args && !input_set_layout(args))
      append("Layouts: us es de fr. Usage: keymap es\n");
    else {
      append("Keyboard layout: ");
      append(input_layout_name(input_layout()));
      append(" / Ctrl-Alt-Space changes layout\n");
      if (*args && disk_mounted())
        desktop_save();
    }
  } else if (!strcmp(input, "install-os")) {
    if (starts(args, "ERASE ") && args[6] >= '0' && args[6] <= '7' && !args[7])
      disk_install(args[6] - '0', append);
    else {
      disk_list(append);
      append("ERASES THE ENTIRE TARGET DISK.\nTo confirm: install-os ERASE "
             "N\nN is the disk number shown above.\n");
    }
  } else if (!strcmp(input, "save"))
    append(desktop_save()
               ? "State saved to disk.\n"
               : "Save failed: no installed BETA disk or I/O error.\n");
  else if (!strcmp(input, "load"))
    desktop_restore();
  else if (!strcmp(input, "gears"))
    open_app(4);
  else if (!strcmp(input, "edit")) {
    if (!*args)
      open_app(5);
    else {
      int f = file_find(args);
      if (f < 0 && file_put(args, ""))
        f = file_find(args);
      if (f < 0)
        append("Could not open the file.\n");
      else {
        used = cursor_pos = 0;
        input[0] = 0;
        editor_request_open(f);
      }
    }
  } else if (!strcmp(input, "asm")) {
    char *name = word(args);
    int f = file_find(args);
    if (f < 0)
      append("Source file not found.\n");
    else
      build_source(files[f].data, *name ? name : "scratch");
  } else if (!strcmp(input, "history")) {
    unsigned first = history_count > 32 ? history_count - 32 : 0;
    for (unsigned i = first; i < history_count; i++) {
      term_number(i + 1);
      append("  ");
      append(history[i % 32]);
      append("\n");
    }
  } else if (!strcmp(input, "netstats"))
    net_stats(append);
  else if (!strcmp(input, "perf")) {
    append("RENDER frames=");
    term_number(rendered_frames);
    append(" cursor=");
    term_number(cursor_frames);
    append(" last_us=");
    term_number(last_render_us);
    append(" max_us=");
    term_number(max_render_us);
    append(" video_bytes=");
    term_number(presented_bytes);
    append(" timer_ticks=");
    term_number(timer_ticks);
    append(" gear_frames=");
    term_number(gear_frames);
    append(" input_irqs=");
    term_number(input_irq_ticks);
    append("\n");
  } else if (!strcmp(input, "uptime")) {
    term_number((cycles() - started) / clock_hz);
    append(" seconds\n");
  } else if (!strcmp(input, "uname"))
    append("BETAOS " BETA_VERSION " x86-64 native ASM/C\n");
  else if (!strcmp(input, "pwd"))
    append("/ram\n");
  else if (!strcmp(input, "undelete"))
    file_undelete();
  else if (!strcmp(input, "touch")) {
    if (file_find(args) >= 0)
      append("File exists; contents preserved.\n");
    else if (file_put(args, "")) {
      file_changed = 1;
      append("Created.\n");
    } else
      append("Invalid filename or filesystem full.\n");
  } else if (!strcmp(input, "cp") || !strcmp(input, "mv")) {
    char *dest = word(args);
    int f = file_find(args);
    if (f < 0 || !file_name_valid(dest))
      append("Invalid source or destination.\n");
    else if (!strcmp(args, dest))
      append("Source and destination are the same; contents preserved.\n");
    else if (editor_conflict(dest))
      append("Save or discard editor changes before replacing this file.\n");
    else if (file_find(dest) >= 0)
      append("Destination exists; nothing was overwritten.\n");
    else if (!strcmp(input, "mv")) {
      if (!strcmp(editor_name, args))
        strcopy(editor_name, dest, sizeof(editor_name));
      strcopy(files[f].name, dest, sizeof(files[f].name));
      file_changed = 1;
      append("Done.\n");
    } else if (!file_put(dest, files[f].data))
      append("Copy failed: storage full.\n");
    else {
      file_changed = 1;
      append("Done.\n");
    }
  } else if (!strcmp(input, "wc")) {
    int f = file_find(args);
    if (f < 0)
      append("File not found.\n");
    else {
      term_number(strlen(files[f].data));
      append(" bytes\n");
    }
  } else if (!strcmp(input, "help"))
    append("help clear about mem net echo ls write cat rm\nprograms | run NAME "
           "| browse URL | reboot "
           "| halt\ninstall / uninstall NAME | edit [FILE] | asm FILE "
           "[APP]\napps | installer | gears | disks | install-os | save | "
           "load\nhistory uptime "
           "uname pwd touch cp mv wc undelete\nkeymap [us|es|de|fr] / "
           "Ctrl-Alt-Space\n"
           "usb ls | refresh | mount N | cd NAME | back\nusb cat NAME | cp "
           "NAME | install NAME\n"
           "Installed ASM apps: run NAME or just NAME; also Start menu.\nArrow "
           "keys: history / edit. Ctrl-L "
           "clear. Ctrl-C cancel.\n");
  else if (!strcmp(input, "programs"))
    program_list(append);
  else if (!strcmp(input, "run"))
    program_run(args, append);
  else if ((!strcmp(input, "clear") || !strcmp(input, "cls")))
    terminal[0] = 0;
  else if (!strcmp(input, "about"))
    append("BETAOS " BETA_VERSION
           " / x86-64 / ASM + C\nNative UEFI boot, GOP graphics, "
           "PS/2 input.\nNo Linux, libc, or host operating system.\n");
  else if (!strcmp(input, "mem")) {
    append("Available RAM at boot: ");
    term_number(usable_ram / 1048576);
    append(" MiB\n");
  } else if (!strcmp(input, "net")) {
    append(net_status());
    append("\n");
  } else if (!strcmp(input, "echo")) {
    append(args);
    append("\n");
  } else if ((!strcmp(input, "ls") || !strcmp(input, "dir"))) {
    for (unsigned i = 0; i < 16; i++)
      if (files[i].name[0]) {
        append(files[i].name);
        append("\n");
      }
  } else if (!strcmp(input, "write")) {
    char *data = word(args);
    int slot = -1;
    for (int i = 0; i < 16; i++)
      if (!strcmp(args, files[i].name)) {
        slot = i;
        break;
      }
    if (slot < 0)
      for (int i = 0; i < 16; i++)
        if (!files[i].name[0]) {
          slot = i;
          break;
        }
    if (!file_name_valid(args) || strlen(data) >= sizeof(files[0].data))
      append("Invalid name or file too large.\n");
    else if (editor_conflict(args))
      append("Save or discard editor changes before replacing this file.\n");
    else if (slot < 0)
      append("Filesystem full.\n");
    else {
      strcopy(files[slot].name, args, 32);
      strcopy(files[slot].data, data, sizeof(files[slot].data));
      file_changed = 1;
      editor_sync_file(slot);
      append("Saved.\n");
    }
  } else if (!strcmp(input, "cat") || !strcmp(input, "type") ||
             !strcmp(input, "rm") || !strcmp(input, "del")) {
    int found = 0;
    for (int i = 0; i < 16; i++)
      if (*args && !strcmp(files[i].name, args)) {
        found = 1;
        if (!strcmp(input, "rm") || !strcmp(input, "del")) {
          if (editor_conflict(args))
            append(
                "Save or discard editor changes before deleting this file.\n");
          else {
            file_delete(i);
            file_changed = 1;
          }
        } else {
          append(files[i].data);
          append("\n");
        }
        break;
      }
    if (!found)
      append("File not found.\n");
  } else if (!strcmp(input, "browse")) {
    char url[640];
    strcopy(url, args, sizeof(url));
    open_app(0);
    browser_load(url);
  } else if (!strcmp(input, "reboot")) {
    while (in8(0x64) & 2) {
    }
    out8(0x64, 0xfe);
  } else if (!strcmp(input, "halt")) {
    serial("HALTED\n");
    for (;;)
      __asm__ volatile("cli; hlt");
  } else if (program_exists(input)) {
    if (*args)
      append("Native apps currently accept no command arguments.\n");
    else
      program_run(input, append);
  } else if (*input)
    append("Unknown command. Type help.\n");
  if (disk_mounted() && file_changed) {
    if (!desktop_save())
      append("Disk save failed. RAM changes are not persisted.\n");
  }
}
void gui_status(const char *s) {
  strcopy(notice, s, sizeof(notice));
  dirty = 1;
}
void gui_init(void) {
  started = cycles();
  for (int i = 0; i < 8; i++) {
    windows[i].x = 48;
    windows[i].y = 88;
  }
  desktop_config.version = 1;
  const char *labels[] = {"Browser",    "Terminal",   "Files",
                          "System",      "Gears", "ASM Editor",
                          "ASM Program", "ASM Apps",   "Install OS"};
  for (int i = 0; i < 25; i++) {
    desktop_config.icons[i].x = 16 + (i / 6) * 112;
    desktop_config.icons[i].y = 20 + (i % 6) * 80;
    desktop_config.icons[i].style = i < 6    ? i
                                    : i == 6 ? 1
                                    : i == 7 ? 2
                                    : i == 8 ? 3
                                             : 6;
    if (i < 9)
      strcopy(desktop_config.icons[i].label, labels[i], 32);
  }

  editor_used = strlen(editor);
  editor_cursor = editor_used;
  strcopy(editor_saved_text, editor, sizeof(editor));
  strcopy(files[0].name, "welcome.txt", 32);
  strcopy(
      files[0].data,
      "Welcome to BETAOS, an independent 64-bit OS written in assembly and C.",
      512);
  browser_home();
}
static void switch_page(int p) {
  if (page >= 0)
    strcopy(drafts[page], input, sizeof(input));
  window_drag = window_drag_moved = window_resize = 0;
  drag_icon = -1;
  dragging = 0;
  last_icon = -1;
  page = p;
  if (p >= 0) {
    opened[p] = 1;
    windows[p].minimized = 0;
    int at = 0;
    while (at < window_count && window_order[at] != p)
      at++;
    if (at < window_count) {
      for (int j = at; j + 1 < window_count; j++)
        window_order[j] = window_order[j + 1];
      window_count--;
    }
    window_order[window_count++] = p;
  } else
    for (int j = 0; j < 8; j++)
      if (opened[j])
        windows[j].minimized = 1;
  launcher = 0;
  context_open = 0;
  if (page >= 0)
    strcopy(input, drafts[page], sizeof(input));
  else
    input[0] = 0;
  used = strlen(input);
  cursor_pos = used;
  dirty = 1;
  serial("UI APP: ");
  serial(page < 0    ? "desktop"
         : page == 0 ? "browser"
         : page == 1 ? "terminal"
         : page == 2 ? "files"
         : page == 3 ? "system"
         : page == 4 ? "gears"
         : page == 5 ? "editor"
         : page == 6 ? "installer"
                     : "apps");
  serial("\n");
}
static void file_action(unsigned action) {
  if (files_usb) {
    usb_action(action);
    return;
  }
  dialog_from_files = 1;
  if (action == 1) {
    dialog_mode = 1;
    dialog_text[0] = dialog_error[0] = 0;
  } else if (selected_file >= 0 && files[selected_file].name[0] &&
             file_matches(files[selected_file].name)) {
    if (action == 0)
      activate_desktop(9 + selected_file);
    else {
      desktop_selected = 9 + selected_file;
      dialog_mode = action == 2 ? 2 : 3;
      dialog_text[0] = dialog_error[0] = 0;
      if (action == 3)
        strcopy(dialog_text, files[selected_file].name, 32);
    }
  }
  dirty = 1;
}
static int panel_rows(int panel) {
  int x, y, w, h;
  window_box(&x, &y, &w, &h);
  int rows = (h - (panel == 6 ? 254 : 210)) / (panel == 6 ? 33 : 48);
  return rows > 0 ? rows : 1;
}
static void panel_scroll_by(int panel, int delta) {
  int count = panel == 6 ? (int)disk_total() : (int)package_total(),
      rows = panel_rows(panel), limit = count > rows ? count - rows : 0;
  int *scroll = panel == 6 ? &disk_scroll : &package_scroll;
  *scroll += delta;
  if (*scroll < 0)
    *scroll = 0;
  if (*scroll > limit)
    *scroll = limit;
}
static void window_reveal(void) {
  for (int j = window_count - 1; j >= 0; j--) {
    int app = window_order[j];
    if (opened[app] && !windows[app].minimized) {
      switch_page(app);
      return;
    }
  }
  switch_page(-1);
}
static void window_minimize(void) {
  if (page >= 0)
    windows[page].minimized = 1;
  window_reveal();
}
static void editor_vertical(int direction) {
  unsigned start = editor_cursor;
  while (start && editor[start - 1] != '\n')
    start--;
  if (editor_goal_column == ~0u)
    editor_goal_column = editor_cursor - start;
  unsigned next = start, end;
  if (direction < 0) {
    if (!start)
      return;
    end = start - 1;
    next = end;
    while (next && editor[next - 1] != '\n')
      next--;
  } else {
    while (next < editor_used && editor[next] != '\n')
      next++;
    if (next == editor_used)
      return;
    next++;
    end = next;
    while (end < editor_used && editor[end] != '\n')
      end++;
  }
  unsigned width = end - next;
  editor_cursor =
      next + (editor_goal_column < width ? editor_goal_column : width);
}
void gui_key(int c) {
  progress_active = 0;
  if (page == 0 && (c == 135 || c == 136)) {
    if (c == 135)
      browser_back();
    else
      browser_forward();
    browser_scroll = 0;
    browser_editing = 0;
    dirty = 1;
    return;
  }
  if (page == 0 && c == 137) {
    if (!browser_editing && web_key(c)) {
      dirty = 1;
      return;
    }
    web_clear_focus();
    browser_editing = 1;
    browser_address_selected = 1;
    dirty = 1;
    return;
  }
  if (c == 130) {
    input_set_layout(input_layout_name((input_layout() + 1) % 4));
    if (disk_mounted())
      desktop_save();
    gui_status(input_layout_name(input_layout()));
    serial("KEYMAP: ");
    serial(input_layout_name(input_layout()));
    serial("\n");
    return;
  }
  if (launcher && (c == 16 || c == 17)) {
    launcher_build();
    unsigned count = launcher_count;
    if (c == 17 && launcher_scroll + 7 < count)
      launcher_scroll += 7;
    if (c == 16)
      launcher_scroll = launcher_scroll >= 7 ? launcher_scroll - 7 : 0;
    dirty = 1;
    return;
  }
  if (dialog_mode) {
    if (c == 27) {
      dialog_mode = 0;
      dirty = 1;
      return;
    }
    if (dialog_mode == 3) {
      if (c == '\r' || c == '\n') {
        int f = desktop_selected - 9;
        if (f >= 0 && f < 16 && files[f].name[0]) {
          file_delete(f);
          if (disk_mounted())
            desktop_save();
        }
        dialog_mode = 0;
      }
      dirty = 1;
      return;
    }
    if (dialog_mode == 4) {
      if (c == '\n' || c == '\r' || c == 'd' || c == 'D') {
        if ((c == '\n' || c == '\r') && !editor_save()) {
          dirty = 1;
          return;
        }
        int target = pending_editor;
        pending_editor = -2;
        dialog_mode = 0;
        if (target >= 0)
          editor_load_file(target);
        else {
          if (c == 'd' || c == 'D') {
            strcopy(editor, editor_saved_text, sizeof(editor));
            editor_used = strlen(editor);
            editor_cursor = editor_used;
            editor_reset_history();
          }
          editor_modified = 0;
          opened[5] = 0;
          window_reveal();
        }
      }
      dirty = 1;
      return;
    }
    size_t n = strlen(dialog_text);
    if (c == 8 || c == 127) {
      if (n)
        dialog_text[n - 1] = 0;
    } else if (c == '\r' || c == '\n') {
      int ok = 0;
      if (!*dialog_text)
        strcopy(dialog_error, "Enter a name.", sizeof(dialog_error));
      else if (dialog_mode == 1) {
        if (file_find(dialog_text) >= 0)
          strcopy(dialog_error, "A file with that name already exists.",
                  sizeof(dialog_error));
        else if (file_put(dialog_text, "")) {
          int f = file_find(dialog_text);
          desktop_selected = 9 + f;
          if (!dialog_from_files) {
            desktop_config.icons[9 + f].x = context_x;
            desktop_config.icons[9 + f].y = context_y;
          }
          selected_file = f;
          ok = 1;
          files_clamp_scroll();
          serial("DESKTOP CREATED: ");
          serial(dialog_text);
          serial("\n");
        } else {
          strcopy(dialog_error, "No space to create more files.",
                  sizeof(dialog_error));
          serial("FILE CREATE FAILED: full\n");
        }
      } else if (desktop_selected >= 0 && desktop_selected < 9) {
        strcopy(desktop_config.icons[desktop_selected].label, dialog_text, 32);
        ok = 1;
      } else if (desktop_selected >= 9) {
        int f = desktop_selected - 9;
        if (!file_name_valid(dialog_text))
          strcopy(dialog_error, "Invalid name: no /, \\ or control characters.",
                  sizeof(dialog_error));
        else if (file_find(dialog_text) < 0 || file_find(dialog_text) == f) {
          if (!strcmp(editor_name, files[f].name))
            strcopy(editor_name, dialog_text, sizeof(editor_name));
          strcopy(files[f].name, dialog_text, 32);
          ok = 1;
        } else
          strcopy(dialog_error, "That name already exists.", sizeof(dialog_error));
      }
      if (ok) {
        dialog_mode = 0;
        if (disk_mounted())
          desktop_save();
      }
    } else if (((c >= 32 && c < 127) || (c >= 160 && c <= 255)) && n < 31) {
      dialog_text[n] = c;
      dialog_text[n + 1] = 0;
    }
    dirty = 1;
    return;
  }
  if (c == 30) {
    launcher_toggle();
    return;
  }
  if (launcher && c != 31 && c != 14) {
    launcher_build();
    if (c == 27) {
      launcher = 0;
      dirty = 1;
      return;
    }
    if (c == '\n' || c == '\r') {
      launcher_launch(launcher_selected);
      return;
    }
    if (c == 18 || c == 19 || c == 22 || c == 23) {
      if (c == 22)
        launcher_selected = 0;
      else if (c == 23)
        launcher_selected = launcher_count ? launcher_count - 1 : 0;
      else if (c == 18 && launcher_selected)
        launcher_selected--;
      else if (c == 19 && launcher_selected + 1 < launcher_count)
        launcher_selected++;
      if (launcher_selected < launcher_scroll)
        launcher_scroll = launcher_selected;
      if (launcher_selected >= launcher_scroll + 7)
        launcher_scroll = launcher_selected - 6;
      dirty = 1;
      return;
    }
    size_t n = strlen(launcher_query);
    if (c == 8) {
      if (n)
        launcher_query[n - 1] = 0;
    } else if (((c >= 32 && c < 127) || (c >= 160 && c <= 255)) && n < 31) {
      launcher_query[n] = c;
      launcher_query[n + 1] = 0;
    } else if (c >= 1 && c <= 6) {
      switch_page(c - 1);
      return;
    }
    launcher_selected = launcher_scroll = 0;
    dirty = 1;
    return;
  }
  if (c == 31) {
    for (int i = 1; i <= 8; i++) {
      int candidate = (page + i + 8) % 8;
      if (opened[candidate]) {
        switch_page(candidate);
        return;
      }
    }
    switch_page(-1);
    return;
  }
  if (c == 14 && page >= 0) {
    if (page == 0)
      browser_cancel();
    if (page == 5)
      editor_request_close();
    else {
      opened[page] = 0;
      window_reveal();
    }
    return;
  }
  if (c == 7 && (page == 2 || page < 0)) {
    if (page == 2 && files_usb) {
      files_message("USB is read-only.");
      return;
    }
    if (page < 0) {
      if (desktop_selected < 9)
        return;
      selected_file = desktop_selected - 9;
    }
    file_action(3);
    return;
  }
  if (page == 2) {
    if (c >= 1 && c <= 6) {
      switch_page(c - 1);
      return;
    }
    if (file_search) {
      size_t n = strlen(file_filter);
      if (c == 27) {
        file_filter[0] = 0;
        file_search = 0;
      } else if (c == '\n' || c == '\r')
        file_search = 0;
      else if (c == 8) {
        if (n)
          file_filter[n - 1] = 0;
      } else if (((c >= 32 && c < 127) || (c >= 160 && c <= 255)) && n < 31) {
        file_filter[n] = c;
        file_filter[n + 1] = 0;
      }
      files_scroll = 0;
      dirty = 1;
      return;
    }
    if (c == '/' || c == 134) {
      file_search = 1;
      file_filter[0] = 0;
      selected_file = usb_selected = -1;
      file_preview_scroll = 0;
      dirty = 1;
      return;
    }
    if (c == 24) {
      file_copy();
      return;
    }
    if (c == 128) {
      file_undelete();
      return;
    }
    if (c == 132 || c == 133) {
      file_preview_scroll += c == 132 ? -5 : 5;
      if (file_preview_scroll < 0)
        file_preview_scroll = 0;
      dirty = 1;
      return;
    }
    if (c == 131) {
      file_paste();
      return;
    }
    if (files_usb && c == 8) {
      usb_action(3);
      return;
    }
    if (c == 15) {
      if (files_usb) {
        files_message("Create files in Local; USB is read-only.");
        return;
      }
      file_action(1);
      return;
    }
    if (c == '\n' || c == '\r') {
      file_action(0);
      return;
    }
    if (c == 18 || c == 19 || c == 22 || c == 23) {
      files_build_order();
      int selected = files_usb ? usb_selected : selected_file, rank = -1;
      for (int j = 0; j < file_order_count; j++)
        if (file_order[j] == selected)
          rank = j;
      if (c == 22 || (rank < 0 && c != 18))
        rank = 0;
      else if (c == 23 || rank < 0)
        rank = file_order_count - 1;
      else
        rank += c == 18 ? -1 : 1;
      if (rank < 0)
        rank = 0;
      if (rank >= file_order_count)
        rank = file_order_count - 1;
      if (rank >= 0) {
        int i = file_order[rank];
        file_preview_scroll = 0;
        if (files_usb) {
          usb_selected = i;
          usb_preview();
        } else
          selected_file = i;
        int x, y, w, h;
        window_box(&x, &y, &w, &h);
        int rows = (h - 190) / 22;
        if (rank < files_scroll)
          files_scroll = rank;
        if (rank >= files_scroll + rows)
          files_scroll = rank - rows + 1;
      }
      dirty = 1;
      return;
    }
    if (c == 16 || c == 17) {
      files_scroll += c == 16 ? -5 : 5;
      files_clamp_scroll();
      dirty = 1;
      return;
    }
  }
  if ((page == 6 || page == 7) && (c == 16 || c == 17)) {
    panel_scroll_by(page, c == 16 ? -3 : 3);
    dirty = 1;
    return;
  }

  if (page == 4 && (c == ' ' || c == '+' || c == '-' || c == '=')) {
    if (c == ' ')
      gears_paused = !gears_paused;
    else if (c == '-' && gear_speed > 1)
      gear_speed--;
    else if (c != '-' && gear_speed < 8)
      gear_speed++;
    dirty = 1;
    return;
  }
  if (page == 5 && (c == 134 || editor_search)) {
    unsigned length = strlen(editor_query);
    if (c == 134) {
      editor_search = 1;
      editor_query[0] = 0;
    } else if (c == 27) editor_search = 0;
    else if (c == '\r' || c == '\n') {
      if (length) {
        const char *match = editor_find_text(editor + (editor_cursor < editor_used ? editor_cursor + 1 : 0), editor_query);
        if (!match) match = editor_find_text(editor, editor_query);
        if (match) {
          editor_cursor = match - editor;
          editor_goal_column = ~0u;
          strcopy(editor_result, "Match found. Enter: next; Esc: edit.", sizeof(editor_result));
        } else strcopy(editor_result, "Text not found.", sizeof(editor_result));
      }
    } else if (c == 8 && length) editor_query[length - 1] = 0;
    else if (((c >= 32 && c < 127) || (c >= 160 && c <= 255)) && length + 1 < sizeof(editor_query)) {
      editor_query[length] = c;
      editor_query[length + 1] = 0;
    }
    dirty = 1;
    return;
  }
  if (page == 5 && c != 27 && !(c >= 1 && c <= 6)) {
    if (c == 128) {
      if (undo_count) {
        if (redo_count < 16)
          editor_remember(&editor_redo[redo_count++]);
        undo_head = (undo_head + 15) % 16;
        undo_count--;
        editor_restore_snapshot(&editor_undo[undo_head]);
      }
      dirty = 1;
      return;
    }
    if (c == 129) {
      if (redo_count) {
        editor_push_undo();
        editor_restore_snapshot(&editor_redo[--redo_count]);
      }
      dirty = 1;
      return;
    }

    if (c != 18 && c != 19 && c != 16 && c != 17)
      editor_goal_column = ~0u;
    if (c == 18 || c == 19) {
      editor_vertical(c == 18 ? -1 : 1);
      dirty = 1;
      return;
    }
    if (c == 16 || c == 17) {
      int x, y, w, h;
      window_box(&x, &y, &w, &h);
      for (int i = 0; i < (h - 115) / 18 - 1; i++)
        editor_vertical(c == 16 ? -1 : 1);
      dirty = 1;
      return;
    }
    if (c == 7) {
      if (editor_cursor < editor_used) {
        editor_checkpoint();
        for (unsigned i = editor_cursor; i < editor_used; i++)
          editor[i] = editor[i + 1];
        editor_used--;
        editor_refresh_modified();
      }
      dirty = 1;
      return;
    }

    if (c == 26) {
      editor_save();
    } else if (c == 28)
      build_source(editor, editor_app_name());
    else if (c == 29) {
      if (build_source(editor, editor_app_name())) {
        switch_page(1);
        program_run(editor_app_name(), append);
      }
    } else if (c == 20) {
      if (editor_cursor)
        editor_cursor--;
    } else if (c == 21) {
      if (editor_cursor < editor_used)
        editor_cursor++;
    } else if (c == 22) {
      while (editor_cursor && editor[editor_cursor - 1] != '\n')
        editor_cursor--;
    } else if (c == 23) {
      while (editor_cursor < editor_used && editor[editor_cursor] != '\n')
        editor_cursor++;
    } else if (c == 11)
      editor_cursor = 0;
    else if (c == 12)
      editor_cursor = editor_used;
    else if (c == 8 || c == 127) {
      if (editor_cursor) {
        editor_checkpoint();
        for (unsigned i = editor_cursor - 1; i < editor_used; i++)
          editor[i] = editor[i + 1];
        editor_cursor--;
        editor_used--;
        editor_refresh_modified();
      }
    } else if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c < 127) ||
               (c >= 160 && c <= 255)) {
      unsigned line_start = editor_cursor;
      while (line_start && editor[line_start - 1] != '\n')
        line_start--;
      unsigned indent = 0;
      if (c == '\n' || c == '\r')
        while (line_start + indent < editor_cursor &&
               editor[line_start + indent] == ' ') indent++;
      unsigned count = c == '\t' ? 4 - ((editor_cursor - line_start) % 4) : 1 + indent;
      if (editor_used + count < sizeof(editor)) {
        editor_checkpoint();
        for (unsigned n = 0; n < count; n++) {
          for (unsigned i = editor_used + 1; i > editor_cursor; i--)
            editor[i] = editor[i - 1];
          editor[editor_cursor++] = n && indent ? ' ' :
              c == '\r' ? '\n' : c == '\t' ? ' ' : c;
          editor_used++;
        }
        editor_refresh_modified();
      } else
        strcopy(editor_result,
                "8191-byte limit: additional text was not inserted.",
                sizeof(editor_result));
    }
    dirty = 1;
    return;
  }

  if (c >= 1 && c <= 6) {
    switch_page(c - 1);
    return;
  }
  if (c == 27) {
    if (page == 0 && browser_busy()) {
      web_cancel_pending();
      browser_cancel();
      dirty = 1;
      return;
    }
    if (context_open)
      context_open = 0;
    else if (launcher)
      launcher = 0;
    else
      switch_page(-1);
    dirty = 1;
    return;
  }
  if (c == 16 || c == 17) {
    if (page == 1) {
      terminal_scroll += c == 16 ? 5 : -5;
      if (terminal_scroll < 0)
        terminal_scroll = 0;
      if (terminal_scroll > 900)
        terminal_scroll = 900;
      dirty = 1;
      return;
    }
    if (page == 0) {
      int columns = ((int)fb_width - 140) / 12, lines = 1, column = 0;
      for (const char *p = web_text; *p; p++) {
        if (*p == '\n') {
          lines++;
          column = 0;
        } else if (++column > columns) {
          lines++;
          column = 1;
        }
      }
      int visible = ((int)fb_height - 315) / 18;
      int wx, wy, ww, wh;
      window_box(&wx, &wy, &ww, &wh);
      int maximum = web_active()      ? web_scroll_limit(wh - 118)
                    : lines > visible ? lines - visible
                                      : 0;
      browser_scroll += c == 17 ? 5 : -5;
      if (browser_scroll < 0)
        browser_scroll = 0;
      if (browser_scroll > maximum)
        browser_scroll = maximum;
      serial("UI SCROLL: ");
      serial_num(browser_scroll);
      serial("\n");
      dirty = 1;
    }
    return;
  }
  if (c == '\t' && page == 1) {
    const char *commands[] = {
        "help",    "clear",     "about",     "mem",   "net",      "echo",
        "ls",      "write",     "cat",       "rm",    "programs", "run",
        "install", "uninstall", "installer", "apps",  "disks",    "install-os",
        "save",    "load",      "gears",     "edit",  "asm",      "history",
        "uptime",  "uname",     "pwd",       "touch", "cp",       "mv",
        "wc",      "browse",    "reboot",    "halt",  "usb",      "keymap",
        "undelete"};
    unsigned start = used;
    while (start && input[start - 1] != ' ')
      start--;
    const char *match = 0;
    unsigned matches = 0;
    if (!start) {
      for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
        if (starts(commands[i], input)) {
          match = commands[i];
          matches++;
        }
      for (unsigned i = 0; i < installed_total(); i++)
        if (starts(installed_name(i), input)) {
          match = installed_name(i);
          matches++;
        }
    } else {
      if (starts(input, "run ") || starts(input, "uninstall "))
        for (unsigned i = 0; i < installed_total(); i++)
          if (starts(installed_name(i), input + start)) {
            match = installed_name(i);
            matches++;
          }
      for (unsigned i = 0; i < 16; i++)
        if (files[i].name[0] && starts(files[i].name, input + start)) {
          match = files[i].name;
          matches++;
        }
    }
    if (matches == 1 && start + strlen(match) + 1 < sizeof(input)) {
      strcopy(input + start, match, sizeof(input) - start);
      used = strlen(input);
      input[used++] = ' ';
      input[used] = 0;
      cursor_pos = used;
    }
    dirty = 1;
    return;
  }
  if (page == 0 && c == '\t') {
    browser_editing = 0;
    web_key(c);
    int x, y, w, h;
    window_box(&x, &y, &w, &h);
    browser_scroll = web_focus_scroll(browser_scroll, h - 118);
    dirty = 1;
    return;
  }
  if (c == '\t') {
    switch_page((page + 1) % 6);
    return;
  }
  if (page < 0)
    return;
  if (page == 1) {
    if (c == 18 || c == 19) {
      if (history_pos == history_count)
        strcopy(history_draft, input, sizeof(input));
      unsigned first = history_count > 32 ? history_count - 32 : 0;
      if (c == 18 && history_pos > first)
        history_pos--;
      if (c == 19 && history_pos < history_count)
        history_pos++;
      strcopy(input,
              history_pos == history_count ? history_draft
                                           : history[history_pos % 32],
              sizeof(input));
      used = strlen(input);
      cursor_pos = used;
      dirty = 1;
      return;
    }
    if (c == 20) {
      if (cursor_pos)
        cursor_pos--;
      dirty = 1;
      return;
    }
    if (c == 21) {
      if (cursor_pos < used)
        cursor_pos++;
      dirty = 1;
      return;
    }
    if (c == 22) {
      cursor_pos = 0;
      dirty = 1;
      return;
    }
    if (c == 23) {
      cursor_pos = used;
      dirty = 1;
      return;
    }
    if (c == 25) {
      terminal[0] = 0;
      dirty = 1;
      return;
    }
    if (c == 24) {
      used = cursor_pos = 0;
      draft_overflow[page] = 0;
      input[0] = 0;
      append("^C\n");
      dirty = 1;
      return;
    }
  }
  if (page == 0 && c == 25) {
    web_clear_focus();
    strcopy(input, browser_address(), sizeof(input));
    used = cursor_pos = strlen(input);
    draft_overflow[0] = strlen(browser_address()) >= sizeof(input);
    browser_address_selected = browser_editing = 1;
    dirty = 1;
    return;
  }
  if (page == 0 && c == 29) {
    browser_load(browser_address());
    dirty = 1;
    return;
  }
  if (page == 0 && !browser_editing && web_key(c)) {
    int x, y, w, h;
    window_box(&x, &y, &w, &h);
    browser_scroll = web_focus_scroll(browser_scroll, h - 118);
    dirty = 1;
    return;
  }
  if (page == 0 && (c == 20 || c == 21 || c == 22 || c == 23 || c == 7)) {
    browser_editing = 1;
    if (browser_address_selected) {
      cursor_pos = c == 20 || c == 22 ? 0 : used;
      browser_address_selected = 0;
    } else if (c == 20 && cursor_pos)
      cursor_pos--;
    else if (c == 21 && cursor_pos < used)
      cursor_pos++;
    else if (c == 22)
      cursor_pos = 0;
    else if (c == 23)
      cursor_pos = used;
    else if (c == 7 && cursor_pos < used) {
      memmove(input + cursor_pos, input + cursor_pos + 1, used - cursor_pos);
      used--;
    }
    dirty = 1;
    return;
  }
  if (page == 0 && ((c >= 32 && c < 127) || (c >= 160 && c <= 255)))
    browser_editing = 1;
  if (page == 0 && browser_address_selected &&
      (c == 8 || (c >= 32 && c < 127) || (c >= 160 && c <= 255))) {
    used = cursor_pos = 0;
    input[0] = 0;
    draft_overflow[0] = 0;
    browser_address_selected = 0;
  }
  if (c == '\r' || c == '\n') {
    if (draft_overflow[page]) {
      if (page == 1)
        append("Input too long: command was not executed.\n");
      else
        gui_status("Input too long: request was not sent.");
      draft_overflow[page] = 2;
      used = cursor_pos = 0;
      input[0] = 0;
      dirty = 1;
      return;
    }
    input[used] = 0;
    if (page == 0) {
      serial("BROWSE: ");
      serial(input);
      serial("\n");
      browser_scroll = 0;
      browser_load(input);
      browser_editing = browser_address_selected = 0;
    } else if (page == 1)
      execute();
    used = 0;
    input[0] = 0;
    cursor_pos = 0;
  } else if (c == 8 || c == 127) {
    if (cursor_pos) {
      for (unsigned i = cursor_pos - 1; i < used; i++)
        input[i] = input[i + 1];
      used--;
      cursor_pos--;
      draft_overflow[page] = 0;
    }
  } else if (((c >= 32 && c < 127) || (c >= 160 && c <= 255)) &&
             used < sizeof(input) - 1) {
    if (draft_overflow[page] == 2)
      draft_overflow[page] = 0;
    for (unsigned i = used + 1; i > cursor_pos; i--)
      input[i] = input[i - 1];
    input[cursor_pos++] = c;
    used++;
  } else if ((c >= 32 && c < 127) || (c >= 160 && c <= 255)) {
    draft_overflow[page] = 1;
  }
  dirty = 1;
}
static int desktop_visible(int i) { return i < 9 || files[i - 9].name[0]; }
static int desktop_hit(int x, int y) {
  for (int i = 24; i >= 0; i--)
    if (desktop_visible(i)) {
      struct DesktopIcon *v = &desktop_config.icons[i];
      if (x >= v->x && x < v->x + 100 && y >= v->y && y < v->y + 74)
        return i;
    }
  return -1;
}
static void activate_desktop(int i) {
  if (i < 6)
    switch_page(i);
  else if (i == 6) {
    switch_page(1);
    program_run("hello", append);
  } else if (i == 7)
    switch_page(7);
  else if (i == 8)
    switch_page(6);
  else {
    editor_request_open(i - 9);
  }
}
void gui_mouse(int dx, int dy, int buttons) {
  int before_hover = hover_zone(), before_page = page, before_drag = drag_icon;
  mx += dx;
  my -= dy;
  if (mx < 0)
    mx = 0;
  if (my < 0)
    my = 0;
  if (mx > (int)fb_width - 12)
    mx = fb_width - 12;
  if (my > (int)fb_height - 16)
    my = fb_height - 16;
  if (window_drag && (buttons & 1) && page >= 0) {
    if (mx - window_press_x > 4 || mx - window_press_x < -4 ||
        my - window_press_y > 4 || my - window_press_y < -4)
      window_drag_moved = 1;
    if (window_drag_moved) {
      struct WindowState *v = current_window();
      if (v->maximized) {
        v->maximized = 0;
        window_grab_x = (fb_width - 96) / 2;
        window_grab_y = 20;
      }
      v->x = mx - window_grab_x;
      v->y = my - window_grab_y;
      if (v->x < 0)
        v->x = 0;
      int bw, bh, bx, by;
      window_box(&bx, &by, &bw, &bh);
      if (v->x > (int)fb_width - bw)
        v->x = fb_width - bw;
      if (v->y < 0)
        v->y = 0;
      if (v->y > (int)fb_height - 52 - bh)
        v->y = fb_height - 52 - bh;
      dirty = 1;
    }
  }
  if (window_resize && (buttons & 1) && page >= 0) {
    struct WindowState *v = current_window();
    v->w = resize_w + mx - resize_x;
    v->h = resize_h + my - resize_y;
    int minimum_width = page == 0 ? 400 : 640,
        minimum_height = page == 0 ? 300 : 360;
    if (v->w < minimum_width)
      v->w = minimum_width;
    if (v->h < minimum_height)
      v->h = minimum_height;
    if (v->w > (int)fb_width - v->x)
      v->w = fb_width - v->x;
    if (v->h > (int)fb_height - 52 - v->y)
      v->h = fb_height - 52 - v->y;
    dirty = 1;
  }
  int pressed = (buttons & 1) && !(last_buttons & 1),
      released = !(buttons & 1) && (last_buttons & 1);
  if (page < 0 && drag_icon >= 0 && (buttons & 1)) {
    int ddx = mx - drag_start_x, ddy = my - drag_start_y;
    if (ddx > 4 || ddx < -4 || ddy > 4 || ddy < -4)
      dragging = 1;
    if (dragging) {
      struct DesktopIcon *v = &desktop_config.icons[drag_icon];
      v->x = mx - drag_dx;
      v->y = my - drag_dy;
      if (v->x < 0)
        v->x = 0;
      if (v->y < 0)
        v->y = 0;
      if (v->x > (int)fb_width - 100)
        v->x = fb_width - 100;
      if (v->y > (int)fb_height - 126)
        v->y = fb_height - 126;
    }
  }
  if (released && window_resize) {
    window_resize = 0;
    serial("WINDOW RESIZED\n");
    dirty = 1;
  }
  if (released && window_drag) {
    if (window_drag_moved) {
      serial("WINDOW MOVED: ");
      serial_num(current_window()->x);
      serial(" ");
      serial_num(current_window()->y);
      serial("\n");
      last_title_page = -1;
    } else if (last_title_page == page &&
               cycles() - last_title_click < clock_hz / 2) {
      current_window()->maximized = !current_window()->maximized;
      last_title_page = -1;
      dirty = 1;
    } else {
      last_title_page = page;
      last_title_click = cycles();
    }
    window_drag = window_drag_moved = 0;
  }
  if (released && drag_icon >= 0) {
    int i = drag_icon;
    drag_icon = -1;
    if (dragging) {
      serial("DESKTOP MOVED\n");
      if (disk_mounted())
        desktop_save();
      last_icon = -1;
    } else if (last_icon == i && cycles() - last_icon_click < clock_hz / 2) {
      last_icon = -1;
      activate_desktop(i);
    } else {
      last_icon = i;
      last_icon_click = cycles();
    }
    dragging = 0;
  }
  if ((buttons & 2) && !(last_buttons & 2) && page < 0 && !dialog_mode) {
    desktop_selected = desktop_hit(mx, my);
    context_open = 1;
    launcher = 0;
    context_x = mx;
    context_y = my;
    if (context_x > (int)fb_width - 230)
      context_x = fb_width - 230;
    if (context_y > (int)fb_height - 235)
      context_y = fb_height - 235;
  }
  if (pressed) {
    if (progress_active) {
      progress_active = 0;
      dirty = 1;
      last_buttons = buttons;
      return;
    }
    if (dialog_mode) {
      last_buttons = buttons;
      return;
    }
    if (context_open) {
      if (mx >= context_x && mx < context_x + 230 && my >= context_y &&
          my < context_y + 175) {
        int item = (my - context_y - 6) / 32;
        if (item == 0) {
          dialog_from_files = 0;
          dialog_mode = 1;
          dialog_text[0] = dialog_error[0] = 0;
        }
        if (item == 1 && desktop_selected >= 0) {
          dialog_mode = 2;
          dialog_text[0] = dialog_error[0] = 0;
        }
        if (item == 2 && desktop_selected >= 0) {
          struct DesktopIcon *v = &desktop_config.icons[desktop_selected];
          v->style = (v->style + 1) % 7;
          serial("DESKTOP ICON CHANGED\n");
          if (disk_mounted())
            desktop_save();
        }
        if (item == 3) {
          for (int i = 0; i < 25; i++) {
            desktop_config.icons[i].x = 16 + (i / 6) * 112;
            desktop_config.icons[i].y = 20 + (i % 6) * 80;
          }
          if (disk_mounted())
            desktop_save();
        }
      }
      context_open = 0;
      last_buttons = buttons;
      dirty = 1;
      return;
    }
    if (!launcher && my < (int)fb_height - 52) {
      int saved = page, hit = -1;
      for (int j = window_count - 1; j >= 0; j--) {
        int app = window_order[j];
        if (!opened[app] || windows[app].minimized)
          continue;
        page = app;
        int x, y, w, h;
        window_box(&x, &y, &w, &h);
        if (mx >= x && mx < x + w && my >= y && my < y + h) {
          hit = app;
          break;
        }
      }
      page = saved;
      if (hit < 0 && saved >= 0) {
        strcopy(drafts[saved], input, sizeof(input));
        input[0] = 0;
        used = cursor_pos = 0;
        page = -1;
        dirty = 1;
      }
      if (hit >= 0 && hit != saved) {
        switch_page(hit);
        last_buttons = buttons;
        return;
      }
    }
    int menu_y = fb_height - 52 - 388;
    if (my >= (int)fb_height - 52) {
      if (mx < 100)
        launcher_toggle();
      else if (mx < 484) {
        int app = (mx - 100) / 64;
        if (page == app)
          window_minimize();
        else
          switch_page(app);
      } else if (mx > (int)fb_width - 22)
        switch_page(-1);
      else if (mx >= (int)fb_width - 264 && mx < (int)fb_width - 215)
        gui_key(130);
    } else if (launcher) {
      if (mx >= 356 && mx < 696 && my >= menu_y && my < (int)fb_height - 52) {
        launcher_build();
        unsigned count = launcher_count;
        if (my >= menu_y + 52 && my < menu_y + 332) {
          unsigned index = launcher_scroll + (my - menu_y - 52) / 40;
          if (index < count)
            launcher_launch(index);
        } else if (my >= menu_y + 338) {
          if (mx < 520)
            launcher_scroll = launcher_scroll >= 7 ? launcher_scroll - 7 : 0;
          else if (launcher_scroll + 7 < count)
            launcher_scroll += 7;
        }
      } else if (mx >= 8 && mx < 348 && my >= menu_y &&
                 my < (int)fb_height - 52) {
        if (my >= menu_y + 66 && my < menu_y + 330)
          switch_page((my - menu_y - 66) / 44);
        else if (my >= menu_y + 338)
          switch_page(mx < 178 ? 7 : 6);
      } else
        launcher = 0;
    } else if (page < 0) {
      int i = desktop_hit(mx, my);
      desktop_selected = i;
      if (i >= 0) {
        drag_icon = i;
        dragging = 0;
        drag_dx = mx - desktop_config.icons[i].x;
        drag_dy = my - desktop_config.icons[i].y;
        drag_start_x = mx;
        drag_start_y = my;
      }
    } else {
      int wx, wy, ww, wh;
      window_box(&wx, &wy, &ww, &wh);
      if (!current_window()->maximized && mx >= wx + ww - 16 && mx < wx + ww &&
          my >= wy + wh - 16 && my < wy + wh) {
        window_resize = 1;
        resize_w = ww;
        resize_h = wh;
        resize_x = mx;
        resize_y = my;
      } else if (my >= wy && my < wy + 42 && mx >= wx + ww - 111 &&
                 mx < wx + ww) {
        if (mx >= wx + ww - 40) {
          if (page == 0)
            browser_cancel();
          if (page == 5)
            editor_request_close();
          else {
            opened[page] = 0;
            window_reveal();
          }
        } else if (mx >= wx + ww - 76) {
          current_window()->maximized = !current_window()->maximized;
          serial(current_window()->maximized ? "WINDOW MAXIMIZED\n"
                                             : "WINDOW RESTORED\n");
        } else
          window_minimize();
      } else if (my >= wy && my < wy + 42 && mx >= wx && mx < wx + ww - 111) {
        window_drag = 1;
        window_drag_moved = 0;
        window_grab_x = mx - wx;
        window_grab_y = my - wy;
        window_press_x = mx;
        window_press_y = my;
      } else {
        if (page == 2 && my >= wy + 48 && my < wy + 76 && mx >= wx + 160 &&
            mx < wx + 258) {
          file_sort = (file_sort + 1) % 3;
          files_scroll = 0;
          last_file = -1;
          files_message(file_sort == 1   ? "Sort: name / A-Z"
                        : file_sort == 2 ? "Sort: size / largest first"
                                         : "Sort: original");
        }
        if (page == 5 && mx >= wx + 60 && mx < wx + ww - 18 &&
            my >= wy + 60 && my < wy + wh - 60) {
          unsigned line = 0;
          for (unsigned i = 0; i < editor_cursor; i++) if (editor[i] == '\n') line++;
          unsigned visible = (wh - 115) / 18;
          unsigned first = line >= visible ? line - visible + 1 : 0;
          unsigned target = first + (my - wy - 60) / 18, at = 0;
          for (unsigned row = 0; row < target && editor[at];)
            if (editor[at++] == '\n') row++;
          unsigned column = editor_column_offset + (mx - wx - 60) / 12;
          while (column-- && editor[at] && editor[at] != '\n') at++;
          editor_cursor = at;
          editor_goal_column = ~0u;
          editor_search = 0;
        }
        if (page == 0) {
          if (my >= wy + 49 && my < wy + 82) {
            if (mx >= wx + 8 && mx < wx + 140) {
              int b = (mx - wx - 8) / 34;
              if (b == 0)
                browser_back();
              else if (b == 1)
                browser_forward();
              else if (b == 2) {
                if (browser_busy()) {
                  web_cancel_pending();
                  browser_cancel();
                } else
                  browser_load(browser_address());
              } else
                browser_home();
              browser_scroll = 0;
              browser_editing = 0;
            } else if (mx >= wx + 148 && mx < wx + ww - 78)
              gui_key(25);
            else if (mx >= wx + ww - 69 && mx < wx + ww - 11) {
              browser_load("google");
              browser_scroll = 0;
              browser_editing = 0;
            }
          } else if (my >= wy + 90 && my < wy + wh - 28 && mx >= wx &&
                     mx < wx + ww) {
            browser_editing = 0;
            browser_address_selected = 0;
            if (mx >= wx + ww - 13) {
              int max = web_scroll_limit(wh - 118);
              browser_scroll = max * (my - wy - 90) / (wh - 118);
              dirty = 1;
            } else
              web_click(mx, my);
          }
        }
        if (page == 3 && my >= wy + wh - 75 && my < wy + wh - 28 &&
            mx >= wx + 22 && mx < wx + 542) {
          switch_page(mx < wx + 276 ? 6 : 7);
          last_buttons = buttons;
          return;
        }
        if (page == 6) {
          if (mx >= wx + 22 && mx < wx + ww - 22 && my >= wy + 95 &&
              my < wy + 95 + panel_rows(6) * 33) {
            install_target = disk_scroll + (my - (wy + 95)) / 33;
            if (install_target >= (int)disk_total())
              install_target = -1;
            erase_confirmed = 0;
          }
          if (my >= wy + wh - 123 && my < wy + wh - 91 && mx >= wx + 22 &&
              mx < wx + ww - 22)
            erase_confirmed = !erase_confirmed;
          if (my >= wy + wh - 70 && my < wy + wh - 28 && mx >= wx + 22 &&
              mx < wx + 300 && install_target >= 0 && erase_confirmed) {
            erase_confirmed = 0;
            disk_install(install_target, append);
          }
        }
        if (page == 7 && my >= wy + 95 && my < wy + 95 + panel_rows(7) * 48 &&
            mx >= wx + ww - 190 && mx < wx + ww - 25) {
          unsigned n = package_scroll + (my - (wy + 95)) / 48;
          if (n >= package_total()) {
            last_buttons = buttons;
            return;
          }
          int changed = package_installed(n)
                            ? program_remove(package_name(n), append)
                            : program_install(package_name(n), append);
          if (changed && disk_mounted())
            desktop_save();
        }

        if (page == 2 && my >= wy + wh - 78 && my < wy + wh - 50) {
          last_file = -1;
          if (mx >= wx + 22 && mx < wx + 92) {
            files_usb = 0;
            file_preview_scroll = 0;
            files_scroll = 0;
            file_filter[0] = 0;
            files_message(
                disk_mounted()
                    ? "BETA disk / autosave"
                    : "Local RAM / install BETAOS to keep files");
          } else if (mx >= wx + 102 && mx < wx + 172) {
            unsigned next =
                files_usb && usb_total() ? (usb_drive + 1) % usb_total() : 0;
            usb_choose(next);
          } else if (mx >= wx + 182 && mx < wx + 260) {
            usb_rescan();
            usb_choose(usb_drive);
          } else if (mx >= wx + 294 && mx < wx + 394)
            file_copy();
          else if (mx >= wx + 404 && mx < wx + 504)
            file_paste();
          else if (mx >= wx + 514 && mx < wx + 614) {
            file_search = 1;
            file_filter[0] = 0;
            selected_file = usb_selected = -1;
          }
        }
        if (page == 2 && my >= wy + 48 && my < wy + 76 && mx >= wx + 294 &&
            mx < wx + ww - 22) {
          unsigned width = (ww - 320) / 4;
          unsigned action = (mx - (wx + 294)) / width;
          if (action < 4)
            file_action(action);
        }
        if (page == 2 && mx >= wx + 22 && mx < wx + 270 && my >= wy + 87 &&
            my < wy + 87 + ((wh - 190) / 22) * 22) {
          files_build_order();
          int row = (my - (wy + 87)) / 22 + files_scroll;
          if (row >= 0 && row < file_order_count) {
            int i = file_order[row];
            file_preview_scroll = 0;
            if (files_usb) {
              char name[128];
              strcopy(name, usb_entries[i].name, sizeof(name));
              usb_selected = i;
              usb_preview();
              if (last_file == i && cycles() - last_file_click < clock_hz / 2) {
                last_file = -1;
                usb_action(0);
              } else {
                last_file = i;
                last_file_click = cycles();
              }
              serial("UI USB FILE: ");
              serial(name);
              serial("\n");
              last_buttons = buttons;
              dirty = cursor_dirty = 1;
              return;
            }
            if (last_file == i && cycles() - last_file_click < clock_hz / 2) {
              last_file = -1;
              activate_desktop(9 + i);
            } else {
              last_file = i;
              last_file_click = cycles();
            }
            selected_file = i;
            serial("UI FILE: ");
            serial(files[i].name);
            serial("\n");
          }
        }
      }
    }
  }
  if (buttons != last_buttons || before_page != page ||
      before_hover != hover_zone() || dragging || before_drag >= 0)
    dirty = 1;
  cursor_dirty = 1;
  last_buttons = buttons;
}
static void restore_pointer(void) {
  if (painted_mx < 0)
    return;
  for (int y = 0; y < 15 && painted_my + y < (int)fb_height; y++)
    for (int x = 0; x < 9 && painted_mx + x < (int)fb_width; x++)
      framebuffer[(painted_my + y) * fb_stride + painted_mx + x] =
          gui_canvas[(painted_my + y) * fb_stride + painted_mx + x];
}
static void paint_pointer(void) {
  for (int y = 0; y < 15; y++)
    for (int x = 0; x <= y / 2; x++)
      if (mx + x < (int)fb_width && my + y < (int)fb_height)
        framebuffer[(my + y) * fb_stride + mx + x] =
            native(x == 0 || x == y / 2 ? BG : INK);
  painted_mx = mx;
  painted_my = my;
  cursor_dirty = 0;
}
static void present_region(unsigned x, unsigned top, unsigned width,
                           unsigned height) {
  restore_pointer();
  for (unsigned y = top; y < top + height; y++) {
    u32 *a = gui_canvas + y * fb_stride, *b = gui_previous + y * fb_stride;
    unsigned left = x, right = x + width;
    while (left + 1 < right && read_le64(a + left) == read_le64(b + left))
      left += 2;
    if (left < right && a[left] == b[left])
      left++;
    if (left == right)
      continue;
    while (right > left && a[right - 1] == b[right - 1])
      right--;
    unsigned n = right - left;
    memcpy(b + left, a + left, n * 4);
    void *destination = (void *)(uintptr_t)(framebuffer + y * fb_stride + left);
    const void *source = a + left;
    u64 count = n;
    __asm__ volatile("cld; rep movsl"
                     : "+D"(destination), "+S"(source), "+c"(count)::"memory");
    presented_bytes += (u64)n * 4;
  }
  paint_pointer();
}
static void present_scene(void) { present_region(0, 0, fb_width, fb_height); }
static void record_frame(u64 begin) {
  rendered_frames++;
  if (page == 4)
    gear_frames++;
  last_render_us = (cycles() - begin) * 1000000 / clock_hz;
  if (last_render_us > max_render_us)
    max_render_us = last_render_us;
  last_scene_time = begin;
}
int gui_animating(void) {
  return (page == 4 && !gears_paused && !launcher && !context_open &&
          !dialog_mode && !progress_active) ||
         window_drag || window_resize;
}
static void toolbar_icon(int which, int x, int y, u32 color) {
  if (which < 2) {
    int flip = which == 1;
    rect(x + 3, y + 9, 15, 2, color);
    for (int n = 0; n < 7; n++)
      rect(x + (flip ? 16 - n : 3 + n), y + 3 + n, 2, 2, color);
    for (int n = 0; n < 7; n++)
      rect(x + (flip ? 16 - n : 3 + n), y + 9 + n, 2, 2, color);
  } else if (which == 2 && browser_busy()) {
    for (int n = 0; n < 12; n++) {
      rect(x + 4 + n, y + 4 + n, 2, 2, color);
      rect(x + 15 - n, y + 4 + n, 2, 2, color);
    }
  } else if (which == 2) {
    for (int yy = -7; yy <= 7; yy++)
      for (int xx = -7; xx <= 7; xx++) {
        int d = xx * xx + yy * yy;
        if (d >= 30 && d <= 49 && !(xx > 1 && yy < -2))
          blend_pixel(x + 10 + xx, y + 10 + yy, color, 255);
      }
    triangle(x + 12, y + 1, x + 19, y + 4, x + 13, y + 8, color);
  } else {
    for (int n = 0; n < 9; n++) {
      rect(x + 1 + n, y + 9 - n, 2, 2, color);
      rect(x + 9 + n, y + 1 + n, 2, 2, color);
    }
    rect(x + 4, y + 10, 2, 8, color);
    rect(x + 15, y + 10, 2, 8, color);
    rect(x + 4, y + 17, 13, 2, color);
    rect(x + 9, y + 12, 3, 7, color);
  }
}
static void draw_window(void) {
  int x, y, w, h;
  window_box(&x, &y, &w, &h);
  rounded(x + 3, y + 6, w, h, 12, 0x0b1320);
  rounded(x + 1, y + 3, w, h, 11, 0x121c2b);
  gui_web_clip(x, y, w, h);
  rounded(x, y, w, h, 12, PANEL);
  rounded(x, y, w, 42, 10, draw_active ? 0x303640 : 0x272d37);
  rect(x, y + 25, w, 17, draw_active ? 0x303640 : 0x272d37);
  rect(x, y + 41, w, 1, 0x404852);
  icon(page, x + 14, y + 9, ACCENT);
  const char *titles[] = {web_title,         "Terminal",
                          "Files",        "System information",
                          "Gears",      "Code editor",
                          "Install BETAOS", "ASM applications"};
  char title[65];
  size_t title_cap = (w - 160) / 12;
  if (title_cap > sizeof(title))
    title_cap = sizeof(title);
  strcopy(title, titles[page], title_cap);
  ui_label(x + 56, y + 12, title, INK, 15, w - 180);
  rounded(x + w - 108, y + 10, 25, 23, 4, 0x33465e);
  text(x + w - 102, y + 17, "_", MUTED, 2);
  rounded(x + w - 73, y + 10, 25, 23, 4, 0x33465e);
  rect(x + w - 66, y + 16, 11, 10, MUTED);
  rect(x + w - 64, y + 18, 7, 6, 0x33465e);
  rounded(x + w - 38, y + 10, 25, 23, 5, 0x583a48);
  text(x + w - 31, y + 15, "X", 0xffb4b4, 2);
  if (page == 0) {
    if (strcmp(painted_browser_url, browser_address())) {
      strcopy(painted_browser_url, browser_address(),
              sizeof(painted_browser_url));
      browser_scroll = 0;
    }
    rect(x, y + 42, w, 48, 0x292f39);
    for (int i = 0; i < 4; i++) {
      int bx = x + 8 + i * 34;
      int enabled = i == 0   ? browser_can_back()
                    : i == 1 ? browser_can_forward()
                             : 1;
      int hover = mx >= bx && mx < bx + 30 && my >= y + 49 && my < y + 81;
      if (hover)
        rounded(bx, y + 49, 30, 32, 6, 0x3a4351);
      toolbar_icon(i, bx + 5, y + 54, enabled ? INK : 0x657084);
    }
    int ax = x + 148, aw = w - 226;
    rounded(ax, y + 49, aw, 32, 8, browser_editing ? 0x567eab : 0x454d59);
    rounded(ax + 1, y + 50, aw - 2, 30, 7, 0x181e28);
    ui_label(ax + 10, y + 57, browser_verified() ? "HTTPS" : "",
             browser_verified() ? 0x83d9ad : MUTED, 11, 42);
    int tx = ax + (browser_verified() ? 56 : 12), tw = aw - (tx - ax) - 12;
    if (browser_editing) {
      unsigned begin = 0;
      while (begin < cursor_pos) {
        char prefix[640];
        unsigned n = cursor_pos - begin;
        memcpy(prefix, input + begin, n);
        prefix[n] = 0;
        if (font_label_width(prefix, 14) <= tw - 10)
          break;
        begin++;
      }
      if (browser_address_selected)
        rounded(tx - 2, y + 55, tw, 21, 3, 0x295d91);
      ui_label(tx, y + 56,
               used ? input + begin : "Search Google or enter an address",
               used ? INK : MUTED, 14, tw);
      if (cursor_pos >= begin) {
        char prefix[640];
        unsigned n = cursor_pos - begin;
        memcpy(prefix, input + begin, n);
        prefix[n] = 0;
        int cx = tx + font_label_width(prefix, 14);
        if (cx < ax + aw - 8)
          rect(cx, y + 56, 1, 19, ACCENT);
      }
    } else
      ui_label(tx, y + 56, browser_address(), INK, 14, tw);
    rounded(x + w - 69, y + 49, 58, 32, 6, 0x3b577b);
    ui_label(x + w - 62, y + 58, "Google", INK, 12, 52);
    web_paint(x + 1, y + 90, w - 14, h - 118, browser_scroll);
    int maximum = web_scroll_limit(h - 118);
    rect(x + w - 13, y + 90, 12, h - 118, 0xe9edf2);
    if (maximum > 0) {
      int track = h - 122, thumb = track * (h - 118) / (h - 118 + maximum * 18);
      if (thumb < 24)
        thumb = 24;
      int sy = y + 92 + (track - thumb) * browser_scroll / maximum;
      rounded(x + w - 10, sy, 6, thumb, 3, 0x9da9b8);
    }
    ui_label(x + 12, y + h - 21,
             draft_overflow[0] ? "Address too long: not sent"
                               : browser_status(),
             MUTED, 12, w - 24);
  } else if (page == 1) {
    int columns = (w - 44) / 12, lines = 1, column = 0,
        visible = (h - 145) / 18;
    for (const char *p = terminal; *p; p++) {
      if (*p == '\n') {
        lines++;
        column = 0;
      } else if (++column > columns) {
        lines++;
        column = 1;
      }
    }
    int skip = lines - visible - terminal_scroll;
    if (skip < 0)
      skip = 0;
    block_view(x + 22, y + 62, w - 44, h - 145, terminal, ACCENT, skip);
  } else if (page == 2) {
    files_clamp_scroll();
    rect(x + 270, y + 56, 1, h - 145, 0x2b3b53);
    int yy = y + 87, visible_rows = (h - 190) / 22;
    text_fit(x + 22, y + 60,
             file_search || *file_filter ? file_filter
             : files_usb                 ? usb_path[usb_depth]
                                         : "LOCAL / NAME",
             ACCENT, 1, 130);
    const char *sort_names[] = {"Original", "Name", "Size"};
    rounded(x + 160, y + 48, 98, 28, 4, 0x294663);
    ui_label(x + 168, y + 54, sort_names[file_sort], INK, 13, 86);
    if (file_search)
      text(x + 22, y + 73, "SEARCH / Enter finishes / Esc clears", MUTED, 1);
    int selected = files_usb ? usb_selected : selected_file;
    for (int row = files_scroll;
         row < file_order_count && row < files_scroll + visible_rows; row++) {
      int i = file_order[row];
      const char *name = file_view_name(i);
      if (i == selected)
        rounded(x + 12, yy - 5, 246, 21, 4, 0x294958);
      if (files_usb && usb_entries[i].directory) {
        rect(x + 22, yy - 1, 10, 4, ACCENT);
        rounded(x + 22, yy + 2, 18, 10, 2, ACCENT);
      }
      text_fit(x + (files_usb && usb_entries[i].directory ? 60 : 22), yy, name,
               i == selected ? ACCENT : INK, 1,
               files_usb && usb_entries[i].directory ? 196 : 236);
      yy += 22;
    }
    const char *local_actions[] = {"Open", "New", "Name", "Delete"};
    const char *usb_actions[] = {"Open", "Copy", "Install", "Back"};
    const char **actions = files_usb ? usb_actions : local_actions;
    int action_width = (w - 320) / 4;
    for (int i = 0; i < 4; i++) {
      int bx = x + 294 + i * action_width;
      rounded(bx, y + 48, action_width - 5, 28, 4, 0x294663);
      ui_label(bx + 8, y + 54, actions[i], INK, 13, action_width - 12);
    }
    u32 bytes = 0;
    const char *preview =
        "Select a file to preview.\nDouble-click or Open to "
        "edit.\nCtrl-N creates a new file.\nCtrl-C / Ctrl-V duplicates without "
        "overwriting.\n/ opens search.";
    if (files_usb) {
      preview = usb_selected >= 0
                    ? usb_preview_text
                    : "USB / READ ONLY\nSelect a file or open a "
                      "folder.\nCopy imports text to Local.\nInstall "
                      "adds a BEX executable to Start.\nRefresh "
                      "detects newly connected USB drives.";
      if (usb_selected >= 0)
        bytes = usb_entries[usb_selected].size;
    } else if (selected_file >= 0 && files[selected_file].name[0]) {
      preview = files[selected_file].data;
      bytes = strlen(preview);
    }
    text(x + 294, y + 91, "PREVIEW", MUTED, 1);
    char metadata[32], reverse[16];
    unsigned at = 0, n = 0;
    u32 value = bytes;
    do {
      reverse[n++] = '0' + value % 10;
      value /= 10;
    } while (value);
    while (n)
      metadata[at++] = reverse[--n];
    strcopy(metadata + at, " bytes", sizeof(metadata) - at);
    text(x + 420, y + 91, metadata, MUTED, 1);
    text(x + 294, y + 104, "Ctrl-PgUp / Ctrl-PgDn: scroll preview", MUTED,
         1);
    int lines = 1, column = 0, columns = (w - 320) / 12;
    for (const char *p = preview; *p; p++) {
      if (*p == '\r')
        continue;
      if (*p == '\n') {
        lines++;
        column = 0;
      } else {
        if (column >= columns) {
          lines++;
          column = 0;
        }
        column++;
      }
    }
    int visible = (h - 210 - 14 + 17) / 18,
        max_scroll = lines > visible ? lines - visible : 0;
    if (file_preview_scroll > max_scroll)
      file_preview_scroll = max_scroll;
    block_view(x + 294, y + 117, w - 320, h - 210, preview, INK,
               file_preview_scroll);
    const char *labels[] = {"Local",  "USB >", "Refresh",
                            "Copy", "Paste", "Search"};
    const int offsets[] = {22, 102, 182, 294, 404, 514},
              widths[] = {70, 70, 78, 100, 100, 100};
    for (unsigned i = 0; i < 6; i++) {
      rounded(x + offsets[i], y + h - 78, widths[i], 28, 4, 0x294663);
      ui_label(x + offsets[i] + 8, y + h - 72, labels[i], INK, 13,
               widths[i] - 12);
    }
    ui_label(x + 22, y + h - 39, file_message, MUTED, 12, w - 44);
  } else if (page == 4) {
    draw_gear_shapes(x, y, w, h);
    text(x + 22, y + 60, "SPACE PAUSE / RESUME    + / - SPEED", MUTED, 1);
    text(x + 22, y + h - 35,
         gears_paused ? "PAUSED" : "RUNNING / NATIVE C RENDERER", ACCENT, 1);
  } else if (page == 6) {
    panel_scroll_by(6, 0);
    text(x + 22, y + 62, "SELECT A SATA / AHCI DISK", INK, 2);
    text(x + 22, y + 81, "PgUp / PgDn to browse disks", MUTED, 1);
    for (unsigned i = disk_scroll;
         i < disk_total() && i < (unsigned)(disk_scroll + panel_rows(6)); i++) {
      int yy = y + 95 + (i - disk_scroll) * 33;
      rounded(x + 22, yy, w - 44, 28, 5,
              (int)i == install_target ? 0x294958 : 0x0e192b);
      char label[70];
      char number[5] = {'0' + i, 0};
      strcopy(label, "DISK ", sizeof(label));
      strcopy(label + strlen(label), number, sizeof(label) - strlen(label));
      strcopy(label + strlen(label), " / ", sizeof(label) - strlen(label));
      strcopy(label + strlen(label), disk_name(i),
              sizeof(label) - strlen(label));
      text(x + 32, yy + 8, label, INK, 1);
      u64 size = disk_mebibytes(i);
      char digits[24], reverse[24];
      unsigned ni = 0, nj = 0;
      do {
        reverse[ni++] = '0' + size % 10;
        size /= 10;
      } while (size);
      while (ni)
        digits[nj++] = reverse[--ni];
      digits[nj] = 0;
      text(x + w - 130, yy + 8, digits, ACCENT, 1);
      text(x + w - 55, yy + 8, "MIB", MUTED, 1);
    }
    if (!disk_total())
      block(x + 22, y + 110, w - 44, 90,
            "No supported disks detected. Attach a SATA/AHCI hard disk. NVMe "
            "and USB targets are not supported.",
            MUTED);
    text(x + 22, y + h - 159,
         "WARNING: ALL DATA ON THE DISK WILL BE ERASED.", 0xff8b94, 1);
    text(x + 22, y + h - 117,
         erase_confirmed ? "[X] I CONFIRM ERASING THE SELECTED DISK"
                         : "[ ] I CONFIRM ERASING THE SELECTED DISK",
         INK, 1);
    rounded(x + 22, y + h - 70, 278, 42, 7,
            erase_confirmed && install_target >= 0 ? 0x375f60 : 0x27364a);
    text(x + 42, y + h - 55, "INSTALL BETAOS",
         erase_confirmed && install_target >= 0 ? ACCENT : MUTED, 2);
  } else if (page == 7) {
    panel_scroll_by(7, 0);
    text(x + 22, y + 62, "LOCAL ASM PACKAGES / BEX FORMAT", INK, 2);
    text(x + 22, y + 81, "PgUp / PgDn to browse packages", MUTED, 1);
    for (unsigned i = package_scroll;
         i < package_total() && i < (unsigned)(package_scroll + panel_rows(7));
         i++) {
      int yy = y + 95 + (i - package_scroll) * 48;
      rounded(x + 22, yy, w - 44, 40, 6, 0x0e192b);
      text(x + 38, yy + 13, package_name(i), INK, 2);
      rounded(x + w - 190, yy + 5, 165, 30, 5, 0x294958);
      text(x + w - 174, yy + 15, package_installed(i) ? "REMOVE" : "INSTALL",
           ACCENT, 1);
    }
    block(x + 22, y + h - 110, w - 44, 85,
          "Apps persist on an installed BETA disk. Without one, installations "
          "live in RAM. Use the ASM editor to build your own app.",
          MUTED);
  } else if (page == 5) {
    unsigned cursor_line = 0, line_start = 0;
    for (unsigned i = 0; i < editor_cursor; i++)
      if (editor[i] == '\n') {
        cursor_line++;
        line_start = i + 1;
      }
    unsigned visible = (h - 115) / 18,
             first = cursor_line >= visible ? cursor_line - visible + 1 : 0;
    unsigned column = editor_cursor - line_start, columns = (w - 78) / 12;
    if (column < editor_column_offset)
      editor_column_offset = column;
    if (column >= editor_column_offset + columns)
      editor_column_offset = column - columns + 1;
    text_fit(x + 18, y + 44, editor_search ? "Search:" : editor_name, MUTED, 1, 220);
    text_fit(x + 110, y + 44, editor_search ? editor_query : "Ctrl-F search | Ctrl-S save | Ctrl-B build | Ctrl-R run", INK, 1, w - 128);
    rect(x + 52, y + 60 + (cursor_line - first) * 18, w - 70, 18, 0x303b4b);
    const char *src = editor;
    unsigned skip = 0;
    while (*src && skip < first)
      if (*src++ == '\n')
        skip++;
    int yy = y + 60;
    unsigned line = first + 1;
    while (*src && yy < y + h - 60) {
      char buf[512];
      unsigned n = 0, absolute = 0;
      while (*src && *src != '\n') {
        if (absolute++ >= editor_column_offset && n < sizeof(buf) - 1 &&
            n < columns)
          buf[n++] = *src;
        src++;
      }
      buf[n] = 0;
      char digits[5] = {'0' + (line / 100) % 10, '0' + (line / 10) % 10,
                        '0' + line % 10, 0};
      text(x + 14, yy, digits, MUTED, 1);
      unsigned max = (w - 78) / 12;
      if (max < sizeof(buf))
        buf[max] = 0;
      const char *v = buf;
      while (*v == ' ')
        v++;
      text(x + 60, yy, buf,
           *v == ';'           ? MUTED
           : starts(v, "msg:") ? 0xffcc66
                               : ACCENT,
           2);
      if (*src == '\n')
        src++;
      yy += 18;
      line++;
    }
    if (column >= editor_column_offset &&
        column - editor_column_offset < columns)
      rect(x + 60 + (column - editor_column_offset) * 12,
           y + 60 + (cursor_line - first) * 18, 2, 15, INK);
    text(x + 18, y + h - 39,
         editor_modified ? "* Unsaved changes" : editor_name,
         editor_modified ? 0xffcc66 : MUTED, 1);
    text_fit(x + 18, y + h - 23, editor_result, MUTED, 1, w - 36);
  } else {
    block(x + 22, y + 62, w - 44, h - 112,
          "BETAOS " BETA_VERSION
          "\n\nNative x86-64 / Assembly + C.\nGraphics, PS/2 "
          "input, Ethernet, HTTP/HTTPS browser,\nSATA/AHCI disk installation "
          "and "
          "saved state.\n\nF5: gears / F6: ASM code editor\nEditor: Ctrl-S "
          "save, Ctrl-B build, Ctrl-R run.\n\nNo NVMe/Wi-Fi/audio or "
          "user isolation.",
          INK);
    rounded(x + 22, y + h - 75, 250, 47, 7, 0x294958);
    text(x + 38, y + h - 58, "DISK INSTALLER", INK, 2);
    rounded(x + 292, y + h - 75, 250, 47, 7, 0x294958);
    text(x + 308, y + h - 58, "ASM APPS", INK, 2);
  }
  if (page == 1) {
    if (page == 1 && draft_overflow[page])
      text_fit(x + 22, y + h - 77,
               "Line too long: not executed. Edit it or use Ctrl-C.",
               0xff8b94, 1, w - 44);
    rounded(x + 14, y + h - 60, w - 28, 43, 7, 0x0c1627);
    text(x + 26, y + h - 44, page == 0 ? "WEB>" : ">", ACCENT, 2);
    const char *visible = input;
    unsigned maxchars = (w - 130) / 12;
    if (used > maxchars)
      visible += used - maxchars;
    text(x + 96, y + h - 44, visible, INK, 2);
    unsigned prefix = visible - input;
    if (cursor_pos >= prefix)
      text(x + 96 + (cursor_pos - prefix) * 12, y + h - 44, "_", ACCENT, 2);
  }
  if (!current_window()->maximized) {
    for (int g = 0; g < 3; g++)
      rect(x + w - 6 - g * 3, y + h - 7 - g * 3, 2, 2, MUTED);
  }
  gui_web_unclip();
}
void gui_render(void) {
  u64 now = cycles();
  int animation_only = 0;
  int animate = page == 4 && !gears_paused && !launcher && !context_open &&
                !dialog_mode && !progress_active;
  if (animate && now - last_animation >= clock_hz / 60) {
    u64 delta = now - animation_time;
    if (!animation_time || delta > clock_hz)
      delta = clock_hz / 60;
    gear_angle = (gear_angle + delta * 36 * gear_speed * 65536 / clock_hz) %
                 (1024 * 65536);
    last_animation = animation_time = now;
    animation_only = !dirty;
    dirty = 1;
  } else if (!animate)
    animation_time = now;

  u64 second = (cycles() - started) / clock_hz;
  if (second != last_second) {
    last_second = second;
    dirty = 1;
    animation_only = 0;
  }
  if (!dirty) {
    if (cursor_dirty) {
      restore_pointer();
      paint_pointer();
      cursor_frames++;
    }
    return;
  }
  if ((window_drag || window_resize) && now - last_scene_time < clock_hz / 60) {
    if (cursor_dirty) {
      restore_pointer();
      paint_pointer();
      cursor_frames++;
    }
    return;
  }
  u64 render_begin = cycles();
  dirty = 0;
  if (animation_only) {
    int x, y, w, h;
    window_box(&x, &y, &w, &h);
    int radius = h < 500 ? 65 : 90, small = h < 500 ? 50 : 70, cx = x + w / 2,
        top = y + h / 2 + 10 - radius / 2;
    int left = cx - 2 * radius - 10, upper = top - radius - 10,
        width = 4 * radius + 22, height = 2 * radius + 2 * small + 24;
    rect(left, upper, width, height, PANEL);
    draw_gear_shapes(x, y, w, h);
    present_region(left, upper, width, height);
    animation_damage_frames++;
    record_frame(render_begin);
    return;
  }
  /* Scanline wallpaper: no external compositor or bitmap dependency. */
  for (unsigned y = 0; y < fb_height; y++) {
    unsigned t = y * 48 / fb_height;
    rect(0, y, fb_width, 1,
         ((17 + t / 5) << 16) | ((30 + t / 2) << 8) | (52 + t));
    int bend =
        (int)fb_width / 2 +
        (int)((long long)((int)y - (int)fb_height / 2) *
              ((int)y - (int)fb_height / 2) / (fb_height ? fb_height : 1));
    rect(bend, y, fb_width - bend, 1,
         ((20 + t / 4) << 16) | ((48 + t / 2) << 8) | (83 + t));
    rect(bend + fb_width / 5, y, fb_width, 1,
         ((24 + t / 3) << 16) | ((65 + t / 2) << 8) | (109 + t));
  }
  const char *names[] = {"Browser", "Terminal",   "Files",
                         "System",   "Gears", "ASM Editor"};
  {
    ui_label(fb_width - 235, fb_height - 143, "BETAOS", 0xc1d9f2, 28, 210);
    ui_label(fb_width - 233, fb_height - 105, "Your workspace", 0x94b4d5,
             14, 220);
    for (int i = 0; i < 25; i++)
      if (desktop_visible(i)) {
        struct DesktopIcon *v = &desktop_config.icons[i];
        if (desktop_selected == i)
          rounded(v->x, v->y, 100, 74, 4, 0x294663);
        icon(v->style, v->x + 36, v->y + 8,
             i == 2 || i >= 9 ? 0xffce72
             : i == 4         ? 0xff8195
             : i == 5         ? 0xb8a0ff
                              : INK);
        const char *label = i < 9 ? v->label : files[i - 9].name;
        char display[17];
        strcopy(display, label, sizeof(display));
        if (font_label_width(display, 13) > 96) {
          char *space = strchr(display, ' ');
          if (space) {
            *space = 0;
            ui_label(v->x + (100 - font_label_width(display, 13)) / 2,
                     v->y + 45, display, INK, 13, 100);
            ui_label(v->x + (100 - font_label_width(space + 1, 13)) / 2,
                     v->y + 61, space + 1, INK, 13, 100);
          } else
            ui_label(v->x + 2, v->y + 48, display, INK, 13, 96);
        } else
          ui_label(v->x + (100 - font_label_width(display, 13)) / 2, v->y + 48,
                   display, INK, 13, 100);
      }
  }
  int focused = page;
  char focused_input[640];
  strcopy(focused_input, input, sizeof(focused_input));
  unsigned focused_used = used, focused_cursor = cursor_pos;
  for (int j = 0; j < window_count; j++) {
    int app = window_order[j];
    if (app == focused || !opened[app] || windows[app].minimized)
      continue;
    page = app;
    strcopy(input, drafts[app], sizeof(input));
    used = strlen(input);
    cursor_pos = used;
    draw_active = 0;
    draw_window();
  }
  page = focused;
  strcopy(input, focused_input, sizeof(input));
  used = focused_used;
  cursor_pos = focused_cursor;
  draw_active = 1;
  if (page >= 0)
    draw_window();
  int task_y = fb_height - 52;
  rect(0, task_y, fb_width, 52, 0x1c2027);
  rect(0, task_y, fb_width, 1, 0x2d4055);
  if (launcher)
    rect(0, task_y, 100, 52, 0x294663);
  icon(-1, 14, task_y + 14, ACCENT);
  ui_label(48, task_y + 19, "Start", INK, 13, 48);
  for (int i = 0; i < 6; i++) {
    int bx = 100 + i * 64, hover = mx >= bx && mx < bx + 64 && my >= task_y;
    if (page == i || hover)
      rect(bx, task_y + 2, 64, 50, page == i ? 0x364a63 : 0x303844);
    icon(i, bx + 18, task_y + 12, page == i ? ACCENT : INK);
    if (opened[i])
      rect(bx + 8, fb_height - 3, 48, 3, page == i ? ACCENT : 0x657c96);
    if (hover) {
      int len = strlen(names[i]) * 6 + 20;
      rounded(bx, task_y - 30, len, 24, 4, 0x303844);
      ui_label(bx + 10, task_y - 25, names[i], INK, 13, len - 16);
    }
  }
  char clock[9];
  uptime_text(clock, second);
  rounded(fb_width - 211, task_y + 22, 7, 7, 3,
          net_ready() ? ACCENT : 0xefab65);
  text(fb_width - 195, task_y + 24, net_ready() ? "CONNECTED" : "OFFLINE",
       MUTED, 1);
  u32 utc_day, utc_second;
  if (platform_utc(&utc_day, &utc_second))
    uptime_text(clock, utc_second);
  ui_label(fb_width - 99, task_y + 10, clock, INK, 13, 90);
  ui_label(fb_width - 99, task_y + 30, "BETAOS " BETA_VERSION, MUTED, 11, 90);
  rounded(fb_width - 264, task_y + 12, 46, 28, 4, 0x294663);
  ui_label(fb_width - 253, task_y + 18, input_layout_name(input_layout()), INK,
           13, 27);
  rect(fb_width - 9, task_y, 1, 52, 0x556577);
  if (launcher) {
    int menu_y = task_y - 388;
    rounded(8, menu_y, 340, 388, 5, 0x262d38);
    ui_label(28, menu_y + 21, "Applications", INK, 22, 280);
    for (int i = 0; i < 6; i++) {
      int yy = menu_y + 66 + i * 44;
      if (mx >= 8 && mx < 348 && my >= yy && my < yy + 44)
        rect(12, yy, 332, 44, 0x3a4c63);
      icon(i, 28, yy + 8, ACCENT);
      ui_label(76, yy + 13, names[i], INK, 16, 250);
    }
    rounded(20, menu_y + 338, 150, 38, 4, 0x294663);
    text(35, menu_y + 352, "ASM APPS", INK, 1);
    rounded(186, menu_y + 338, 150, 38, 4, 0x294663);
    text(198, menu_y + 352, "INSTALL OS", INK, 1);
    launcher_build();
    unsigned count = launcher_count;
    rounded(356, menu_y, 340, 388, 5, 0x262d38);
    rounded(368, menu_y + 8, 316, 25, 4, 0x0e192a);
    text_fit(376, menu_y + 15,
             *launcher_query ? launcher_query : "Search apps: type here",
             ACCENT, 1, 290);
    text(376, menu_y + 38, "Arrows select / Enter opens / Esc closes", MUTED,
         1);
    for (unsigned i = launcher_scroll; i < count && i < launcher_scroll + 7;
         i++) {
      int yy = menu_y + 52 + (i - launcher_scroll) * 40;
      if (i == launcher_selected ||
          (mx >= 356 && mx < 696 && my >= yy && my < yy + 40))
        rect(360, yy, 332, 40, 0x3a4c63);
      int app = launcher_order[i];
      icon(app < 0 && -app - 1 < 6 ? -app - 1 : 1, 372, yy + 4, ACCENT);
      char label[32];
      strcopy(label,
              app < 0 ? launcher_builtins[-app - 1] : installed_name(app),
              sizeof(label));
      font_draw_label(420, yy + 10, label, INK, 14);
    }
    if (!count)
      text(376, menu_y + 70,
           *launcher_query ? "No results. Backspace to edit."
                           : "Install or build an ASM app.",
           MUTED, 1);
    text(376, menu_y + 352, "< PREVIOUS", MUTED, 1);
    text(540, menu_y + 352, "NEXT >", MUTED, 1);
  }
  if (context_open) {
    rounded(context_x + 3, context_y + 4, 230, 175, 4, 0x080f1c);
    rounded(context_x, context_y, 230, 175, 4, 0x24384c);
    const char *items[] = {"New file", "Rename", "Change icon",
                           "Arrange icons", "Refresh"};
    for (int i = 0; i < 5; i++) {
      int yy = context_y + 6 + i * 32;
      if (mx >= context_x && mx < context_x + 230 && my >= yy && my < yy + 32)
        rect(context_x + 4, yy, 222, 32, 0x35536d);
      ui_label(context_x + 15, yy + 7, items[i],
               i == 1 || i == 2 ? (desktop_selected >= 0 ? INK : MUTED) : INK,
               14, 210);
    }
  }
  if (dialog_mode) {
    int px = fb_width / 2 - 220, py = fb_height / 2 - 95;
    rounded(px + 5, py + 5, 440, 190, 7, 0x080f1c);
    rounded(px, py, 440, 190, 7, 0x24384c);
    text(px + 22, py + 22,
         dialog_mode == 4   ? "SAVE CHANGES?"
         : dialog_mode == 3 ? "DELETE FILE?"
         : dialog_mode == 1 ? "NEW FILE"
                            : "RENAME",
         INK, 2);
    rounded(px + 22, py + 64, 396, 35, 4, 0x101b29);
    text(px + 33, py + 76, dialog_text, INK, 1);
    text(px + 33 + strlen(dialog_text) * 6, py + 76, "_", ACCENT, 1);
    text(px + 22, py + 120,
         dialog_mode == 4 ? "ENTER save / D discard / ESC cancel"
                          : "ENTER: accept  /  ESC: cancel",
         MUTED, 1);
    text(px + 22, py + 152, dialog_error, 0xff8b94, 1);
  }
  if (progress_active) {
    int px = fb_width / 2 - 240, py = fb_height / 2 - 95;
    rounded(px + 5, py + 6, 480, 190, 12, 0x080d16);
    rounded(px, py, 480, 190, 12, 0x25384e);
    text(px + 24, py + 24, progress_title, INK, 2);
    char label[65];
    strcopy(label, progress_label, 65);
    text(px + 24, py + 64, label, MUTED, 1);
    rounded(px + 24, py + 96, 432, 20, 7, 0x0b1424);
    if (progress_percent)
      rounded(px + 24, py + 96, 432 * progress_percent / 100, 20, 7,
              progress_failed ? 0xff788b : ACCENT);
    char percent[5];
    unsigned n = progress_percent;
    percent[0] = '0' + n / 100;
    percent[1] = '0' + (n / 10) % 10;
    percent[2] = '0' + n % 10;
    percent[3] = '%';
    percent[4] = 0;
    text(px + 24, py + 138,
         n == 100  ? percent
         : n >= 10 ? percent + 1
                   : percent + 2,
         INK, 2);
    text(px + 125, py + 143,
         progress_failed ? "FAILED"
         : n == 100      ? "COMPLETE"
                         : "WORKING...",
         MUTED, 1);
  }
  present_scene();
  record_frame(render_begin);
}

void gui_web_rect(int x, int y, int w, int h, u32 color) {
  rect(x, y, w, h, color);
}
void gui_web_round(int x, int y, int w, int h, int radius, u32 color) {
  rounded(x, y, w, h, radius, color);
}

void gui_web_redraw(void) {
  if (page == 0)
    dirty = 1;
}

void gui_web_clip(int x, int y, int w, int h) {
  drawing_clip = 1;
  clip_left = x;
  clip_top = y;
  clip_right = x + w;
  clip_bottom = y + h;
  font_clip(x, y, w, h);
}
void gui_web_unclip(void) {
  drawing_clip = 0;
  font_unclip();
}
void gui_web_pixel(int x, int y, u32 color, unsigned alpha) {
  blend_pixel(x, y, color, alpha);
}
void gui_wheel(int delta) {
  if (page == 0) {
    int x, y, w, h;
    window_box(&x, &y, &w, &h);
    browser_scroll += delta * 3;
    int maximum = web_scroll_limit(h - 118);
    if (browser_scroll < 0)
      browser_scroll = 0;
    if (browser_scroll > maximum)
      browser_scroll = maximum;
    dirty = 1;
  } else if (page == 1) {
    terminal_scroll -= delta * 3;
    if (terminal_scroll < 0)
      terminal_scroll = 0;
    dirty = 1;
  } else if (page == 2) {
    files_scroll += delta * 3;
    files_clamp_scroll();
    dirty = 1;
  }
}
