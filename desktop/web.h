#ifndef BETA_WEB_H
#define BETA_WEB_H
#include "os.h"
#define WEB_NODES 2048
struct WebStyle {
  int display, size, bold, width, height, max_width, margin[4], padding[4],
      border, radius, align, gap;
  int width_percent, max_width_percent, min_width, border_box, line_height,
      flex_dir, flex_wrap, justify, align_items, grow, basis;
  u32 color, bg, border_color;
  int bg_set, pre;
};
struct WebNode {
  int parent, first, last, next, first_attr;
  char tag[16];
  char *text, *id, *classes, *href, *src, *style_text, *value, *name, *type,
      *action, *onclick, *rel;
  struct WebStyle style;
  int x, y, w, h;
};
extern struct WebNode web_nodes[WEB_NODES];
extern int web_node_count;
extern int web_dirty;
void web_home(void);
void web_document(const char *, const char *);
void web_image_reset(void);
int web_image_load(int, const void *, unsigned);
void web_image_size(int, int *, int *);
int web_image_draw(int, int, int, int, int, int, int, int, int);
void gui_web_pixel(int, int, u32, unsigned);
void gui_web_clip(int, int, int, int);
void gui_web_unclip(void);
void web_paint(int, int, int, int, int);
void web_click(int, int);
int web_key(int);
void web_poll(void);
int web_next_request(char *, unsigned, int *, int *);
void web_resource(int, int, const char *, unsigned, int);
void web_queue_navigation(const char *);
int web_create(const char *);
int web_find(const char *);
int web_query(const char *);
void web_append(int, int);
void web_set_text(int, const char *);
const char *web_get_text(int);
void web_set_attr(int, const char *, const char *);
const char *web_get_attr(int, const char *);
int web_has_attr(int, const char *);
const char *web_location(void);
int web_body(void);
int web_active(void);
int web_scroll_limit(int);
int web_focus_scroll(int, int);
void web_cancel_pending(void);
void web_clear_focus(void);
void web_js_reset(void);
void web_js_eval(const char *, const char *);
int web_js_event(int, const char *);
void web_js_fetch_result(int, const char *, int);
int web_fetch(const char *, int);
void web_set_html(int, const char *);
void web_js_loaded(void);
void web_js_tick(void);
void gui_web_redraw(void);
void gui_web_rect(int, int, int, int, u32);
void gui_web_round(int, int, int, int, int, u32);
#endif
