#include "fmt.h"

#include <stdbool.h>
#include <stdint.h>

static int emit_num(fmt_putc_t out, void *ctx, unsigned long v, unsigned base,
                    bool upper, bool neg, int width, bool zero, bool left) {
  char tmp[24];
  int n = 0;
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  do {
    tmp[n++] = digits[v % base];
    v /= base;
  } while (v != 0u && n < (int)sizeof(tmp));
  int len = n + (neg ? 1 : 0);
  int count = 0;
  if (!left && !zero) {
    for (; len < width; ++len, ++count) out(' ', ctx);
  }
  if (neg) {
    out('-', ctx);
    ++count;
  }
  if (!left && zero) {
    for (; len < width; ++len, ++count) out('0', ctx);
  }
  while (n > 0) {
    out(tmp[--n], ctx);
    ++count;
  }
  if (left) {
    for (; len < width; ++len, ++count) out(' ', ctx);
  }
  return count;
}

int fmt_vformat(fmt_putc_t out, void *ctx, const char *f, va_list ap) {
  int count = 0;
  for (; *f != '\0'; ++f) {
    if (*f != '%') {
      out(*f, ctx);
      ++count;
      continue;
    }
    ++f;
    bool left = false;
    bool zero = false;
    int width = 0;
    bool is_long = false;
    for (;; ++f) {
      if (*f == '-') {
        left = true;
      } else if (*f == '0') {
        zero = true;
      } else {
        break;
      }
    }
    while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
    if (*f == 'l') {
      is_long = true;
      ++f;
    }
    switch (*f) {
      case 'd':
      case 'i': {
        long v = is_long ? va_arg(ap, long) : va_arg(ap, int);
        bool neg = v < 0;
        unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1u : (unsigned long)v;
        count += emit_num(out, ctx, u, 10u, false, neg, width, zero, left);
        break;
      }
      case 'u':
      case 'x':
      case 'X': {
        unsigned long v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
        unsigned base = (*f == 'u') ? 10u : 16u;
        count += emit_num(out, ctx, v, base, *f == 'X', false, width, zero, left);
        break;
      }
      case 'c':
        out((char)va_arg(ap, int), ctx);
        ++count;
        break;
      case 's': {
        const char *s = va_arg(ap, const char *);
        if (s == NULL) s = "(null)";
        int len = 0;
        while (s[len] != '\0') ++len;
        if (!left) {
          for (; len < width; ++len, ++count) out(' ', ctx);
        }
        for (; *s != '\0'; ++s, ++count) out(*s, ctx);
        if (left) {
          for (; len < width; ++len, ++count) out(' ', ctx);
        }
        break;
      }
      case '%':
        out('%', ctx);
        ++count;
        break;
      case '\0':
        return count;
      default:
        out('%', ctx);
        out(*f, ctx);
        count += 2;
        break;
    }
  }
  return count;
}

typedef struct {
  char *buf;
  size_t size;
  size_t pos;
} sn_ctx_t;

static void sn_putc(char c, void *ctx) {
  sn_ctx_t *s = (sn_ctx_t *)ctx;
  if (s->pos + 1u < s->size) s->buf[s->pos] = c;
  ++s->pos;
}

int fmt_snprintf(char *buf, size_t size, const char *f, ...) {
  sn_ctx_t s = {buf, size, 0u};
  va_list ap;
  va_start(ap, f);
  int n = fmt_vformat(sn_putc, &s, f, ap);
  va_end(ap);
  if (size != 0u) buf[s.pos < size ? s.pos : size - 1u] = '\0';
  return n;
}
