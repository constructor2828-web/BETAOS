/* Freestanding C support for the embedded JavaScript VM. No host syscalls. */
#include "os.h"
#include <limits.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"
#define HEAP_BYTES (32u * 1024 * 1024)
struct __attribute__((aligned(16))) Block {
  size_t size;
  struct Block *next;
  unsigned used;
};
static u8 heap[HEAP_BYTES] __attribute__((aligned(16)));
static struct Block *blocks;
void *malloc(size_t n) {
  if (!n)
    n = 1;
  if (n > HEAP_BYTES - sizeof(struct Block) - 16)
    return 0;
  n = (n + 15) & ~(size_t)15;
  if (!blocks) {
    blocks = (struct Block *)heap;
    blocks->size = HEAP_BYTES - sizeof(*blocks);
    blocks->next = 0;
    blocks->used = 0;
  }
  for (struct Block *b = blocks; b; b = b->next)
    if (!b->used && b->size >= n) {
      if (b->size >= n + sizeof(*b) + 16) {
        struct Block *rest = (struct Block *)((u8 *)(b + 1) + n);
        rest->size = b->size - n - sizeof(*b);
        rest->next = b->next;
        rest->used = 0;
        b->next = rest;
        b->size = n;
      }
      b->used = 1;
      return b + 1;
    }
  return 0;
}
void free(void *p) {
  if (!p)
    return;
  struct Block *target = (struct Block *)p - 1;
  if ((u8 *)target < heap || (u8 *)target >= heap + HEAP_BYTES)
    fatal("Invalid runtime free");
  target->used = 0;
  for (struct Block *b = blocks; b && b->next;)
    if (!b->used && !b->next->used) {
      b->size += sizeof(*b) + b->next->size;
      b->next = b->next->next;
    } else
      b = b->next;
}
size_t malloc_usable_size(void *p) {
  return p ? ((struct Block *)p - 1)->size : 0;
}
void *calloc(size_t n, size_t bytes) {
  if (bytes && n > SIZE_MAX / bytes)
    return 0;
  void *p = malloc(n * bytes);
  if (p)
    memset(p, 0, n * bytes);
  return p;
}
void *realloc(void *p, size_t n) {
  if (!p)
    return malloc(n);
  if (!n) {
    free(p);
    return 0;
  }
  size_t old = malloc_usable_size(p);
  if (old >= n)
    return p;
  void *q = malloc(n);
  if (q) {
    memcpy(q, p, old);
    free(p);
  }
  return q;
}
int abs(int n) { return n < 0 ? -n : n; }
void *memchr(const void *p, int value, size_t n) {
  const u8 *s = p;
  for (size_t i = 0; i < n; i++)
    if (s[i] == (u8)value)
      return (void *)(s + i);
  return 0;
}
char *strchr(const char *s, int c) {
  do {
    if ((u8)*s == (u8)c)
      return (char *)s;
  } while (*s++);
  return 0;
}
char *strrchr(const char *s, int c) {
  const char *found = 0;
  do {
    if ((u8)*s == (u8)c)
      found = s;
  } while (*s++);
  return (char *)found;
}
int vsnprintf(char *out, size_t n, const char *format, va_list ap) {
  return stbsp_vsnprintf(out, n > INT_MAX ? INT_MAX : (int)n, format, ap);
}
int snprintf(char *out, size_t n, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int r = vsnprintf(out, n, format, ap);
  va_end(ap);
  return r;
}
static FILE out_file, err_file;
FILE *stdout = &out_file, *stderr = &err_file;
int fprintf(FILE *file, const char *format, ...) {
  (void)file;
  char text[1024];
  va_list ap;
  va_start(ap, format);
  int r = vsnprintf(text, sizeof(text), format, ap);
  va_end(ap);
  serial(text);
  return r;
}
void abort(void) {
  fatal("JavaScript runtime assertion/abort");
  for (;;)
    __asm__ volatile("cli; hlt");
}
void __assert_fail(const char *assertion, const char *file, unsigned line,
                   const char *function) {
  (void)line;
  (void)function;
  serial(file);
  serial(": ");
  serial(assertion);
  serial("\n");
  abort();
}
int gettimeofday(struct timeval *tv, void *zone) {
  (void)zone;
  u32 days = 719528, seconds = 0;
  platform_utc(&days, &seconds);
  tv->tv_sec = ((int64_t)days - 719528) * 86400 + seconds;
  tv->tv_usec = 0;
  return 0;
}
struct tm *localtime_r(const time_t *value, struct tm *out) {
  int64_t days = *value / 86400, seconds = *value % 86400;
  if (seconds < 0) {
    seconds += 86400;
    days--;
  }
  out->tm_hour = seconds / 3600;
  out->tm_min = seconds / 60 % 60;
  out->tm_sec = seconds % 60;
  out->tm_wday = (days + 4) % 7;
  if (out->tm_wday < 0)
    out->tm_wday += 7;
  int year = 1970;
  int64_t remain = days;
  while (remain < 0) {
    year--;
    remain += 365 + (!(year % 4) && (year % 100 || !(year % 400)));
  }
  for (;;) {
    int n = 365 + (!(year % 4) && (year % 100 || !(year % 400)));
    if (remain < n)
      break;
    remain -= n;
    year++;
  }
  out->tm_yday = remain;
  static const u8 md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int month = 0;
  for (; month < 11; month++) {
    int n = md[month] +
            (month == 1 && !(year % 4) && (year % 100 || !(year % 400)));
    if (remain < n)
      break;
    remain -= n;
  }
  out->tm_year = year - 1900;
  out->tm_mon = month;
  out->tm_mday = remain + 1;
  out->tm_isdst = 0;
  out->tm_gmtoff = 0;
  out->tm_zone = "UTC";
  return out;
}

#include <complex.h>
#include <fenv.h>
double(creal)(double _Complex z) { return __real__ z; }
int feholdexcept(fenv_t *env) {
  __asm__ volatile("fnstenv %0" : "=m"(*env));
  u32 mx;
  __asm__ volatile("stmxcsr %0" : "=m"(mx));
  ((u32 *)env)[7] = mx;
  mx = (mx & ~63u) | 0x1f80;
  __asm__ volatile("fnclex; ldmxcsr %0" ::"m"(mx));
  return 0;
}
int feupdateenv(const fenv_t *env) {
  fenv_t value = *env;
  u32 mx;
  u16 status;
  __asm__ volatile("stmxcsr %0; fnstsw %1" : "=m"(mx), "=a"(status));
  ((u32 *)&value)[7] |= mx & 63;
  ((u16 *)&value)[2] |= status & 63;
  __asm__ volatile("fldenv %0; ldmxcsr %1" ::"m"(value),
                   "m"(((u32 *)&value)[7]));
  return 0;
}
