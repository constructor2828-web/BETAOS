#include "os.h"
struct Label {
  char name[32];
  u32 offset;
};
struct Fixup {
  char name[32];
  u32 site;
};
struct Assembler {
  u8 *out;
  u32 at, cap, line;
  int failed;
  char *error;
  u32 error_cap;
  struct Label labels[128];
  unsigned label_count;
  struct Fixup fixes[128];
  unsigned fix_count;
};
static struct Assembler state;
static char *trim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
    s[--n] = 0;
  return s;
}
static int lower_equal(const char *a, const char *b) {
  while (*a && *b) {
    char c = *a++;
    if (c >= 'A' && c <= 'Z')
      c += 32;
    if (c != *b++)
      return 0;
  }
  return !*a && !*b;
}
static void error(struct Assembler *a, const char *message) {
  if (a->failed)
    return;
  a->failed = 1;
  if (!a->error || !a->error_cap)
    return;
  char num[16], rev[16];
  unsigned i = 0, j = 0, n = a->line;
  do {
    rev[i++] = '0' + n % 10;
    n /= 10;
  } while (n);
  while (i)
    num[j++] = rev[--i];
  num[j] = 0;
  strcopy(a->error, "Line ", a->error_cap);
  strcopy(a->error + strlen(a->error), num, a->error_cap - strlen(a->error));
  strcopy(a->error + strlen(a->error), ": ", a->error_cap - strlen(a->error));
  strcopy(a->error + strlen(a->error), message,
          a->error_cap - strlen(a->error));
}
static void emit(struct Assembler *a, u8 b) {
  if (a->at >= a->cap) {
    error(a, "Program exceeds output capacity");
    return;
  }
  a->out[a->at++] = b;
}
static void emit32(struct Assembler *a, u32 n) {
  for (unsigned i = 0; i < 4; i++)
    emit(a, n >> (i * 8));
}
static int number(const char *s, u64 *out) {
  int neg = 0;
  if (*s == '-') {
    neg = 1;
    s++;
  }
  unsigned base = 10;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    base = 16;
    s += 2;
  }
  u64 n = 0;
  unsigned digits = 0;
  while (*s) {
    unsigned d = *s >= '0' && *s <= '9'   ? *s - '0'
                 : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10
                 : *s >= 'A' && *s <= 'F' ? *s - 'A' + 10
                                          : 99;
    if (d >= base || n > (~(u64)0 - d) / base)
      return 0;
    n = n * base + d;
    s++;
    digits++;
  }
  if (!digits)
    return 0;
  *out = neg ? 0 - n : n;
  return 1;
}
static int reg(const char *s) {
  static const char *names[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp",
                                "rsi", "rdi", "r8",  "r9",  "r10", "r11",
                                "r12", "r13", "r14", "r15"};
  for (int i = 0; i < 16; i++)
    if (lower_equal(s, names[i]))
      return i;
  return -1;
}
static int valid_label(const char *s) {
  unsigned n = 0;
  while (*s) {
    char c = *s++;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
          (n && c >= '0' && c <= '9')))
      return 0;
    n++;
  }
  return n && n < 32;
}
static void fix(struct Assembler *a, const char *name) {
  if (!valid_label(name) || a->fix_count >= 128) {
    error(a, "Invalid label or too many references");
    return;
  }
  struct Fixup *f = &a->fixes[a->fix_count++];
  strcopy(f->name, name, 32);
  f->site = a->at;
  emit32(a, 0);
}
static void data_bytes(struct Assembler *a, char *s) {
  while (*s && !a->failed) {
    s = trim(s);
    if (*s == '"' || *s == '\'') {
      char quote = *s++;
      while (*s && *s != quote) {
        char c = *s++;
        if (c == '\\') {
          c = *s++;
          if (!c) {
            error(a, "Unterminated escape");
            return;
          }
          if (c == 'n')
            c = '\n';
          else if (c == 'r')
            c = '\r';
          else if (c == 't')
            c = '\t';
        }
        emit(a, c);
      }
      if (*s != quote) {
        error(a, "Unterminated string");
        return;
      }
      s++;
    } else {
      char token[32];
      unsigned n = 0;
      while (*s && *s != ',' && n < 31)
        token[n++] = *s++;
      token[n] = 0;
      u64 value;
      if (!number(trim(token), &value) ||
          ((value > 255) && (int64_t)value < -128)) {
        error(a, "DB expects a string or byte value");
        return;
      }
      if (value > 255 && (int64_t)value >= 0) {
        error(a, "Byte value out of range");
        return;
      }
      emit(a, value);
    }
    s = trim(s);
    if (!*s)
      break;
    if (*s++ != ',') {
      error(a, "Expected comma in DB");
      return;
    }
    if (!*trim(s)) {
      error(a, "Missing byte value");
      return;
    }
  }
}
static void instruction(struct Assembler *a, char *s) {
  s = trim(s);
  if (!*s)
    return;
  char *colon = s;
  while (*colon && *colon != ':' && *colon != ' ' && *colon != '\t')
    colon++;
  if (*colon == ':') {
    *colon = 0;
    if (!valid_label(s) || a->label_count >= 128) {
      error(a, "Invalid label");
      return;
    }
    for (unsigned i = 0; i < a->label_count; i++)
      if (!strcmp(s, a->labels[i].name)) {
        error(a, "Duplicate label");
        return;
      }
    strcopy(a->labels[a->label_count].name, s, 32);
    a->labels[a->label_count++].offset = a->at;
    s = trim(colon + 1);
    if (!*s)
      return;
  }
  char *args = s;
  while (*args && *args != ' ' && *args != '\t')
    args++;
  if (*args)
    *args++ = 0;
  args = trim(args);
  if (lower_equal(s, "bits")) {
    if (!lower_equal(args, "64"))
      error(a, "Only BITS 64 is supported");
    return;
  }
  if (lower_equal(s, "db")) {
    data_bytes(a, args);
    return;
  }
  if (lower_equal(s, "ret") || lower_equal(s, "nop")) {
    if (*args) {
      error(a, "Unexpected operand");
      return;
    }
    emit(a, lower_equal(s, "ret") ? 0xc3 : 0x90);
    return;
  }
  if (lower_equal(s, "push") || lower_equal(s, "pop")) {
    int r = reg(args);
    if (r < 0) {
      error(a, "Expected 64-bit register");
      return;
    }
    if (r > 7)
      emit(a, 0x41);
    emit(a, (lower_equal(s, "push") ? 0x50 : 0x58) + (r & 7));
    return;
  }
  if (lower_equal(s, "inc") || lower_equal(s, "dec")) {
    int r = reg(args);
    if (r < 0) {
      error(a, "Expected register");
      return;
    }
    emit(a, 0x48 | (r >> 3));
    emit(a, 0xff);
    emit(a, 0xc0 | (lower_equal(s, "dec") ? 8 : 0) | (r & 7));
    return;
  }
  if (lower_equal(s, "call") && args[0] == '[') {
    char compact[64];
    unsigned n = 0;
    for (char *p = args; *p && n < 63; p++)
      if (*p != ' ' && *p != '\t')
        compact[n++] = *p;
    compact[n] = 0;
    int disp = lower_equal(compact, "[rbx+8]")    ? 8
               : lower_equal(compact, "[rbx+16]") ? 16
               : lower_equal(compact, "[rbx+24]") ? 24
                                                  : -1;
    if (disp < 0) {
      error(a, "API call must use [rbx+8], [rbx+16], or [rbx+24]");
      return;
    }
    emit(a, 0xff);
    emit(a, 0x53);
    emit(a, disp);
    return;
  }
  if (lower_equal(s, "jmp") || lower_equal(s, "jnz") || lower_equal(s, "jz") ||
      lower_equal(s, "call")) {
    if (lower_equal(s, "jnz") || lower_equal(s, "jz")) {
      emit(a, 0x0f);
      emit(a, lower_equal(s, "jnz") ? 0x85 : 0x84);
    } else
      emit(a, lower_equal(s, "jmp") ? 0xe9 : 0xe8);
    fix(a, args);
    return;
  }
  char *comma = args;
  while (*comma && *comma != ',')
    comma++;
  if (!*comma) {
    error(a, "Expected two operands");
    return;
  }
  *comma = 0;
  char *left = trim(args), *right = trim(comma + 1);
  int dst = reg(left), src = reg(right);
  if (dst < 0) {
    error(a, "Invalid destination register");
    return;
  }
  if (lower_equal(s, "lea")) {
    if (!starts(right, "[rel ")) {
      error(a, "LEA expects [rel label]");
      return;
    }
    size_t n = strlen(right);
    if (n < 7 || right[n - 1] != ']') {
      error(a, "Invalid LEA expression");
      return;
    }
    right[n - 1] = 0;
    emit(a, 0x48 | ((dst >> 3) << 2));
    emit(a, 0x8d);
    emit(a, 5 | ((dst & 7) << 3));
    fix(a, trim(right + 5));
    return;
  }
  int mov = lower_equal(s, "mov"), add = lower_equal(s, "add"),
      sub = lower_equal(s, "sub"), cmp = lower_equal(s, "cmp"),
      xr = lower_equal(s, "xor");
  if (!(mov || add || sub || cmp || xr)) {
    error(a, "Unsupported instruction");
    return;
  }
  if (src >= 0) {
    emit(a, 0x48 | ((src >> 3) << 2) | (dst >> 3));
    emit(a, mov ? 0x89 : add ? 1 : sub ? 0x29 : cmp ? 0x39 : 0x31);
    emit(a, 0xc0 | ((src & 7) << 3) | (dst & 7));
    return;
  }
  u64 value;
  if (!number(right, &value) || xr) {
    error(a, "Invalid immediate or operand");
    return;
  }
  emit(a, 0x48 | (dst >> 3));
  if (mov) {
    emit(a, 0xb8 + (dst & 7));
    for (unsigned i = 0; i < 8; i++)
      emit(a, value >> (i * 8));
  } else {
    if ((int64_t)value > 2147483647ll || (int64_t)value < -2147483648ll) {
      error(a, "Immediate must fit signed 32 bits");
      return;
    }
    emit(a, 0x81);
    emit(a, 0xc0 | (sub ? 40 : cmp ? 56 : 0) | (dst & 7));
    emit32(a, value);
  }
}
int assemble(const char *source, u8 *output, u32 capacity, char *message,
             u32 message_capacity) {
  if (!source || !output || (!message && message_capacity))
    return 0;
  struct Assembler *a = &state;
  memset(a, 0, sizeof(*a));
  a->out = output;
  a->cap = capacity;
  a->at = 16;
  a->error = message;
  a->error_cap = message_capacity;
  if (message_capacity)
    message[0] = 0;
  if (capacity < 17)
    return 0;
  while (*source && !a->failed) {
    char line[512];
    unsigned n = 0;
    a->line++;
    int quote = 0, escape = 0, comment = 0;
    while (*source && *source != '\n') {
      char c = *source++;
      if (comment)
        continue;
      if (!quote && c == ';') {
        comment = 1;
        continue;
      }
      if (!escape && (c == '"' || c == '\'')) {
        if (!quote)
          quote = c;
        else if (quote == c)
          quote = 0;
      }
      if (c == '\\' && quote && !escape)
        escape = 1;
      else
        escape = 0;
      if (n >= sizeof(line) - 1) {
        error(a, "Source line too long");
        break;
      }
      line[n++] = c;
    }
    if (*source == '\n')
      source++;
    line[n] = 0;
    instruction(a, line);
  }
  if (a->failed)
    return 0;
  if (a->at == 16) {
    error(a, "Empty program");
    return 0;
  }
  for (unsigned i = 0; i < a->fix_count; i++) {
    int found = 0;
    for (unsigned j = 0; j < a->label_count; j++)
      if (!strcmp(a->fixes[i].name, a->labels[j].name)) {
        u32 site = a->fixes[i].site;
        write_le32(output + site, a->labels[j].offset - (site + 4));
        found = 1;
        break;
      }
    if (!found) {
      error(a, "Undefined label");
      return 0;
    }
  }
  memcpy(output, "BEX1", 4);
  write_le32(output + 4, a->at - 16);
  write_le32(output + 8, 0);
  write_le32(output + 12, 1);
  for (unsigned i = 0; i < a->label_count; i++)
    if (!strcmp(a->labels[i].name, "start"))
      write_le32(output + 8, a->labels[i].offset - 16);
  if (read_le32(output + 8) >= a->at - 16) {
    error(a, "Entry label points past the program");
    return 0;
  }
  strcopy(message, "Assembly successful", message_capacity);
  return a->at;
}
