#include "os.h"
void *memmove(void *dst, const void *src, size_t size) {
  u8 *d = dst;
  const u8 *s = src;
  if (d < s)
    for (size_t i = 0; i < size; i++)
      d[i] = s[i];
  else
    while (size) {
      size--;
      d[size] = s[size];
    }
  return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
  const u8 *x = a, *y = b;
  for (size_t i = 0; i < n; i++)
    if (x[i] != y[i])
      return x[i] - y[i];
  return 0;
}

int strncmp(const char *a, const char *b, size_t n) {
  for (size_t i=0;i<n;i++) {
    unsigned char x=a[i],y=b[i];
    if (x!=y || !x) return (int)x-(int)y;
  }
  return 0;
}
