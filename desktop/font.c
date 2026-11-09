#include "os.h"
#include <math.h>
#include <stdlib.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
extern const unsigned char desktop_font_start[], desktop_font_end[];
extern const unsigned char interface_font_start[];
static stbtt_fontinfo faces[2];
static int clip_on, clip_x, clip_y, clip_w, clip_h;
static int ready;
#define CACHE 512
struct Glyph {
  int code, size, face, x, y, w, h;
  u8 pixels[48 * 48];
  u64 used;
};
static struct Glyph glyphs[CACHE];
static u64 counter;
void font_init(void) {
  ready = stbtt_InitFont(&faces[0], desktop_font_start,
                         stbtt_GetFontOffsetForIndex(desktop_font_start, 0));
  ready = ready &&
          stbtt_InitFont(&faces[1], interface_font_start,
                         stbtt_GetFontOffsetForIndex(interface_font_start, 0));
  if (ready)
    serial("FONT: TrueType antialiasing enabled\n");
}
static struct Glyph *glyph_at(int code, int height, int face_id) {
  struct Glyph *chosen = &glyphs[0];
  for (unsigned i = 0; i < CACHE; i++) {
    if (glyphs[i].used && glyphs[i].code == code && glyphs[i].size == height &&
        glyphs[i].face == face_id) {
      glyphs[i].used = ++counter;
      return &glyphs[i];
    }
    if (glyphs[i].used < chosen->used)
      chosen = &glyphs[i];
  }
  stbtt_fontinfo *face = &faces[face_id];
  chosen->face = face_id;
  chosen->code = code;
  chosen->size = height;
  float scale = stbtt_ScaleForPixelHeight(face, height);
  int x1, y1;
  stbtt_GetCodepointBitmapBox(face, code, scale, scale, &chosen->x, &chosen->y,
                              &x1, &y1);
  chosen->w = x1 - chosen->x;
  chosen->h = y1 - chosen->y;
  if (chosen->w > 48 || chosen->h > 48 || chosen->w < 0 || chosen->h < 0) {
    chosen->w = chosen->h = 0;
  }
  if (chosen->w && chosen->h)
    stbtt_MakeCodepointBitmap(face, chosen->pixels, chosen->w, chosen->h, 48,
                              scale, scale, code);
  chosen->used = ++counter;
  return chosen;
}
static void draw_glyph(int x, int baseline, int code, int height, u32 color,
                       int face_id) {
  struct Glyph *g = glyph_at(code, height, face_id);
  u32 source = fb_format == 0
                   ? ((color & 255) << 16) | (color & 0xff00) | (color >> 16)
                   : color;
  for (int row = 0; row < g->h; row++)
    for (int col = 0; col < g->w; col++) {
      int dx = x + g->x + col, dy = baseline + g->y + row;
      if (dx < 0 || dy < 0 || dx >= (int)fb_width || dy >= (int)fb_height)
        continue;
      if (clip_on && (dx < clip_x || dy < clip_y || dx >= clip_x + clip_w ||
                      dy >= clip_y + clip_h))
        continue;
      unsigned a = g->pixels[row * 48 + col];
      if (!a)
        continue;
      u32 *p = gui_canvas + dy * fb_stride + dx, old = *p, result = 0;
      for (unsigned shift = 0; shift < 24; shift += 8)
        result |= ((((old >> shift) & 255) * (255 - a) +
                    ((source >> shift) & 255) * a + 127) /
                   255)
                  << shift;
      *p = result;
    }
}
int font_draw_ui(int x, int y, const char *text, u32 color, int scale) {
  if (!ready || scale < 1 || scale > 3)
    return 0;
  for (const u8 *s = (const u8 *)text; *s; s++) {
    draw_glyph(x, y + 7 * scale, *s, 12 * scale, color, 0);
    x += 6 * scale;
  }
  return 1;
}
void font_draw_web_mono(int x, int y, const char *text, u32 color, int size) {
  if (!ready)
    return;
  if (size < 8)
    size = 8;
  if (size > 32)
    size = 32;
  int height = size * 4 / 3;
  const u8 *s = (const u8 *)text;
  while (*s) {
    unsigned code = *s++;
    if (code >= 0xc2 && code < 0xe0 && s[0] >= 0x80 && s[0] < 0xc0) {
      code = ((code & 31) << 6) | (s[0] & 63);
      s++;
    } else if (code >= 0xe0 && code < 0xf0 && s[0] >= 0x80 && s[0] < 0xc0 &&
               s[1] >= 0x80 && s[1] < 0xc0) {
      code = ((code & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63);
      s += 2;
    }
    draw_glyph(x, y + size, code, height, color, 0);
    x += font_web_advance(size);
  }
}
int font_web_advance(int size) { return (size * 3 + 2) / 5; }

void font_clip(int x, int y, int w, int h) {
  clip_on = 1;
  clip_x = x;
  clip_y = y;
  clip_w = w;
  clip_h = h;
}
void font_unclip(void) { clip_on = 0; }
int font_label_width(const char *text, int size) {
  if (!ready)
    return (int)strlen(text) * size / 2;
  float scale = stbtt_ScaleForPixelHeight(&faces[1], size * 4 / 3);
  float width = 0;
  for (const u8 *p = (const u8 *)text; *p; p++) {
    int advance;
    stbtt_GetCodepointHMetrics(&faces[1], *p, &advance, 0);
    width += advance * scale;
  }
  return (int)(width + 0.5f);
}
void font_draw_label(int x, int y, const char *text, u32 color, int size) {
  if (!ready)
    return;
  float scale = stbtt_ScaleForPixelHeight(&faces[1], size * 4 / 3),
        pen = (float)x;
  for (const u8 *p = (const u8 *)text; *p; p++) {
    int advance;
    stbtt_GetCodepointHMetrics(&faces[1], *p, &advance, 0);
    draw_glyph((int)(pen + 0.5f), y + size, *p, size * 4 / 3, color, 1);
    pen += advance * scale;
  }
}

static unsigned next_utf8(const u8 **cursor) {
  const u8 *s = *cursor;
  unsigned code = *s++;
  if (code >= 0xc2 && code < 0xe0 && s[0] >= 0x80 && s[0] < 0xc0) {
    code = ((code & 31) << 6) | (s[0] & 63);
    s++;
  } else if (code >= 0xe0 && code < 0xf0 && s[0] >= 0x80 && s[0] < 0xc0 &&
             s[1] >= 0x80 && s[1] < 0xc0) {
    code = ((code & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63);
    s += 2;
  }
  *cursor = s;
  return code;
}
int font_web_measure(const char *text, unsigned bytes, int size) {
  if (!ready)
    return bytes * size / 2;
  float scale = stbtt_ScaleForPixelHeight(&faces[1], size * 4 / 3), width = 0;
  const u8 *p = (const u8 *)text, *end = p + bytes;
  while (p < end && *p) {
    unsigned code = next_utf8(&p);
    int advance;
    stbtt_GetCodepointHMetrics(&faces[1], code, &advance, 0);
    width += advance * scale;
  }
  return (int)(width + 0.5f);
}
void font_draw_web(int x, int y, const char *text, u32 color, int size) {
  if (!ready || size < 8 || size > 32)
    return;
  float scale = stbtt_ScaleForPixelHeight(&faces[1], size * 4 / 3), pen = x;
  const u8 *p = (const u8 *)text;
  while (*p) {
    unsigned code = next_utf8(&p);
    int advance;
    stbtt_GetCodepointHMetrics(&faces[1], code, &advance, 0);
    draw_glyph((int)(pen + 0.5f), y + size, code, size * 4 / 3, color, 1);
    pen += advance * scale;
  }
}
