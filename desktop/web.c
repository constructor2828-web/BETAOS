#include "web.h"
#include <stdlib.h>
#include <string.h>
#define RULES 256
#define BOXES 8192
struct WebNode web_nodes[WEB_NODES];
int web_node_count, web_dirty;
static char strings[3 * 1024 * 1024];
static unsigned strings_used;
static char page_url[2048], navigation[2048];
struct Rule {
  char selector[128];
  char *text;
  int score;
};
static struct Rule rules[RULES];
static unsigned rule_count;
struct Box {
  int node, x, y, w, h, size;
  u32 color;
  const char *text;
  unsigned bytes;
  int control;
};
static struct Box boxes[BOXES];
static unsigned box_count;
struct Resource {
  char url[2048];
  int kind, node;
};
static struct Resource resources[96];
static unsigned resource_count, resource_next;
static char *sheets[24];
static unsigned sheet_count;
static int sheet_nodes[24];
static int script_cursor, script_waiting, script_deferred;
static unsigned focus_cursor;
static int focus_select;
static int focus = -1, paint_x, paint_y, paint_w, paint_h, view_width = 800,
           scroll_px, document_height;
static int changed, js_loaded, css_dirty;
static char empty[] = "";
struct Attribute {
  char *key, *value;
  int next;
};
static struct Attribute attributes[8192];
static unsigned attribute_count;
static char control_labels[WEB_NODES][256];
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int equal(const char *a, const char *b) {
  while (*a && *b) {
    if (lower((u8)*a++) != lower((u8)*b++))
      return 0;
  }
  return !*a && !*b;
}
static int prefix(const char *a, const char *b) {
  while (*b)
    if (lower((u8)*a++) != lower((u8)*b++))
      return 0;
  return 1;
}
static const char *find_ci(const char *p, const char *what) {
  for (; *p; p++)
    if (prefix(p, what))
      return p;
  return 0;
}
static int whitespace(int c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}
static char *save_n(const char *p, unsigned n) {
  if (n > sizeof(strings) - strings_used - 1)
    return empty;
  char *out = strings + strings_used;
  memcpy(out, p, n);
  out[n] = 0;
  strings_used += n + 1;
  return out;
}
static char *save(const char *s) { return save_n(s, strlen(s)); }
static char *decode(const char *p, unsigned n) {
  char *out = save_n(p, n);
  if (out == empty)
    return out;
  unsigned read = 0, write = 0;
  while (read < n) {
    if (p[read] == '&') {
      unsigned end = read + 1;
      while (end < n && end - read < 14 && p[end] != ';' && !whitespace(p[end]))
        end++;
      if (end < n && p[end] == ';') {
        char entity[16];
        unsigned length = end - read - 1;
        memcpy(entity, p + read + 1, length);
        entity[length] = 0;
        unsigned code = 0;
        if (!strcmp(entity, "amp"))
          code = '&';
        else if (!strcmp(entity, "lt"))
          code = '<';
        else if (!strcmp(entity, "gt"))
          code = '>';
        else if (!strcmp(entity, "quot"))
          code = '"';
        else if (!strcmp(entity, "apos"))
          code = '\'';
        else if (!strcmp(entity, "nbsp"))
          code = ' ';
        else if (entity[0] == '#') {
          unsigned base = 10, i = 1;
          if (entity[i] == 'x' || entity[i] == 'X') {
            base = 16;
            i++;
          }
          for (; entity[i]; i++) {
            int v = entity[i] >= '0' && entity[i] <= '9' ? entity[i] - '0'
                    : lower(entity[i]) >= 'a' && lower(entity[i]) <= 'f'
                        ? lower(entity[i]) - 'a' + 10
                        : 99;
            if (v >= (int)base) {
              code = 0;
              break;
            }
            code = code * base + v;
            if (code > 0x10ffff) {
              code = 0;
              break;
            }
          }
        }
        if (code && !(code >= 0xd800 && code <= 0xdfff)) {
          if (code < 128)
            out[write++] = code;
          else if (code < 2048) {
            out[write++] = 0xc0 | (code >> 6);
            out[write++] = 0x80 | (code & 63);
          } else if (code < 65536) {
            out[write++] = 0xe0 | (code >> 12);
            out[write++] = 0x80 | ((code >> 6) & 63);
            out[write++] = 0x80 | (code & 63);
          } else {
            out[write++] = 0xf0 | (code >> 18);
            out[write++] = 0x80 | ((code >> 12) & 63);
            out[write++] = 0x80 | ((code >> 6) & 63);
            out[write++] = 0x80 | (code & 63);
          }
          read = end + 1;
          continue;
        }
      }
    }
    out[write++] = p[read++];
  }
  out[write] = 0;
  return out;
}
int web_create(const char *tag) {
  if (web_node_count >= WEB_NODES)
    return -1;
  int i = web_node_count++;
  struct WebNode *n = &web_nodes[i];
  memset(n, 0, sizeof(*n));
  n->parent = n->first = n->last = n->next = n->first_attr = -1;
  strcopy(n->tag, tag, sizeof(n->tag));
  for (char *p = n->tag; *p; p++)
    *p = lower(*p);
  n->text = n->id = n->classes = n->href = n->src = n->style_text = n->value =
      n->name = n->type = n->action = n->onclick = n->rel = empty;
  changed = web_dirty = 1;
  return i;
}
void web_append(int parent, int child) {
  if (parent < 0 || parent >= web_node_count || child <= 0 ||
      child >= web_node_count || parent == child)
    return;
  unsigned depth = 0;
  for (int p = parent; p >= 0; p = web_nodes[p].parent)
    if (p == child || ++depth > 64)
      return;
  struct WebNode *n = &web_nodes[child];
  if (n->parent >= 0) {
    struct WebNode *old = &web_nodes[n->parent];
    int previous = -1;
    for (int c = old->first; c >= 0; c = web_nodes[c].next) {
      if (c == child) {
        if (previous < 0)
          old->first = n->next;
        else
          web_nodes[previous].next = n->next;
        if (old->last == child)
          old->last = previous;
        break;
      }
      previous = c;
    }
  }
  n->parent = parent;
  n->next = -1;
  struct WebNode *p = &web_nodes[parent];
  if (p->last >= 0)
    web_nodes[p->last].next = child;
  else
    p->first = child;
  p->last = child;
  changed = web_dirty = css_dirty = 1;
}
void web_set_attr(int i, const char *key, const char *value) {
  if (i < 0 || i >= web_node_count)
    return;
  struct WebNode *n = &web_nodes[i];
  char **where = 0;
  if (equal(key, "id"))
    where = &n->id;
  else if (equal(key, "class") || equal(key, "className"))
    where = &n->classes;
  else if (equal(key, "href"))
    where = &n->href;
  else if (equal(key, "src"))
    where = &n->src;
  else if (equal(key, "style"))
    where = &n->style_text;
  else if (equal(key, "value"))
    where = &n->value;
  else if (equal(key, "name"))
    where = &n->name;
  else if (equal(key, "type"))
    where = &n->type;
  else if (equal(key, "action"))
    where = &n->action;
  else if (equal(key, "onclick"))
    where = &n->onclick;
  else if (equal(key, "rel"))
    where = &n->rel;
  char *copy = save(value);
  if (where)
    *where = copy;
  int a = n->first_attr;
  while (a >= 0 && !equal(attributes[a].key, key))
    a = attributes[a].next;
  if (a < 0 && attribute_count < 8192) {
    a = attribute_count++;
    attributes[a].key = save(key);
    attributes[a].next = n->first_attr;
    n->first_attr = a;
  }
  if (a >= 0)
    attributes[a].value = copy;
  changed = web_dirty = 1;
}
const char *web_get_attr(int i, const char *key) {
  if (i < 0 || i >= web_node_count)
    return empty;
  struct WebNode *n = &web_nodes[i];
  if (equal(key, "id"))
    return n->id;
  if (equal(key, "class") || equal(key, "className"))
    return n->classes;
  if (equal(key, "href"))
    return n->href;
  if (equal(key, "src"))
    return n->src;
  if (equal(key, "style"))
    return n->style_text;
  if (equal(key, "value"))
    return n->value;
  if (equal(key, "name"))
    return n->name;
  if (equal(key, "type"))
    return n->type;
  for (int a = n->first_attr; a >= 0; a = attributes[a].next)
    if (equal(attributes[a].key, key))
      return attributes[a].value;
  return empty;
}
int web_has_attr(int i, const char *key) {
  if (i < 0 || i >= web_node_count)
    return 0;
  for (int a = web_nodes[i].first_attr; a >= 0; a = attributes[a].next)
    if (equal(attributes[a].key, key))
      return 1;
  return 0;
}
void web_set_text(int i, const char *text) {
  if (i < 0 || i >= web_node_count)
    return;
  web_nodes[i].text = save(text);
  web_nodes[i].first = web_nodes[i].last = -1;
  changed = web_dirty = css_dirty = 1;
}
static void gather(int i, char *out, unsigned cap, unsigned *used, int depth) {
  if (i < 0 || depth > 64)
    return;
  struct WebNode *n = &web_nodes[i];
  for (const char *p = n->text; *p && *used + 1 < cap; p++)
    out[(*used)++] = *p;
  for (int c = n->first; c >= 0; c = web_nodes[c].next)
    gather(c, out, cap, used, depth + 1);
}
const char *web_get_text(int i) {
  static char text[16384];
  unsigned n = 0;
  gather(i, text, sizeof(text), &n, 0);
  text[n] = 0;
  return text;
}
static int attached(int i) {
  unsigned d = 0;
  while (i > 0 && d++ < 64) {
    int p = web_nodes[i].parent;
    if (p < 0)
      return 0;
    int found = 0;
    for (int c = web_nodes[p].first; c >= 0; c = web_nodes[c].next)
      if (c == i) {
        found = 1;
        break;
      }
    if (!found)
      return 0;
    i = p;
  }
  return i == 0;
}
int web_find(const char *id) {
  for (int i = 0; i < web_node_count; i++)
    if (attached(i) && !strcmp(web_nodes[i].id, id))
      return i;
  return -1;
}
static int class_has(const char *list, const char *name, unsigned length) {
  while (*list) {
    while (whitespace(*list))
      list++;
    const char *end = list;
    while (*end && !whitespace(*end))
      end++;
    if ((unsigned)(end - list) == length && !memcmp(list, name, length))
      return 1;
    list = end;
  }
  return 0;
}
static int simple_match(int i, const char *sel) {
  const struct WebNode *n = &web_nodes[i];
  const char *p = sel;
  if (*p == '*')
    p++;
  else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
    const char *start = p;
    while (*p && *p != '.' && *p != '#' && *p != ':' && *p != '[')
      p++;
    unsigned length = p - start;
    if (strlen(n->tag) != length)
      return 0;
    for (unsigned k = 0; k < length; k++)
      if (lower(start[k]) != n->tag[k])
        return 0;
  }
  while (*p) {
    char kind = *p++;
    if (kind == '[') {
      while (whitespace(*p))
        p++;
      char key[48];
      unsigned at = 0;
      while (*p && *p != ']' && *p != '=' && *p != '~' && *p != '^' &&
             *p != '$' && *p != '*' && *p != '|' && !whitespace(*p)) {
        if (at < 47)
          key[at++] = lower(*p);
        p++;
      }
      key[at] = 0;
      while (whitespace(*p))
        p++;
      char op = 0;
      if (*p != '=')
        op = *p;
      if (*p && *p != ']')
        p++;
      if (op && *p == '=')
        p++;
      if (!web_has_attr(i, key))
        return 0;
      if (op != ']') {
        while (whitespace(*p))
          p++;
        char quote = (*p == '\'' || *p == '"') ? *p++ : 0;
        char value[128];
        at = 0;
        while (*p && (quote ? *p != quote : *p != ']' && !whitespace(*p))) {
          if (at < 127)
            value[at++] = *p;
          p++;
        }
        value[at] = 0;
        if (quote && *p == quote)
          p++;
        const char *actual = web_get_attr(i, key);
        unsigned len = strlen(actual);
        int insensitive = equal(key, "type") || equal(key, "rel");
        while (whitespace(*p))
          p++;
        if (*p == 'i') {
          insensitive = 1;
          p++;
        }
        int match = 0;
        if (!op)
          match = insensitive ? equal(actual, value) : !strcmp(actual, value);
        else if (op == '~')
          match = class_has(actual, value, at);
        else if (op == '^')
          match = !strncmp(actual, value, at);
        else if (op == '$')
          match = len >= at && !strcmp(actual + len - at, value);
        else if (op == '*')
          match = find_ci(actual, value) != 0;
        else if (op == '|')
          match =
              !strncmp(actual, value, at) && (!actual[at] || actual[at] == '-');
        if (!match)
          return 0;
      }
      while (whitespace(*p))
        p++;
      if (*p != ']')
        return 0;
      p++;
      continue;
    }
    const char *start = p;
    while (*p && *p != '.' && *p != '#' && *p != ':' && *p != '[' && *p != '(')
      p++;
    unsigned len = p - start;
    if (kind == '#') {
      if (strlen(n->id) != len || memcmp(n->id, start, len))
        return 0;
    } else if (kind == '.') {
      if (!class_has(n->classes, start, len))
        return 0;
    } else if (kind == ':') {
      if (len == 4 && !memcmp(start, "root", 4)) {
        if (strcmp(n->tag, "html") && i)
          return 0;
      } else if (len == 5 && !memcmp(start, "focus", 5)) {
        if (i != focus)
          return 0;
      } else if ((len == 11 && !memcmp(start, "first-child", 11)) ||
                 (len == 10 && !memcmp(start, "last-child", 10))) {
        int parent = n->parent, first = -1, last = -1;
        if (parent < 0)
          return 0;
        for (int c = web_nodes[parent].first; c >= 0; c = web_nodes[c].next)
          if (strcmp(web_nodes[c].tag, "#text")) {
            if (first < 0)
              first = c;
            last = c;
          }
        if (i != (len == 11 ? first : last))
          return 0;
      } else if (len == 3 && !memcmp(start, "not", 3) && *p == '(') {
        char inner[128];
        unsigned at = 0;
        p++;
        while (*p && *p != ')') {
          if (at < 127)
            inner[at++] = *p;
          p++;
        }
        inner[at] = 0;
        if (*p != ')')
          return 0;
        p++;
        if (simple_match(i, inner))
          return 0;
      } else
        return 0;
    } else
      return 0;
  }
  return 1;
}
static int selector_match(int i, const char *selector) {
  char pieces[8][128], relations[8];
  unsigned count = 0;
  const char *p = selector;
  char relation = ' ';
  while (*p && count < 8) {
    while (whitespace(*p))
      p++;
    if (*p == '>' || *p == '+' || *p == '~') {
      relation = *p++;
      continue;
    }
    unsigned n = 0;
    int bracket = 0, quote = 0;
    while (*p) {
      if (!quote && !bracket &&
          (whitespace(*p) || *p == '>' || *p == '+' || *p == '~'))
        break;
      if (quote) {
        if (*p == quote)
          quote = 0;
      } else if (*p == '\'' || *p == '"')
        quote = *p;
      else if (*p == '[' || *p == '(')
        bracket++;
      else if ((*p == ']' || *p == ')') && bracket)
        bracket--;
      if (n < 127)
        pieces[count][n++] = *p;
      p++;
    }
    if (!n)
      break;
    pieces[count][n] = 0;
    relations[count++] = relation;
    relation = ' ';
  }
  if (*p || !count || !simple_match(i, pieces[count - 1]))
    return 0;
  while (--count) {
    char rel = relations[count];
    if (rel == '+' || rel == '~') {
      int previous = -1, parent = web_nodes[i].parent;
      if (parent < 0)
        return 0;
      for (int c = web_nodes[parent].first; c >= 0 && c != i;
           c = web_nodes[c].next)
        if (strcmp(web_nodes[c].tag, "#text")) {
          if (rel == '+' || simple_match(c, pieces[count - 1]))
            previous = c;
        }
      i = previous;
      if (i < 0 || !simple_match(i, pieces[count - 1]))
        return 0;
    } else {
      i = web_nodes[i].parent;
      if (rel == '>') {
        if (i < 0 || !simple_match(i, pieces[count - 1]))
          return 0;
      } else {
        while (i >= 0 && !simple_match(i, pieces[count - 1]))
          i = web_nodes[i].parent;
        if (i < 0)
          return 0;
      }
    }
  }
  return 1;
}
int web_query(const char *selector) {
  for (int i = 1; i < web_node_count; i++)
    if (attached(i) && selector_match(i, selector))
      return i;
  return -1;
}
int web_body(void) {
  int i = web_query("body");
  return i >= 0 ? i : 0;
}
const char *web_location(void) { return page_url; }
static void resolve_from(const char *base, const char *url, char *out,
                         unsigned cap) {
  char full[2048];
  full[0] = 0;
  out[0] = 0;
  if (strlen(url) >= sizeof(full))
    return;
  if (starts(url, "http://") || starts(url, "https://"))
    strcopy(full, url, sizeof(full));
  else {
    unsigned scheme = starts(base, "https://")  ? 8
                      : starts(base, "http://") ? 7
                                                : 0;
    if (!scheme)
      return;
    if (starts(url, "//")) {
      memcpy(full, base, scheme - 2);
      full[scheme - 2] = 0;
      strcopy(full + scheme - 2, url, sizeof(full) - scheme + 2);
    } else {
      const char *end = base + scheme;
      while (*end && *end != '/' && *end != '?' && *end != '#')
        end++;
      unsigned n = end - base;
      if (*url == '#') {
        n = strlen(base);
        const char *hash = strchr(base, '#');
        if (hash)
          n = hash - base;
      } else if (*url == '?') {
        n = strlen(base);
        const char *query = strchr(base, '?'), *hash = strchr(base, '#');
        if (query)
          n = query - base;
        else if (hash)
          n = hash - base;
      } else if (*url != '/') {
        const char *stop = end;
        while (*stop && *stop != '?' && *stop != '#')
          stop++;
        for (const char *q = end; q < stop; q++)
          if (*q == '/')
            n = q - base + 1;
      }
      if (n + strlen(url) + 2 >= sizeof(full))
        return;
      memcpy(full, base, n);
      full[n] = 0;
      if (*url != '/' && *url != '?' && *url != '#' &&
          n == (unsigned)(end - base)) {
        full[n++] = '/';
        full[n] = 0;
      }
      strcopy(full + n, url, sizeof(full) - n);
    }
  }
  unsigned scheme = starts(full, "https://") ? 8 : 7;
  const char *path = full + scheme;
  while (*path && *path != '/' && *path != '?' && *path != '#')
    path++;
  unsigned used = path - full;
  if (used + 2 >= cap)
    return;
  memcpy(out, full, used);
  out[used++] = '/';
  out[used] = 0;
  unsigned segments[256], count = 0;
  const char *q = path;
  if (*q == '/')
    q++;
  while (*q && *q != '?' && *q != '#') {
    const char *end = q;
    while (*end && *end != '/' && *end != '?' && *end != '#')
      end++;
    unsigned n = end - q;
    if (n == 2 && q[0] == '.' && q[1] == '.') {
      if (count)
        used = segments[--count];
      out[used] = 0;
    } else if (n && !(n == 1 && *q == '.')) {
      if (count >= 256 || used + n + 2 >= cap) {
        out[0] = 0;
        return;
      }
      segments[count++] = used;
      if (out[used - 1] != '/')
        out[used++] = '/';
      memcpy(out + used, q, n);
      used += n;
      out[used] = 0;
    }
    if (*end == '/') {
      q = end + 1;
      if (!*q || *q == '?' || *q == '#') {
        if (out[used - 1] != '/')
          out[used++] = '/';
        out[used] = 0;
      }
    } else
      q = end;
  }
  if (used + strlen(q) >= cap) {
    out[0] = 0;
    return;
  }
  strcopy(out + used, q, cap - used);
}
static void resolve(const char *url, char *out, unsigned cap) {
  resolve_from(page_url, url, out, cap);
}
void web_queue_navigation(const char *url) {
  if (!url || !*url)
    return;
  if (starts(url, "javascript:") || starts(url, "data:") ||
      starts(url, "file:")) {
    gui_status("Esquema de enlace no soportado");
    return;
  }
  resolve(url, navigation, sizeof(navigation));
}
static int resource_queue_from(const char *url, int kind, int node,
                               const char *base) {
  if (resource_count >= 96)
    return 0;
  char absolute[2048];
  resolve_from(base, url, absolute, sizeof(absolute));
  if ((!starts(absolute, "https://") && !starts(absolute, "http://")) ||
      (starts(page_url, "https://") && !starts(absolute, "https://")))
    return 0;
  for (unsigned i = 0; i < resource_count; i++)
    if (kind == 1 && resources[i].node == node && resources[i].kind == kind &&
        !strcmp(resources[i].url, absolute))
      return 1;
  strcopy(resources[resource_count].url, absolute, sizeof(resources[0].url));
  if (kind == 4) {
    unsigned count = 0;
    for (unsigned j = 0; j < resource_count; j++)
      if (resources[j].kind == 4)
        count++;
    if (count >= 32)
      return 0;
  }
  resources[resource_count].kind = kind;
  resources[resource_count++].node = node;
  return 1;
}
static int resource_queue(const char *url, int kind, int node) {
  return resource_queue_from(url, kind, node, page_url);
}
static void stylesheet_imports(const char *text, const char *base) {
  const char *p = text;
  unsigned count = 0;
  while (*p && count < 12) {
    while (whitespace(*p))
      p++;
    if (starts(p, "/*")) {
      const char *end = find_ci(p + 2, "*/");
      p = end ? end + 2 : p + strlen(p);
      continue;
    }
    if (!prefix(p, "@import"))
      break;
    p += 7;
    while (whitespace(*p))
      p++;
    int fn = prefix(p, "url(");
    if (fn) {
      p += 4;
      while (whitespace(*p))
        p++;
    }
    char quote = *p == '\'' || *p == '"' ? *p++ : 0;
    const char *start = p;
    while (*p &&
           (quote ? *p != quote : *p != ')' && *p != ';' && !whitespace(*p)))
      p++;
    unsigned n = p - start;
    char url[2048];
    if (n && n < sizeof(url)) {
      memcpy(url, start, n);
      url[n] = 0;
      resource_queue_from(url, 1, 0, base);
      count++;
    }
    while (*p && *p != ';')
      p++;
    if (*p)
      p++;
  }
}
static int void_tag(const char *t) {
  return equal(t, "input") || equal(t, "img") || equal(t, "br") ||
         equal(t, "hr") || equal(t, "meta") || equal(t, "link") ||
         equal(t, "source") || equal(t, "wbr") || equal(t, "area");
}
static void parse_html(const char *html, int root) {
  int stack[65] = {0};
  stack[0] = root;
  unsigned depth = 0;
  const char *p = html;
  while (*p && web_node_count < WEB_NODES) {
    if (starts(p, "<!--")) {
      const char *end = find_ci(p + 4, "-->");
      p = end ? end + 3 : p + strlen(p);
      continue;
    }
    if (*p != '<') {
      const char *end = p;
      while (*end && *end != '<')
        end++;
      int n = web_create("#text");
      if (n < 0)
        break;
      web_nodes[n].text = decode(p, end - p);
      web_append(stack[depth], n);
      p = end;
      continue;
    }
    p++;
    if (*p == '!' || *p == '?') {
      while (*p && *p != '>')
        p++;
      if (*p)
        p++;
      continue;
    }
    int closing = *p == '/';
    if (closing)
      p++;
    char tag[16];
    unsigned at = 0;
    while (*p && !whitespace(*p) && *p != '>' && *p != '/' && at < 15)
      tag[at++] = lower(*p++);
    tag[at] = 0;
    if (!at) {
      if (*p)
        p++;
      continue;
    }
    if (closing) {
      while (*p && *p != '>')
        p++;
      if (*p)
        p++;
      for (unsigned d = depth; d; d--)
        if (equal(web_nodes[stack[d]].tag, tag)) {
          depth = d - 1;
          break;
        }
      continue;
    }
    if ((equal(tag, "p") || equal(tag, "li")) &&
        equal(web_nodes[stack[depth]].tag, tag) && depth)
      depth--;
    int n = web_create(tag);
    if (n < 0)
      break;
    web_append(stack[depth], n);
    int self = 0;
    while (*p && *p != '>') {
      while (whitespace(*p))
        p++;
      if (*p == '/' && p[1] == '>') {
        self = 1;
        p++;
        break;
      }
      if (*p == '>')
        break;
      char key[48];
      unsigned k = 0;
      while (*p && !whitespace(*p) && *p != '=' && *p != '>' && k < 47)
        key[k++] = lower(*p++);
      key[k] = 0;
      if (!k) {
        p++;
        continue;
      }
      while (whitespace(*p))
        p++;
      const char *v = p;
      unsigned length = 0;
      if (*p == '=') {
        p++;
        while (whitespace(*p))
          p++;
        int quote = *p == '\'' || *p == '"' ? *p++ : 0;
        v = p;
        while (*p && (quote ? *p != quote : !whitespace(*p) && *p != '>'))
          p++;
        length = p - v;
        if (quote && *p)
          p++;
      }
      web_set_attr(n, key, decode(v, length));
    }
    if (*p == '>')
      p++;
    if (equal(tag, "script") || equal(tag, "style") || equal(tag, "title")) {
      char close[32] = "</";
      strcopy(close + 2, tag, sizeof(close) - 2);
      const char *end = find_ci(p, close);
      if (!end)
        end = p + strlen(p);
      web_nodes[n].text =
          equal(tag, "title") ? decode(p, end - p) : save_n(p, end - p);
      if (equal(tag, "style") && sheet_count < 24) {
        sheet_nodes[sheet_count] = n;
        sheets[sheet_count++] = web_nodes[n].text;
        stylesheet_imports(web_nodes[n].text, page_url);
      }
      if (equal(tag, "title"))
        strcopy(web_title, web_nodes[n].text, sizeof(web_title));
      p = end;
      continue;
    }
    if (equal(tag, "link") && *web_nodes[n].href &&
        find_ci(web_nodes[n].rel, "stylesheet") && sheet_count < 24) {
      /* Reserve the cascade position now; network completion must not reorder it. */
      sheet_nodes[sheet_count] = n;
      sheets[sheet_count++] = empty;
      resource_queue(web_nodes[n].href, 1, n);
    }
    if (equal(tag, "img") && *web_nodes[n].src &&
        !starts(web_nodes[n].src, "data:"))
      resource_queue(web_nodes[n].src, 4, n);
    if (!self && !void_tag(tag) && depth < 64)
      stack[++depth] = n;
  }
}
#include "web_style.inc"
static void add_box(int node, int x, int y, int w, int h, const char *text,
                    unsigned bytes, int control) {
  if (box_count >= BOXES)
    return;
  struct Box *b = &boxes[box_count++];
  b->node = node;
  b->x = x;
  b->y = y;
  b->w = w;
  b->h = h;
  b->text = text;
  b->bytes = bytes;
  b->control = control;
  b->size = web_nodes[node].style.size;
  b->color = web_nodes[node].style.color;
}
#include "web_layout.inc"
/* A later inline script often calls a library loaded by the preceding src.
   Keep that dependency intact even though our transport is serial. */
