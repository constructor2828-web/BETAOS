#include "web.h"
#include <stdlib.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 2048
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_SIMD
#include "stb_image.h"
struct Image {
  int node, w, h;
  unsigned char *pixels;
};
static struct Image images[32];
static unsigned image_bytes;
void web_image_reset(void) {
  for (unsigned i = 0; i < 32; i++) {
    free(images[i].pixels);
    images[i].pixels = 0;
    images[i].node = -1;
  }
  image_bytes = 0;
}
int web_image_load(int node, const void *data, unsigned bytes) {
  if (!data || !bytes || bytes > 1024 * 1024)
    return 0;
  int w = 0, h = 0, components;
  if (!stbi_info_from_memory(data, bytes, &w, &h, &components) || w <= 0 ||
      h <= 0 || w > 2048 || h > 2048 || (unsigned)w * (unsigned)h > 1024 * 1024)
    return 0;
  unsigned size = (unsigned)w * h * 4;
  if (image_bytes + size > 8 * 1024 * 1024)
    return 0;
  int slot = -1;
  for (unsigned i = 0; i < 32; i++)
    if (images[i].pixels && images[i].node == node) {
      image_bytes -= images[i].w * images[i].h * 4;
      free(images[i].pixels);
      images[i].pixels = 0;
      slot = i;
      break;
    }
  if (slot < 0)
    for (unsigned i = 0; i < 32; i++)
      if (!images[i].pixels) {
        slot = i;
        break;
      }
  if (slot < 0)
    return 0;
  unsigned char *pixels =
      stbi_load_from_memory(data, bytes, &w, &h, &components, 4);
  if (!pixels)
    return 0;
  images[slot] = (struct Image){node, w, h, pixels};
  image_bytes += size;
  serial("WEB IMAGE: decoded\n");
  return 1;
}
void web_image_size(int node, int *w, int *h) {
  *w = *h = 0;
  for (unsigned i = 0; i < 32; i++)
    if (images[i].pixels && images[i].node == node) {
      *w = images[i].w;
      *h = images[i].h;
      return;
    }
}
int web_image_draw(int node, int x, int y, int w, int h, int cx, int cy, int cw,
                   int ch) {
  struct Image *im = 0;
  for (unsigned i = 0; i < 32; i++)
    if (images[i].pixels && images[i].node == node) {
      im = &images[i];
      break;
    }
  if (!im || w <= 0 || h <= 0)
    return 0;
  int l = x > cx ? x : cx, t = y > cy ? y : cy,
      r = x + w < cx + cw ? x + w : cx + cw,
      b = y + h < cy + ch ? y + h : cy + ch;
  for (int dy = t; dy < b; dy++)
    for (int dx = l; dx < r; dx++) {
      unsigned at = (((dy - y) * im->h / h) * im->w + (dx - x) * im->w / w) * 4;
      const u8 *p = im->pixels + at;
      gui_web_pixel(dx, dy, ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2], p[3]);
    }
  return 1;
}
