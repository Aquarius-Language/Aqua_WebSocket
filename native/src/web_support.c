#include "aq_plat.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *aq_calloc(size_t n, size_t size) { return calloc(n, size); }
void aq_free(void *p) { free(p); }
size_t aq_strlen(const char *p) { return strlen(p); }
int aq_strcmp(const char *a, const char *b) { return strcmp(a, b); }
int aq_memcmp(const void *a, const void *b, size_t n) {
  return memcmp(a, b, n);
}
long long aq_atoll(const char *s, const char **end) {
  char *last;
  long long v = strtoll(s, &last, 10);
  if (end)
    *end = last;
  return v;
}
void aq_buf_reserve(aq_buf *b, size_t extra) {
  if (b->failed || extra > SIZE_MAX - b->len - 1) {
    b->failed = 1;
    return;
  }
  size_t need = b->len + extra + 1;
  if (need <= b->cap)
    return;
  size_t cap = b->cap ? b->cap : 128;
  while (cap < need) {
    if (cap > SIZE_MAX / 2) {
      cap = need;
      break;
    }
    cap *= 2;
  }
  void *p = realloc(b->data, cap);
  if (!p) {
    b->failed = 1;
    return;
  }
  b->data = p;
  b->cap = cap;
}
void aq_buf_append(aq_buf *b, const void *p, size_t n) {
  aq_buf_reserve(b, n);
  if (!b->failed) {
    if (n)
      memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = 0;
  }
}
void aq_buf_putc(aq_buf *b, char c) { aq_buf_append(b, &c, 1); }
void aq_buf_puts(aq_buf *b, const char *s) { aq_buf_append(b, s, strlen(s)); }
void aq_buf_printf(aq_buf *b, const char *fmt, ...) {
  va_list args, copy;
  va_start(args, fmt);
  va_copy(copy, args);
  int n = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (n < 0) {
    b->failed = 1;
  } else {
    aq_buf_reserve(b, (size_t)n);
    if (!b->failed) {
      vsnprintf((char *)b->data + b->len, (size_t)n + 1, fmt, args);
      b->len += (size_t)n;
    }
  }
  va_end(args);
}
void aq_buf_free(aq_buf *b) {
  free(b->data);
  memset(b, 0, sizeof *b);
}