static void scripts_advance(void) {
  while (!script_waiting) {
    if (script_cursor >= web_node_count) {
      if (script_deferred) return;
      script_deferred = 1;
      script_cursor = 1;
    }
    int i = script_cursor++;
    struct WebNode *n = &web_nodes[i];
    if (!equal(n->tag, "script")) continue;
    if (*n->type && !equal(n->type, "text/javascript") &&
        !equal(n->type, "application/javascript")) continue;
    int deferred = *n->src && web_has_attr(i, "defer") && !web_has_attr(i, "async");
    if (deferred != script_deferred) continue;
    if (*n->src) {
      if (resource_queue(n->src, 2, i)) script_waiting = i;
    } else if (*n->text) web_js_eval(n->text, page_url);
  }
}
void web_document(const char *url, const char *html) {
  web_js_reset();
  web_image_reset();
  memset(web_nodes, 0, sizeof(web_nodes));
  web_node_count = 0;
  strings_used = attribute_count = 0;
  rule_count = sheet_count = resource_count = resource_next = box_count = 0;
  focus = -1;
  js_loaded = 0;
  navigation[0] = 0;
  strcopy(page_url, url, sizeof(page_url));
  web_create("#document");
  parse_html(html, 0);
  css_dirty = 1;
  script_cursor = 1;
  script_waiting = script_deferred = 0;
  scripts_advance();
  changed = web_dirty = 1;
}
static void paint_background(int i, unsigned depth) {
  if (depth > 64)
    return;
  struct WebNode *n = &web_nodes[i];
  struct WebStyle *s = &n->style;
  if (!s->display)
    return;
  int x = paint_x + n->x, y = paint_y + n->y - scroll_px;
  if (y < paint_y + paint_h && y + n->h > paint_y && x < paint_x + paint_w &&
      x + n->w > paint_x) {
    if (s->border)
      gui_web_round(x, y, n->w, n->h, s->radius, s->border_color);
    if (s->bg_set)
      gui_web_round(x + s->border, y + s->border, n->w - 2 * s->border,
                    n->h - 2 * s->border, s->radius, s->bg);
  }
  for (int c = n->first; c >= 0; c = web_nodes[c].next)
    paint_background(c, depth + 1);
}
void web_paint(int x, int y, int w, int h, int scroll) {
  if (view_width != w || paint_h != h)
    changed = css_dirty = 1;
  paint_x = x;
  paint_y = y;
  paint_w = w;
  paint_h = h;
  if (view_width != w || paint_h != h)
    changed = css_dirty = 1;
  view_width = w;
  if (changed) {
    if (css_dirty) {
      rule_count = rule_strings_used = 0;
      /* JS-created style elements need the same cascade path as parsed ones. */
      for (int n = 1; n < web_node_count && sheet_count < 24; n++) {
        if (!equal(web_nodes[n].tag, "style") || !attached(n)) continue;
        unsigned slot = 0;
        while (slot < sheet_count && sheet_nodes[slot] != n) slot++;
        if (slot == sheet_count) {
          sheet_nodes[sheet_count] = n;
          sheets[sheet_count++] = empty;
        }
      }
      for (unsigned i = 0; i < sheet_count; i++) {
        int n = sheet_nodes[i];
        if (n && !attached(n)) continue;
        const char *css = n && equal(web_nodes[n].tag, "style") ?
            (web_nodes[n].first < 0 ? web_nodes[n].text : web_get_text(n)) : sheets[i];
        css_parse(css, 0);
      }
      for (unsigned i = 1; i < rule_count; i++) {
        struct Rule current = rules[i];
        unsigned j = i;
        while (j && rules[j - 1].score > current.score) {
          rules[j] = rules[j - 1];
          j--;
        }
        rules[j] = current;
      }
      css_dirty = 0;
    }
    style_node(0, 0);
    box_count = 0;
    document_height = block_node(0, 0, 0, w, 0);
    changed = 0;
  }
  scroll_px = scroll * 18;
  if (scroll_px < 0)
    scroll_px = 0;
  if (scroll_px > document_height - h)
    scroll_px = document_height > h ? document_height - h : 0;
  gui_web_clip(x, y, w, h);
  int body = web_body();
  gui_web_rect(x, y, w, h,
               web_nodes[body].style.bg_set ? web_nodes[body].style.bg
                                            : 0xffffff);
  paint_background(0, 0);
  for (unsigned i = 0; i < box_count; i++) {
    struct Box *b = &boxes[i];
    int bx = x + b->x, by = y + b->y - scroll_px;
    if (by >= y + h || by + b->h <= y || bx >= x + w || bx + b->w <= x)
      continue;
    struct WebStyle *s = &web_nodes[b->node].style;
    if (b->control == 2 &&
        web_image_draw(b->node, bx, by, b->w, b->h, x, y, w, h))
      continue;
    if (b->control) {
      gui_web_round(bx, by, b->w, b->h, s->radius,
                    b->node == focus ? 0x4285f4 : s->border_color);
      gui_web_round(bx + 1, by + 1, b->w - 2, b->h - 2, s->radius,
                    b->control == 2 ? 0xf1f3f4
                    : s->bg_set     ? s->bg
                                    : 0xffffff);
    }
    char word[512];
    unsigned n = b->bytes;
    if (n >= sizeof(word))
      n = sizeof(word) - 1;
    memcpy(word, b->text, n);
    word[n] = 0;
    struct WebNode *node = &web_nodes[b->node];
    int is_input = equal(node->tag, "input") || equal(node->tag, "textarea");
    int advance = font_web_advance(b->size), max_chars = (b->w - 16) / advance;
    unsigned begin = 0;
    if (is_input && b->node == focus && !focus_select && max_chars > 0) {
      unsigned count = 0;
      for (unsigned k = 0; k < focus_cursor && node->value[k]; k++)
        if (((u8)node->value[k] & 0xc0) != 0x80)
          count++;
      while (count > (unsigned)(max_chars - 1) && begin < focus_cursor) {
        begin++;
        while (((u8)node->value[begin] & 0xc0) == 0x80)
          begin++;
        count--;
      }
    }
    if (is_input && equal(node->type, "password")) {
      unsigned count = 0;
      for (const u8 *p = (const u8 *)node->value + begin;
           *p && count + 1 < sizeof(word); p++)
        if ((*p & 0xc0) != 0x80)
          word[count++] = '*';
      word[count] = 0;
    } else if (is_input)
      strcopy(word, node->value + begin, sizeof(word));
    if (b->control && max_chars >= 0) {
      unsigned chars = 0, k = 0;
      while (word[k]) {
        if (((u8)word[k] & 0xc0) != 0x80 && chars++ >= (unsigned)max_chars) {
          word[k] = 0;
          break;
        }
        k++;
      }
    }
    if (b->node == focus && focus_select && is_input)
      gui_web_rect(bx + 6, by + 4, b->w - 12, b->h - 8, 0xd6e8ff);
    u32 text_color = b->color;
    if (is_input && !*word) {
      strcopy(word, web_get_attr(b->node, "placeholder"), sizeof(word));
      text_color = 0x8290a2;
    }
    if (s->pre)
      font_draw_web_mono(bx + (b->control ? 8 : 0), by + (b->control ? 5 : 0),
                         word, text_color, b->size);
    else
      font_draw_web(bx + (b->control ? 8 : 0), by + (b->control ? 5 : 0), word,
                    text_color, b->size);
    if (s->bold && !b->control)
      font_draw_web(bx + 1, by, word, text_color, b->size);
    if (is_input && b->node == focus && !focus_select) {
      int caret =
          bx + 8 +
          font_web_measure(node->value + begin, focus_cursor - begin, b->size);
      if (caret < bx + b->w - 5)
        gui_web_rect(caret, by + 6, 1, b->size + 3, 0x1967d2);
    }
  }
  gui_web_unclip();
  web_dirty = 0;
}
static int actionable(int i) {
  for (unsigned d = 0; i >= 0 && d < 64; d++, i = web_nodes[i].parent)
    if (*web_nodes[i].href || equal(web_nodes[i].tag, "button") ||
        equal(web_nodes[i].tag, "input") ||
        equal(web_nodes[i].tag, "textarea") || *web_nodes[i].onclick)
      return i;
  return -1;
}
static void submit(int i) {
  int activated = i;
  while (i >= 0 && !equal(web_nodes[i].tag, "form"))
    i = web_nodes[i].parent;
  if (i < 0)
    return;
  const char *method = web_get_attr(i, "method");
  if (*method && !equal(method, "get")) {
    gui_status("Este formulario requiere POST, todavia no disponible");
    return;
  }
  if (web_js_event(i, "submit"))
    return;
  char url[2048];
  resolve(*web_nodes[i].action ? web_nodes[i].action : page_url, url,
          sizeof(url));
  char *query = strchr(url, '?');
  if (query)
    *query = 0;
  char *fragment = strchr(url, '#');
  if (fragment)
    *fragment = 0;
  unsigned at = strlen(url);
  if (at + 1 >= sizeof(url))
    return;
  url[at++] = strchr(url, '?') ? '&' : '?';
  const char *hex = "0123456789ABCDEF";
  int first = 1;
  for (int n = 1; n < web_node_count; n++) {
    if (!*web_nodes[n].name || web_has_attr(n, "disabled"))
      continue;
    const char *type = web_nodes[n].type;
    if (equal(type, "button") || equal(type, "reset") || equal(type, "file"))
      continue;
    if ((equal(type, "checkbox") || equal(type, "radio")) &&
        !web_has_attr(n, "checked"))
      continue;
    if ((equal(type, "submit") || equal(web_nodes[n].tag, "button")) &&
        n != activated)
      continue;
    int p = n;
    while (p >= 0 && p != i)
      p = web_nodes[p].parent;
    if (p != i)
      continue;
    if (!first && at + 1 < sizeof(url))
      url[at++] = '&';
    first = 0;
    const char *values[] = {web_nodes[n].name, web_nodes[n].value};
    for (unsigned k = 0; k < 2; k++) {
      if (k && at + 1 < sizeof(url))
        url[at++] = '=';
      for (const u8 *s = (const u8 *)values[k]; *s; s++) {
        if (at + 4 >= sizeof(url)) {
          gui_status("Formulario demasiado largo: no se ha enviado");
          return;
        }
        u8 c = *s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9'))
          url[at++] = c;
        else {
          url[at++] = '%';
          url[at++] = hex[c >> 4];
          url[at++] = hex[c & 15];
        }
      }
    }
  }
  url[at] = 0;
  web_queue_navigation(url);
}
void web_click(int x, int y) {
  int px = x - paint_x, py = y - paint_y + scroll_px;
  for (unsigned k = box_count; k; k--) {
    struct Box *b = &boxes[k - 1];
    if (px < b->x || px >= b->x + b->w || py < b->y || py >= b->y + b->h)
      continue;
    int i = actionable(b->node);
    if (i < 0)
      return;
    if ((equal(web_nodes[i].tag, "input") ||
         equal(web_nodes[i].tag, "textarea")) &&
        !equal(web_nodes[i].type, "submit")) {
      focus = i;
      focus_cursor = strlen(web_nodes[i].value);
      focus_select = 0;
      changed = web_dirty = 1;
      return;
    }
    if (web_js_event(i, "click"))
      return;
    if (*web_nodes[i].onclick)
      web_js_eval(web_nodes[i].onclick, page_url);
    if (*web_nodes[i].href)
      web_queue_navigation(web_nodes[i].href);
    else if ((equal(web_nodes[i].tag, "button") &&
              !equal(web_nodes[i].type, "button") &&
              !equal(web_nodes[i].type, "reset")) ||
             equal(web_nodes[i].type, "submit"))
      submit(i);
    return;
  }
  focus = -1;
  web_dirty = 1;
}
int web_key(int c) {
  if (c == '\t') {
    for (int step = 1; step <= web_node_count; step++) {
      int i = (focus + step + web_node_count) % web_node_count;
      struct WebNode *n = &web_nodes[i];
      if (attached(i) && n->style.display &&
          ((equal(n->tag, "input") && !equal(n->type, "hidden")) ||
           equal(n->tag, "textarea") || equal(n->tag, "button") || *n->href)) {
        focus = i;
        focus_cursor = strlen(web_nodes[i].value);
        focus_select = 0;
        changed = web_dirty = 1;
        return 1;
      }
    }
    return 1;
  }
  if (focus < 0 || focus >= web_node_count)
    return 0;
  struct WebNode *n = &web_nodes[focus];
  if (c == 27) {
    focus = -1;
    web_dirty = 1;
    return 1;
  }
  if (c == '\n' || c == '\r') {
    if (web_js_event(focus, "click"))
      return 1;
    if (*n->onclick)
      web_js_eval(n->onclick, page_url);
    if (*n->href)
      web_queue_navigation(n->href);
    else if (!equal(n->type, "button") && !equal(n->type, "reset"))
      submit(focus);
    return 1;
  }
  if (!equal(n->tag, "input") && !equal(n->tag, "textarea"))
    return 0;
  unsigned length = strlen(n->value);
  if (focus_cursor > length)
    focus_cursor = length;
  if (web_has_attr(focus, "readonly") || web_has_attr(focus, "disabled"))
    return 1;
  if (c == 137) {
    focus_select = 1;
    web_dirty = 1;
    return 1;
  }
  if (c == 20 || c == 21 || c == 22 || c == 23) {
    if (focus_select) {
      focus_cursor = c == 20 || c == 22 ? 0 : length;
      focus_select = 0;
    } else if (c == 22)
      focus_cursor = 0;
    else if (c == 23)
      focus_cursor = length;
    else if (c == 20 && focus_cursor) {
      focus_cursor--;
      while (focus_cursor && ((u8)n->value[focus_cursor] & 0xc0) == 0x80)
        focus_cursor--;
    } else if (c == 21 && focus_cursor < length) {
      focus_cursor++;
      while (((u8)n->value[focus_cursor] & 0xc0) == 0x80)
        focus_cursor++;
    }
    web_dirty = 1;
    return 1;
  }
  char value[512];
  strcopy(value, n->value, sizeof(value));
  length = strlen(value);
  if (focus_select && (c == 8 || c == 7 || c >= 32)) {
    value[0] = 0;
    length = focus_cursor = 0;
    focus_select = 0;
  }
  if (c == 8 && focus_cursor) {
    unsigned start = focus_cursor - 1;
    while (start && ((u8)value[start] & 0xc0) == 0x80)
      start--;
    memmove(value + start, value + focus_cursor, length - focus_cursor + 1);
    focus_cursor = start;
  } else if (c == 7 && focus_cursor < length) {
    unsigned end = focus_cursor + 1;
    while (((u8)value[end] & 0xc0) == 0x80)
      end++;
    memmove(value + focus_cursor, value + end, length - end + 1);
  } else if (((c >= 32 && c < 127) || (c >= 160 && c <= 255))) {
    unsigned limit = 511, count = 0;
    const char *max = web_get_attr(focus, "maxlength");
    if (*max) {
      int parsed = number(max, 0);
      if (parsed >= 0 && (unsigned)parsed < limit)
        limit = parsed;
    }
    for (const u8 *q = (const u8 *)value; *q; q++)
      if ((*q & 0xc0) != 0x80)
        count++;
    if (count >= limit || length + 3 >= sizeof(value)) {
      gui_status("El campo ha alcanzado su limite");
      return 1;
    }
    unsigned bytes = c < 128 ? 1 : 2;
    memmove(value + focus_cursor + bytes, value + focus_cursor,
            length - focus_cursor + 1);
    if (bytes == 1)
      value[focus_cursor++] = c;
    else {
      value[focus_cursor++] = 0xc0 | (c >> 6);
      value[focus_cursor++] = 0x80 | (c & 63);
    }
  } else if (c != 8 && c != 7)
    return 0;
  web_set_attr(focus, "value", value);
  web_js_event(focus, "input");
  changed = web_dirty = 1;
  return 1;
}

