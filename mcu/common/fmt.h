/* Tiny, heap-free printf subset for the debug consoles.
 * Supports %d %i %u %x %X %c %s %% with optional '-', '0', width and the
 * 'l' length modifier. No floating point: print fixed-point integers. */
#pragma once

#include <stdarg.h>
#include <stddef.h>

typedef void (*fmt_putc_t)(char c, void *ctx);

int fmt_vformat(fmt_putc_t out, void *ctx, const char *f, va_list ap);
int fmt_snprintf(char *buf, size_t size, const char *f, ...)
    __attribute__((format(printf, 3, 4)));
