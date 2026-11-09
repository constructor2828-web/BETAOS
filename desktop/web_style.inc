/* Bounded CSS parser/cascade. Kept separate from DOM and layout. */
static char rule_strings[512 * 1024];
static unsigned rule_strings_used;
static char *css_save(const char *p, unsigned n) {
  if (n + 1 > sizeof(rule_strings) - rule_strings_used)
    return empty;
  char *s = rule_strings + rule_strings_used;
  memcpy(s, p, n);
  s[n] = 0;
  rule_strings_used += n + 1;
  return s;
}
static int number(const char *s, int base) {
  while (whitespace(*s))
    s++;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+')
    s++;
  long long n = 0, fraction = 0, divisor = 1;
  while (*s >= '0' && *s <= '9') {
    if (n < 100000)
      n = n * 10 + *s - '0';
    s++;
  }
  if (*s == '.') {
    s++;
    while (*s >= '0' && *s <= '9') {
      if (divisor < 10000) {
        fraction = fraction * 10 + *s - '0';
        divisor *= 10;
      }
      s++;
    }
  }
  n = n * divisor + fraction;
  if (*s == '%')
    n = n * base / 100;
  else if (prefix(s, "rem") || prefix(s, "em"))
    n = n * base;
  else if (prefix(s, "vw"))
    n = n * view_width / 100;
  else if (prefix(s, "vh"))
    n = n * paint_h / 100;
  n /= divisor;
  if (n > 16384)
    n = 16384;
  return (int)n * sign;
}
static u32 color(const char *s, u32 fallback) {
  while (whitespace(*s))
    s++;
  if (*s == '#') {
    unsigned n = 0, count = 0;
    s++;
    while (count < 8) {
      int c = lower(*s), v = c >= '0' && c <= '9'   ? c - '0'
                             : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                    : 16;
      if (v == 16)
        break;
      n = n * 16 + v;
      count++;
      s++;
    }
    if (count == 3 || count == 4) {
      if (count == 4)
        n >>= 4;
      return ((n >> 8) & 15) * 0x110000 + ((n >> 4) & 15) * 0x1100 +
             (n & 15) * 0x11;
    }
    if (count == 8)
      n >>= 8;
    return count == 6 || count == 8 ? n : fallback;
  }
  static const char *names[] = {
      "black",  "white",  "red",           "green",     "blue",    "gray",
      "grey",   "silver", "navy",          "teal",      "aqua",    "cyan",
      "lime",   "yellow", "orange",        "purple",    "fuchsia", "magenta",
      "maroon", "olive",  "rebeccapurple", "whitesmoke"};
  static const u32 values[] = {0,        0xffffff, 0xff0000, 0x008000, 0x0000ff,
                               0x808080, 0x808080, 0xc0c0c0, 0x000080, 0x008080,
                               0x00ffff, 0x00ffff, 0x00ff00, 0xffff00, 0xffa500,
                               0x800080, 0xff00ff, 0xff00ff, 0x800000, 0x808000,
                               0x663399, 0xf5f5f5};
  for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++)
    if (equal(s, names[i]))
      return values[i];
  if (prefix(s, "rgb(") || prefix(s, "rgba(")) {
    s = strchr(s, '(') + 1;
    unsigned v[3] = {0};
    for (int i = 0; i < 3; i++) {
      while (*s && !((*s >= '0' && *s <= '9') || *s == '-'))
        s++;
      int n = number(s, 255);
      v[i] = n < 0 ? 0 : n > 255 ? 255 : (unsigned)n;
      while (*s && *s != ',' && *s != ')' && !whitespace(*s))
        s++;
      if (*s == ',')
        s++;
    }
    return (v[0] << 16) | (v[1] << 8) | v[2];
  }
  return fallback;
}
static int css_next(const char **cursor, char *key, unsigned key_cap,
                    char *value, unsigned cap, int *important) {
  const char *p = *cursor;
  for (;;) {
    while (whitespace(*p) || *p == ';')
      p++;
    if (starts(p, "/*")) {
      const char *e = find_ci(p + 2, "*/");
      p = e ? e + 2 : p + strlen(p);
      continue;
    }
    if (!*p) {
      *cursor = p;
      return 0;
    }
    break;
  }
  unsigned n = 0;
  while (*p && *p != ':' && *p != ';') {
    if (n + 1 < key_cap)
      key[n++] = lower(*p);
    p++;
  }
  while (n && whitespace(key[n - 1]))
    n--;
  key[n] = 0;
  if (*p != ':') {
    if (*p)
      p++;
    *cursor = p;
    *key = 0;
    *value = 0;
    *important = 0;
    return 1;
  }
  p++;
  while (whitespace(*p))
    p++;
  const char *start = p;
  int quote = 0, level = 0;
  while (*p) {
    if (quote) {
      if (*p == '\\' && p[1])
        p++;
      else if (*p == quote)
        quote = 0;
    } else if (*p == '\'' || *p == '"')
      quote = *p;
    else if (*p == '(')
      level++;
    else if (*p == ')' && level)
      level--;
    else if (*p == ';' && !level)
      break;
    p++;
  }
  n = p - start;
  if (n >= cap)
    n = cap - 1;
  memcpy(value, start, n);
  value[n] = 0;
  while (n && whitespace(value[n - 1]))
    value[--n] = 0;
  *important = 0;
  char *bang = strchr(value, '!');
  if (bang) {
    char *q = bang + 1;
    while (whitespace(*q))
      q++;
    if (equal(q, "important")) {
      *important = 1;
      *bang = 0;
      n = bang - value;
      while (n && whitespace(value[n - 1]))
        value[--n] = 0;
    }
  }
  *cursor = *p ? p + 1 : p;
  return 1;
}
static int custom_value(int node, const char *name, char *out, unsigned cap) {
  for (unsigned depth = 0; node >= 0 && depth < 65;
       node = web_nodes[node].parent, depth++) {
    int found = 0, best = -1;
    for (unsigned r = 0; r <= rule_count; r++) {
      if (r < rule_count && !selector_match(node, rules[r].selector))
        continue;
      const char *p =
          r == rule_count ? web_nodes[node].style_text : rules[r].text;
      char key[64], value[512];
      int imp;
      while (css_next(&p, key, sizeof(key), value, sizeof(value), &imp))
        if (!strcmp(key, name)) {
          int score =
              (r == rule_count ? 10000 : rules[r].score) + (imp ? 100000 : 0);
          if (score >= best) {
            best = score;
            strcopy(out, value, cap);
            found = 1;
          }
        }
    }
    if (found)
      return 1;
  }
  return 0;
}
static int expand_vars(int node, const char *source, char *out, unsigned cap,
                       unsigned depth) {
  if (depth > 8)
    return 0;
  unsigned used = 0;
  const char *p = source;
  while (*p && used + 1 < cap) {
    if (starts(p, "var(")) {
      p += 4;
      const char *start = p;
      int nesting = 1;
      while (*p && nesting) {
        if (*p == '(')
          nesting++;
        else if (*p == ')')
          nesting--;
        if (nesting)
          p++;
      }
      if (!*p)
        return 0;
      const char *comma = start;
      while (comma < p && *comma != ',')
        comma++;
      const char *end = comma;
      while (end > start && whitespace(end[-1]))
        end--;
      while (start < end && whitespace(*start))
        start++;
      char key[64], raw[512], expanded[512];
      unsigned n = end - start;
      if (n >= sizeof(key))
        return 0;
      memcpy(key, start, n);
      key[n] = 0;
      if (!custom_value(node, key, raw, sizeof(raw))) {
        if (comma == p)
          return 0;
        n = p - comma - 1;
        if (n >= sizeof(raw))
          n = sizeof(raw) - 1;
        memcpy(raw, comma + 1, n);
        raw[n] = 0;
      }
      if (!expand_vars(node, raw, expanded, sizeof(expanded), depth + 1))
        return 0;
      n = strlen(expanded);
      if (n >= cap - used)
        return 0;
      memcpy(out + used, expanded, n);
      used += n;
      p++;
    } else
      out[used++] = *p++;
  }
  out[used] = 0;
  return !*p;
}
static void declarations(struct WebStyle *st, const char *text, int node,
                         int important_pass) {
  const char *p = text;
  char key[64], raw[512], v[512];
  int important;
  while (css_next(&p, key, sizeof(key), raw, sizeof(raw), &important)) {
    if (important != important_pass || !key[0] || starts(key, "--"))
      continue;
    if (!expand_vars(node, raw, v, sizeof(v), 0))
      continue;
    if (equal(key, "color"))
      st->color = color(v, st->color);
    else if (equal(key, "background") || equal(key, "background-color")) {
      st->bg_set = !equal(v, "transparent");
      st->bg = color(v, st->bg);
    } else if (equal(key, "font-size")) {
      int n = number(v, st->size);
      if (n >= 8 && n <= 32)
        st->size = n;
    } else if (equal(key, "font-weight"))
      st->bold = prefix(v, "bold") || number(v, 0) >= 600;
    else if (equal(key, "line-height")) {
      char factor[64];
      strcopy(factor, v, sizeof(factor));
      if (!strchr(v, 'p') && !strchr(v, '%') && !strchr(v, 'e') &&
          !strchr(v, 'v')) {
        unsigned len = strlen(factor);
        if (len + 2 < sizeof(factor)) {
          factor[len] = 'e';
          factor[len + 1] = 'm';
          factor[len + 2] = 0;
        }
      }
      int n = number(factor, st->size);
      if (n >= 8 && n <= 128)
        st->line_height = n;
    } else if (equal(key, "width")) {
      st->width = prefix(v, "auto") ? -1 : number(v, 100);
      st->width_percent = strchr(v, '%') != 0;
    } else if (equal(key, "height"))
      st->height = prefix(v, "auto") ? -1 : number(v, paint_h);
    else if (equal(key, "min-width"))
      st->min_width = number(v, view_width);
    else if (equal(key, "max-width")) {
      st->max_width = prefix(v, "none") ? 16384 : number(v, 100);
      st->max_width_percent = strchr(v, '%') != 0;
    } else if (equal(key, "box-sizing"))
      st->border_box = prefix(v, "border-box");
    else if (equal(key, "display"))
      st->display = prefix(v, "none")          ? 0
                    : equal(v, "inline-block") ? 4
                    : prefix(v, "inline")      ? 2
                    : prefix(v, "flex")        ? 3
                                               : 1;
    else if (equal(key, "flex-direction"))
      st->flex_dir = prefix(v, "column");
    else if (equal(key, "flex-wrap"))
      st->flex_wrap = prefix(v, "wrap");
    else if (equal(key, "justify-content"))
      st->justify = prefix(v, "center")                        ? 1
                    : prefix(v, "flex-end") || equal(v, "end") ? 2
                    : prefix(v, "space-between")               ? 3
                                                               : 0;
    else if (equal(key, "align-items"))
      st->align_items = prefix(v, "center")                        ? 1
                        : prefix(v, "flex-end") || equal(v, "end") ? 2
                        : prefix(v, "stretch")                     ? 3
                                                                   : 0;
    else if (equal(key, "flex-grow") || equal(key, "flex")) {
      st->grow = number(v, 100);
      if (!strchr(v, 'p') && !prefix(v, "none") && !prefix(v, "auto") &&
          number(v, 0) > 0) {
        st->grow = number(v, 0) * 100;
        if (equal(key, "flex"))
          st->basis = 0;
      }
    } else if (equal(key, "flex-basis"))
      st->basis = prefix(v, "auto") ? -1 : number(v, view_width);
    else if (equal(key, "text-align"))
      st->align = prefix(v, "center") ? 1 : prefix(v, "right") ? 2 : 0;
    else if (equal(key, "gap") || equal(key, "column-gap"))
      st->gap = number(v, view_width);
    else if (equal(key, "white-space"))
      st->pre = prefix(v, "pre");
    else if (equal(key, "border-radius"))
      st->radius = number(v, view_width);
    else if (equal(key, "border-color"))
      st->border_color = color(v, st->border_color);
    else if (equal(key, "border") || equal(key, "border-width")) {
      st->border =
          prefix(v, "none") || prefix(v, "0") ? 0 : number(v, view_width);
      const char *q = v;
      while (*q) {
        if (*q == '#' || prefix(q, "rgb(")) {
          st->border_color = color(q, st->border_color);
          break;
        }
        q++;
      }
    } else if (prefix(key, "margin") || prefix(key, "padding")) {
      int margin = prefix(key, "margin"),
          *edges = margin ? st->margin : st->padding;
      const char *side = key + (margin ? 6 : 7);
      int value = margin && prefix(v, "auto") ? -1 : number(v, view_width);
      if (!*side) {
        int nums[4], count = 0;
        const char *q = v;
        while (*q && count < 4) {
          while (whitespace(*q))
            q++;
          if (!*q)
            break;
          nums[count++] =
              margin && prefix(q, "auto") ? -1 : number(q, view_width);
          while (*q && !whitespace(*q))
            q++;
        }
        if (count) {
          edges[0] = nums[0];
          edges[1] = count > 1 ? nums[1] : nums[0];
          edges[2] = count > 2 ? nums[2] : nums[0];
          edges[3] = count > 3 ? nums[3] : edges[1];
        }
      } else if (equal(side, "-top"))
        edges[0] = value;
      else if (equal(side, "-right"))
        edges[1] = value;
      else if (equal(side, "-bottom"))
        edges[2] = value;
      else if (equal(side, "-left"))
        edges[3] = value;
    }
  }
  if (st->border < 0)
    st->border = 0;
  if (st->border > 8)
    st->border = 8;
  if (st->radius < 0)
    st->radius = 0;
  if (st->radius > 64)
    st->radius = 64;
  if (st->gap < 0)
    st->gap = 0;
  if (st->gap > 256)
    st->gap = 256;
  for (int i = 0; i < 4; i++) {
    if (st->padding[i] < 0)
      st->padding[i] = 0;
    if (st->padding[i] > 256)
      st->padding[i] = 256;
    if (st->margin[i] < -1)
      st->margin[i] = -1;
    if (st->margin[i] > 256)
      st->margin[i] = 256;
  }
}
static int media_matches(const char *p, unsigned count) {
  char condition[256];
  if (count >= sizeof(condition))
    return 0;
  memcpy(condition, p, count);
  condition[count] = 0;
  if (find_ci(condition, "print") || find_ci(condition, "not screen"))
    return 0;
  const char *q = condition;
  while ((q = strchr(q, '('))) {
    q++;
    const char *colon = strchr(q, ':');
    if (!colon)
      break;
    int value = number(colon + 1, view_width);
    if (prefix(q, "max-width") && view_width > value)
      return 0;
    if (prefix(q, "min-width") && view_width < value)
      return 0;
    if (prefix(q, "max-height") && paint_h > value)
      return 0;
    if (prefix(q, "min-height") && paint_h < value)
      return 0;
    q = colon + 1;
  }
  return 1;
}
static void css_parse(const char *css, int depth) {
  if (depth > 4)
    return;
  const char *p = css;
  while (*p && rule_count < RULES) {
    while (whitespace(*p))
      p++;
    if (starts(p, "/*")) {
      const char *end = find_ci(p + 2, "*/");
      p = end ? end + 2 : p + strlen(p);
      continue;
    }
    const char *start = p;
    int quote = 0;
    while (*p && *p != '{') {
      if (*p == ';' && *start == '@') {
        p++;
        start = p;
      } else
        p++;
    }
    if (!*p)
      break;
    const char *body = ++p;
    int braces = 1;
    while (*p && braces) {
      if (quote) {
        if (*p == '\\' && p[1])
          p++;
        else if (*p == quote)
          quote = 0;
      } else if (*p == '\'' || *p == '"')
        quote = *p;
      else if (*p == '{')
        braces++;
      else if (*p == '}')
        braces--;
      if (braces)
        p++;
    }
    unsigned len = p - body;
    if (*p)
      p++;
    if (*start == '@') {
      if (prefix(start, "@media") && media_matches(start, body - start - 1))
        css_parse(css_save(body, len), depth + 1);
      continue;
    }
    const char *end = body - 1;
    while (end > start && whitespace(end[-1]))
      end--;
    char *decl = css_save(body, len);
    while (start < end && rule_count < RULES) {
      const char *comma = start;
      int square = 0;
      while (comma < end && (*comma != ',' || square)) {
        if (*comma == '[' || *comma == '(')
          square++;
        if ((*comma == ']' || *comma == ')') && square)
          square--;
        comma++;
      }
      unsigned n = comma - start;
      while (n && whitespace(start[n - 1]))
        n--;
      while (n && whitespace(*start)) {
        start++;
        n--;
      }
      if (n && n < 128) {
        struct Rule *r = &rules[rule_count++];
        memcpy(r->selector, start, n);
        r->selector[n] = 0;
        r->text = decl;
        r->score = 1;
        for (unsigned k = 0; k < n; k++)
          r->score += start[k] == '#' ? 100
                      : start[k] == '.' || start[k] == '[' || start[k] == ':'
                          ? 10
                          : 0;
      }
      start = comma < end ? comma + 1 : end;
    }
  }
}
static void style_node(int i, int depth) {
  if (depth > 64)
    return;
  struct WebNode *n = &web_nodes[i];
  struct WebStyle *s = &n->style;
  memset(s, 0, sizeof(*s));
  s->display = 1;
  s->size = 16;
  s->color = 0x202124;
  s->width = s->height = s->basis = -1;
  s->max_width = 16384;
  s->border_color = 0xdadce0;
  s->gap = 0;
  s->align_items = 3;
  if (n->parent >= 0) {
    struct WebStyle *p = &web_nodes[n->parent].style;
    s->size = p->size;
    s->color = p->color;
    s->pre = p->pre;
    s->align = p->align;
    s->line_height = p->line_height;
  }
  if (equal(n->tag, "#text") || equal(n->tag, "span") || equal(n->tag, "a") ||
      equal(n->tag, "b") || equal(n->tag, "strong") || equal(n->tag, "i") ||
      equal(n->tag, "em") || equal(n->tag, "label") || equal(n->tag, "img"))
    s->display = 2;
  if (equal(n->tag, "head") || equal(n->tag, "script") ||
      equal(n->tag, "style") || equal(n->tag, "title") ||
      equal(n->tag, "link") || equal(n->tag, "meta") ||
      equal(n->tag, "noscript"))
    s->display = 0;
  if (equal(n->tag, "body"))
    for (int k = 0; k < 4; k++)
      s->margin[k] = 8;
  if (equal(n->tag, "center"))
    s->align = 1;
  if (equal(n->tag, "table"))
    s->display = 4;
  if (equal(n->tag, "tr")) {
    s->display = 3;
    s->gap = 4;
  }
  if (n->tag[0] == 'h' && n->tag[1] >= '1' && n->tag[1] <= '6' && !n->tag[2]) {
    s->size = 34 - (n->tag[1] - '1') * 3;
    s->bold = 1;
    s->margin[0] = 12;
    s->margin[2] = 10;
  }
  if (equal(n->tag, "p") || equal(n->tag, "ul") || equal(n->tag, "ol"))
    s->margin[2] = 12;
  if (equal(n->tag, "pre")) {
    s->pre = 1;
    s->bg = 0xf1f3f4;
    s->bg_set = 1;
    for (int k = 0; k < 4; k++)
      s->padding[k] = 8;
  }
  if (equal(n->tag, "a"))
    s->color = 0x1967d2;
  if (equal(n->tag, "b") || equal(n->tag, "strong"))
    s->bold = 1;
  if (equal(n->tag, "input") || equal(n->tag, "button") ||
      equal(n->tag, "textarea") || equal(n->tag, "select")) {
    s->display = 2;
    s->border = 1;
    s->radius = 5;
    s->bg = 0xffffff;
    s->bg_set = 1;
    s->padding[0] = s->padding[2] = 8;
    s->padding[1] = s->padding[3] = 12;
  }
  const char *align = web_get_attr(i, "align");
  if (*align)
    s->align = equal(align, "center") ? 1 : equal(align, "right") ? 2 : 0;
  const char *w = web_get_attr(i, "width"), *h = web_get_attr(i, "height");
  if (*w) {
    s->width = number(w, 100);
    s->width_percent = strchr(w, '%') != 0;
  }
  if (*h)
    s->height = number(h, paint_h);
  const char *bg = web_get_attr(i, "bgcolor");
  if (*bg) {
    s->bg = color(bg, 0xffffff);
    s->bg_set = 1;
  }
  for (int pass = 0; pass < 2; pass++) {
    for (unsigned r = 0; r < rule_count; r++)
      if (selector_match(i, rules[r].selector))
        declarations(s, rules[r].text, i, pass);
    declarations(s, n->style_text, i, pass);
  }
  /* Hidden controls are never rendered, even if author rules target all inputs.
   */
  if ((equal(n->tag, "input") && equal(n->type, "hidden")) ||
      web_has_attr(i, "hidden"))
    s->display = 0;
  for (int c = n->first; c >= 0; c = web_nodes[c].next)
    style_node(c, depth + 1);
}
