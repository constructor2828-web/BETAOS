struct Flow {
  int left, right, x, y, line, align;
  unsigned first_box;
};
static void newline(struct Flow *f, int size) {
  int remaining = f->right - f->x, shift = f->align == 1   ? remaining / 2
                                           : f->align == 2 ? remaining
                                                           : 0;
  if (shift > 0)
    for (unsigned b = f->first_box; b < box_count; b++) {
      boxes[b].x += shift;
      if (boxes[b].control)
        web_nodes[boxes[b].node].x += shift;
    }
  f->x = f->left;
  f->y += f->line ? f->line : size + 6;
  f->line = 0;
  f->first_box = box_count;
}
static int block_node(int, int, int, int, int);
static int declared_width(const struct WebStyle *s, int available) {
  int width = s->width_percent ? s->width * available / 100 : s->width;
  if (width < 0)
    return -1;
  if (!s->border_box)
    width += s->padding[1] + s->padding[3] + 2 * s->border;
  return width;
}
static int intrinsic_at(int i, unsigned depth) {
  if (depth > 32)
    return 32;
  struct WebNode *n = &web_nodes[i];
  struct WebStyle *s = &n->style;
  if (!s->display)
    return 0;
  int explicit_width = declared_width(s, view_width);
  if (explicit_width >= 0)
    return explicit_width;
  int width = 0, line = 0;
  if (equal(n->tag, "input") || equal(n->tag, "button")) {
    const char *text = equal(n->tag, "button") ? web_get_text(i) : n->value;
    if (equal(n->tag, "input") && !equal(n->type, "submit") &&
        !equal(n->type, "button")) {
      int columns =
          *web_get_attr(i, "size") ? number(web_get_attr(i, "size"), 0) : 24;
      if (columns < 1)
        columns = 1;
      if (columns > 100)
        columns = 100;
      width = columns * font_web_advance(s->size);
    } else
      width = font_web_measure(text, strlen(text), s->size);
  } else if (equal(n->tag, "img")) {
    int h;
    web_image_size(i, &width, &h);
    if (!width)
      width = 160;
  } else {
    unsigned count = strlen(n->text);
    if (count > 256)
      count = 256;
    line = font_web_measure(n->text, count, s->size);
    for (int c = n->first; c >= 0; c = web_nodes[c].next) {
      struct WebNode *child = &web_nodes[c];
      if (!child->style.display)
        continue;
      if (equal(child->tag, "br")) {
        if (line > width)
          width = line;
        line = 0;
        continue;
      }
      int value = intrinsic_at(c, depth + 1);
      if (child->style.display == 2 || child->style.display == 4 ||
          (s->display == 3 && !s->flex_dir))
        line += value + 8;
      else {
        if (line > width)
          width = line;
        line = 0;
        if (value > width)
          width = value;
      }
    }
    if (line > width)
      width = line;
  }
  width += s->padding[1] + s->padding[3] + 2 * s->border;
  if (width > 16384)
    width = 16384;
  return width > 16 ? width : 16;
}
static int intrinsic(int i) { return intrinsic_at(i, 0); }
static void inline_node(int i, struct Flow *f, int depth) {
  if (depth > 64)
    return;
  struct WebNode *n = &web_nodes[i];
  struct WebStyle *s = &n->style;
  if (!s->display)
    return;
  int size = s->size, advance = font_web_advance(size),
      height = s->line_height ? s->line_height : size + 7;
  if (equal(n->tag, "br")) {
    newline(f, size);
    return;
  }
  int control = equal(n->tag, "input") || equal(n->tag, "button") ||
                equal(n->tag, "textarea") || equal(n->tag, "select");
  if (control || equal(n->tag, "img")) {
    const char *text = equal(n->tag, "button") ? web_get_text(i) : n->value;
    if (control && !*text && equal(n->type, "submit"))
      text = "Enviar";
    int w = declared_width(s, f->right - f->left), h = s->height;
    if (control) {
      if (w < 0 && equal(n->tag, "input") && *web_get_attr(i, "size")) {
        int columns = number(web_get_attr(i, "size"), 0);
        if (columns > 0 && columns <= 100)
          w = columns * advance + 24;
      }
      if (w < 0)
        w = equal(n->tag, "input") && !equal(n->type, "submit") &&
                    !equal(n->type, "button")
                ? 240
                : font_web_measure(text, strlen(text), size) + s->padding[1] +
                      s->padding[3] + 2 * s->border;
      if (h < 0)
        h = size + s->padding[0] + s->padding[2] + 2 * s->border + 4;
    } else {
      int iw = 0, ih = 0;
      web_image_size(i, &iw, &ih);
      if (w < 0)
        w = iw ? iw : 160;
      if (h < 0)
        h = iw && ih ? w * ih / iw : 80;
      text = web_get_attr(i, "alt");
    }
    if (w > f->right - f->left)
      w = f->right - f->left;
    if (w < 8)
      w = 8;
    if (h < 8)
      h = 8;
    if (h > 4096)
      h = 4096;
    if (f->x + w > f->right && f->x > f->left)
      newline(f, size);
    n->x = f->x;
    n->y = f->y;
    n->w = w;
    n->h = h;
    strcopy(control_labels[i], text, sizeof(control_labels[i]));
    add_box(i, n->x, n->y, w, h, control_labels[i], strlen(control_labels[i]),
            control ? 1 : 2);
    f->x += w + 8;
    if (h > f->line)
      f->line = h;
    return;
  }
  if (s->display == 4) {
    int w = declared_width(s, f->right - f->left);
    if (w < 0)
      w = intrinsic(i);
    if (w > f->right - f->left)
      w = f->right - f->left;
    if (f->x + w > f->right && f->x > f->left)
      newline(f, size);
    s->display = 1;
    int h = block_node(i, f->x, f->y, w, depth + 1);
    s->display = 4;
    f->x += w;
    if (h > f->line)
      f->line = h;
    return;
  }
  int decoration = strcmp(n->tag, "#text") != 0;
  if (decoration)
    f->x += (s->margin[3] > 0 ? s->margin[3] : 0) + s->padding[3];
  unsigned first = box_count;
  const char *text = n->text;
  while (*text) {
    if (whitespace(*text)) {
      if (s->pre && *text == '\n')
        newline(f, size);
      else if (f->x > f->left)
        f->x += s->pre ? advance : font_web_measure(" ", 1, size);
      text++;
      continue;
    }
    const char *end = text;
    unsigned count = 0;
    while (*end && !whitespace(*end) && end - text < 240) {
      if (((u8)*end & 0xc0) != 0x80)
        count++;
      end++;
    }
    if (!count) {
      text = end;
      continue;
    }
    int w = s->pre ? (int)count * advance
                   : font_web_measure(text, end - text, size);
    if (f->x + w > f->right && f->x > f->left)
      newline(f, size);
    if (w > f->right - f->left) {
      unsigned take = (f->right - f->left) / advance;
      if (!take)
        take = 1;
      end = text;
      count = 0;
      while (*end && count < take) {
        if (((u8)*end & 0xc0) != 0x80)
          count++;
        end++;
        while (((u8)*end & 0xc0) == 0x80)
          end++;
      }
      w = s->pre ? (int)count * advance
                 : font_web_measure(text, end - text, size);
    }
    add_box(i, f->x, f->y, w, height, text, end - text, 0);
    f->x += w;
    if (height > f->line)
      f->line = height;
    text = end;
  }
  for (int c = n->first; c >= 0; c = web_nodes[c].next)
    inline_node(c, f, depth + 1);
  if (decoration) {
    if (first < box_count) {
      n->x = boxes[first].x - s->padding[3];
      n->y = boxes[first].y;
      n->w = f->x - n->x + s->padding[1];
      n->h = f->y + f->line - n->y;
    }
    f->x += s->padding[1] + (s->margin[1] > 0 ? s->margin[1] : 0);
  }
}
static void translate_tree(int i, int dx, int dy, unsigned depth) {
  if (depth > 64)
    return;
  web_nodes[i].x += dx;
  web_nodes[i].y += dy;
  for (int c = web_nodes[i].first; c >= 0; c = web_nodes[c].next)
    translate_tree(c, dx, dy, depth + 1);
}
static int layout_flex(int i, struct Flow *f, int depth) {
  struct WebNode *n = &web_nodes[i];
  struct WebStyle *s = &n->style;
  int items[256], widths[256], count = 0, total = 0, grow = 0;
  int available = f->right - f->left;
  if (available < 16)
    available = 16;
  for (int c = n->first; c >= 0 && count < 256; c = web_nodes[c].next) {
    struct WebNode *child = &web_nodes[c];
    if (!child->style.display)
      continue;
    if (equal(child->tag, "#text")) {
      const char *p = child->text;
      while (whitespace(*p))
        p++;
      if (!*p)
        continue;
    }
    items[count] = c;
    int w = declared_width(&child->style, available);
    if (w < 0)
      w = child->style.basis >= 0 ? child->style.basis : intrinsic(c);
    if (s->flex_dir)
      w = available;
    widths[count++] = w;
    total += w;
    grow += child->style.grow;
  }
  if (!count)
    return 0;
  int gaps = s->gap * (count - 1), free = available - total - gaps;
  if (s->flex_dir) {
    for (int j = 0; j < count; j++) {
      int c = items[j];
      struct WebStyle *cs = &web_nodes[c].style;
      int width = declared_width(cs, available);
      if (width < 0)
        width = s->align_items == 3 ? available : intrinsic(c);
      if (width > available)
        width = available;
      int x = f->left + (s->align_items == 1   ? (available - width) / 2
                         : s->align_items == 2 ? available - width
                                               : 0);
      int h = block_node(c, x, f->y, width, depth + 1);
      f->y += h + (j + 1 < count ? s->gap : 0);
    }
    return 1;
  }
  if (!s->flex_wrap && free < 0 && total) {
    for (int j = 0; j < count; j++)
      widths[j] =
          widths[j] * (available - gaps > 0 ? available - gaps : 16) / total;
    free = 0;
  }
  if (free > 0 && grow)
    for (int j = 0; j < count; j++)
      widths[j] += free * web_nodes[items[j]].style.grow / grow;
  int extra_gap = !grow && s->justify == 3 && count > 1 && free > 0
                      ? free / (count - 1)
                      : 0;
  int x = f->left + (!grow && free > 0 ? (s->justify == 1   ? free / 2
                                          : s->justify == 2 ? free
                                                            : 0)
                                       : 0),
      row_y = f->y, row_h = 0;
  unsigned first_box = box_count;
  int row_start = 0;
  for (int j = 0; j < count; j++) {
    int c = items[j], w = widths[j];
    if (w < 16)
      w = 16;
    if (s->flex_wrap && j > row_start && x + w > f->right) {
      row_y += row_h + s->gap;
      row_h = 0;
      x = f->left;
      row_start = j;
      first_box = box_count;
    }
    struct WebStyle saved = web_nodes[c].style;
    web_nodes[c].style.width = w;
    web_nodes[c].style.width_percent = 0;
    web_nodes[c].style.border_box = 1;
    int h = block_node(c, x, row_y, w, depth + 1);
    web_nodes[c].style = saved;
    if (h > row_h)
      row_h = h;
    x += w + s->gap + extra_gap;
    /* Cross-axis alignment is resolved after the final item in each row. */
    if (j + 1 == count || (s->flex_wrap && x + widths[j + 1] > f->right)) {
      for (int k = row_start; k <= j; k++) {
        int item = items[k],
            dy = s->align_items == 1   ? (row_h - web_nodes[item].h) / 2
                 : s->align_items == 2 ? row_h - web_nodes[item].h
                                       : 0;
        if (dy > 0) {
          translate_tree(item, 0, dy, 0);
          for (unsigned b = first_box; b < box_count; b++) {
            int p = boxes[b].node;
            while (p >= 0 && p != item)
              p = web_nodes[p].parent;
            if (p == item)
              boxes[b].y += dy;
          }
        }
      }
    }
  }
  f->y = row_y + row_h;
  return 1;
}
static int block_node(int i, int x, int y, int available, int depth) {
  if (depth > 64)
    return 0;
  struct WebNode *n = &web_nodes[i];
  struct WebStyle *s = &n->style;
  if (!s->display)
    return 0;
  if (available < 16)
    available = 16;
  int ml = s->margin[3] > 0 ? s->margin[3] : 0,
      mr = s->margin[1] > 0 ? s->margin[1] : 0;
  int declared = declared_width(s, available),
      w = declared >= 0 ? declared : available - ml - mr;
  int maximum =
      s->max_width_percent ? s->max_width * available / 100 : s->max_width;
  if (!s->border_box)
    maximum += s->padding[1] + s->padding[3] + 2 * s->border;
  if (w > maximum)
    w = maximum;
  if (w < s->min_width)
    w = s->min_width;
  if (w > available - ml - mr)
    w = available - ml - mr;
  if (w < 16)
    w = 16;
  int spare = available - w - ml - mr;
  if (spare > 0) {
    if (s->margin[3] < 0 && s->margin[1] < 0)
      ml = spare / 2;
    else if (s->margin[3] < 0)
      ml = spare;
  }
  n->x = x + ml;
  n->y = y + (s->margin[0] > 0 ? s->margin[0] : 0);
  n->w = w;
  struct Flow f = {n->x + s->padding[3] + s->border,
                   n->x + w - s->padding[1] - s->border,
                   0,
                   n->y + s->padding[0] + s->border,
                   0,
                   s->align,
                   box_count};
  f.x = f.left;
  if (f.right - f.left < 8)
    f.right = f.left + 8;
  if (equal(n->tag, "input") || equal(n->tag, "button") ||
      equal(n->tag, "img") || equal(n->tag, "textarea")) {
    struct Flow leaf = {n->x, n->x + w, n->x, n->y, 0, 0, box_count};
    inline_node(i, &leaf, depth + 1);
    return n->h;
  }
  if (s->display == 3)
    layout_flex(i, &f, depth);
  else {
    if (*n->text) {
      int first = n->first;
      n->first = -1;
      inline_node(i, &f, depth + 1);
      n->first = first;
    }
    for (int c = n->first; c >= 0; c = web_nodes[c].next) {
      struct WebNode *child = &web_nodes[c];
      if (!child->style.display)
        continue;
      if (child->style.display == 2 || child->style.display == 4 ||
          equal(child->tag, "br") || equal(child->tag, "img"))
        inline_node(c, &f, depth + 1);
      else {
        if (f.x != f.left || f.line)
          newline(&f, s->size);
        int h = block_node(c, f.left, f.y, f.right - f.left, depth + 1);
        f.y += h;
        f.x = f.left;
        f.first_box = box_count;
      }
    }
    if (f.line)
      newline(&f, s->size);
  }
  int h = f.y - n->y + s->padding[2] + s->border;
  int minimum = s->height;
  if (minimum >= 0 && !s->border_box)
    minimum += s->padding[0] + s->padding[2] + 2 * s->border;
  if (minimum > h)
    h = minimum;
  if (h < 1)
    h = 1;
  n->h = h;
  return (s->margin[0] > 0 ? s->margin[0] : 0) + h +
         (s->margin[2] > 0 ? s->margin[2] : 0);
}