int web_next_request(char *url, unsigned cap, int *kind, int *node) {
  if (resource_next >= resource_count)
    return 0;
  struct Resource *r = &resources[resource_next++];
  strcopy(url, r->url, cap);
  *kind = r->kind;
  *node = r->node;
  return 1;
}
void web_set_html(int node, const char *html) {
  if (node < 0 || node >= web_node_count)
    return;
  web_nodes[node].text = empty;
  web_nodes[node].first = web_nodes[node].last = -1;
  parse_html(html, node);
  changed = web_dirty = css_dirty = 1;
}
int web_fetch(const char *url, int token) {
  char absolute[2048];
  resolve(url, absolute, sizeof(absolute));
  unsigned scheme = starts(page_url, "https://") ? 8 : 7;
  unsigned origin = scheme;
  while (page_url[origin] && page_url[origin] != '/' &&
         page_url[origin] != '?' && page_url[origin] != '#')
    origin++;
  if (strncmp(page_url, absolute, origin) ||
      (absolute[origin] && absolute[origin] != '/' && absolute[origin] != '?' &&
       absolute[origin] != '#'))
    return 0;
  return resource_queue(absolute, 3, token);
}
void web_resource(int kind, int node, const char *body, unsigned bytes,
                  int code) {
  (void)bytes;
  if (kind == 3) {
    web_js_fetch_result(node, body, code);
    return;
  }
  if (kind == 2) {
    if (code >= 200 && code < 300) web_js_eval(body, resource_next ? resources[resource_next - 1].url : page_url);
    /* A failed library must not permanently block the rest of the document. */
    script_waiting = 0;
    scripts_advance();
  }
  if (code < 200 || code >= 300) return;
  if (kind == 1) {
    unsigned slot = sheet_count;
    for (unsigned i = 0; i < sheet_count; i++)
      if (node && sheet_nodes[i] == node) { slot = i; break; }
    if (slot >= 24) return;
    if (slot == sheet_count) { sheet_nodes[slot] = node; sheet_count++; }
    sheets[slot] = save(body);
    stylesheet_imports(body, resource_next ? resources[resource_next - 1].url
                                           : page_url);
    css_dirty = 1;
  }

  if (kind == 4)
    web_image_load(node, body, bytes);
  changed = web_dirty = 1;
}

void web_poll(void) {
  web_js_tick();
  if (!browser_busy() && *navigation) {
    char url[2048];
    strcopy(url, navigation, sizeof(url));
    navigation[0] = 0;
    browser_load(url);
    return;
  }
  if (!browser_busy()) {
    char url[2048];
    int kind, node;
    if (web_next_request(url, sizeof(url), &kind, &node))
      browser_resource_load(url, kind, node);
    else if (!js_loaded) {
      js_loaded = 1;
      web_js_loaded();
    }
  }
  if (web_dirty) {
    web_dirty = 0;
    gui_web_redraw();
  }
}

int web_active(void) { return web_node_count > 0; }
int web_scroll_limit(int height) {
  return document_height > height ? (document_height - height + 17) / 18 : 0;
}
void web_cancel_pending(void) {
  resource_next = resource_count;
  navigation[0] = 0;
}
void web_clear_focus(void) { focus = -1; }

void web_home(void) {
  web_document(
      "about:start",
      "<!doctype html><html><head><title>BETA Browser</title><style>"
      "body{margin:0;background:#f3f6fb;color:#253449;font-size:16px}"
      "main{max-width:680px;margin:52px "
      "auto;padding:28px;box-sizing:border-box;text-align:center}"
      "h1{font-size:32px;color:#2861c7;margin:0 0 "
      "12px}p{color:#5f6d82;margin:0 0 24px}"
      "form{display:flex;gap:10px;align-items:center;padding:10px;background:"
      "white;border:1px solid #d7dfeb;border-radius:16px}"
      "input{flex:1;min-width:80px;border:0;padding:10px;box-sizing:border-box}"
      "button{background:#2861c7;color:white;border:0;border-radius:9px;"
      "padding:12px 18px}"
      "nav{margin-top:24px;display:flex;justify-content:center;gap:24px}a{"
      "color:#2861c7}"
      "@media(max-width:500px){main{margin:20px auto;padding:16px}}"
      "</style></head><body><main><h1>BETA Browser</h1><p>Un lugar para "
      "empezar a explorar.</p>"
      "<form action=\"https://www.google.com/search\"><input type=hidden "
      "name=hl value=es>"
      "<input type=hidden name=gbv value=1><input type=hidden name=udm "
      "value=14>"
      "<input type=search name=q placeholder=\"Buscar en Google\" "
      "aria-label=\"Buscar en Google\">"
      "<button type=submit>Buscar</button></form><nav><a "
      "href=\"https://www.google.com/\">Google</a>"
      "<a href=\"https://es.wikipedia.org/\">Wikipedia</a></nav>"
      "<p style=\"margin-top:34px;font-size:12px\">Ctrl-L para escribir una "
      "direccion o buscar</p>"
      "</main></body></html>");
}

int web_focus_scroll(int current, int height) {
  if (focus < 0 || focus >= web_node_count)
    return current;
  struct WebNode *n = &web_nodes[focus];
  if (n->y < current * 18)
    current = n->y / 18;
  else if (n->y + n->h > current * 18 + height)
    current = (n->y + n->h - height + 17) / 18;
  int limit = web_scroll_limit(height);
  if (current < 0)
    current = 0;
  if (current > limit)
    current = limit;
  return current;
}
