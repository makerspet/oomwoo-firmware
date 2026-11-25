#include "cli.h"

#include <stdarg.h>
#include <string.h>

static cli_write_t s_write;
static const cli_cmd_t *s_cmds;
static uint32_t s_ncmds;
static const char *s_prompt;
static char s_line[CLI_LINE_MAX];
static uint32_t s_len;

typedef struct {
  char buf[64];
  uint32_t n;
} out_ctx_t;

static void out_flush(out_ctx_t *o) {
  if (o->n != 0u) s_write(o->buf, o->n);
  o->n = 0u;
}

static void out_putc(char c, void *ctx) {
  out_ctx_t *o = (out_ctx_t *)ctx;
  if (c == '\n') {
    o->buf[o->n++] = '\r';
    if (o->n == sizeof(o->buf)) out_flush(o);
  }
  o->buf[o->n++] = c;
  if (o->n == sizeof(o->buf)) out_flush(o);
}

void cli_printf(const char *f, ...) {
  out_ctx_t o;
  o.n = 0u;
  va_list ap;
  va_start(ap, f);
  fmt_vformat(out_putc, &o, f, ap);
  va_end(ap);
  out_flush(&o);
}

void cli_init(cli_write_t write, const cli_cmd_t *cmds, uint32_t ncmds, const char *prompt) {
  s_write = write;
  s_cmds = cmds;
  s_ncmds = ncmds;
  s_prompt = prompt;
  s_len = 0u;
  cli_printf("\n%s", s_prompt);
}

static void run_line(void) {
  char *argv[CLI_ARGS_MAX];
  int argc = 0;
  char *p = s_line;
  while (*p != '\0' && argc < (int)CLI_ARGS_MAX) {
    while (*p == ' ') *p++ = '\0';
    if (*p == '\0') break;
    argv[argc++] = p;
    while (*p != '\0' && *p != ' ') ++p;
  }
  if (argc == 0) return;
  if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
    for (uint32_t i = 0; i < s_ncmds; ++i) cli_printf("  %-10s %s\n", s_cmds[i].name, s_cmds[i].help);
    return;
  }
  for (uint32_t i = 0; i < s_ncmds; ++i) {
    if (strcmp(argv[0], s_cmds[i].name) == 0) {
      s_cmds[i].fn(argc, argv);
      return;
    }
  }
  cli_printf("unknown command '%s' (try help)\n", argv[0]);
}

void cli_feed(char c) {
  if (c == '\r' || c == '\n') {
    cli_printf("\n");
    s_line[s_len] = '\0';
    run_line();
    s_len = 0u;
    cli_printf("%s", s_prompt);
  } else if (c == 0x08 || c == 0x7F) {
    if (s_len != 0u) {
      --s_len;
      s_write("\b \b", 3u);
    }
  } else if (c >= ' ' && s_len + 1u < CLI_LINE_MAX) {
    s_line[s_len++] = c;
    s_write(&c, 1u);
  }
}

bool cli_int(const char *s, long *out) {
  long v = 0;
  bool neg = false;
  if (s == NULL || *s == '\0') return false;
  if (*s == '-') {
    neg = true;
    ++s;
  }
  if (*s == '\0') return false;
  for (; *s != '\0'; ++s) {
    if (*s < '0' || *s > '9') return false;
    v = v * 10 + (*s - '0');
    if (v > 100000000L) return false;
  }
  *out = neg ? -v : v;
  return true;
}

bool cli_on_off(const char *s, bool *out) {
  if (s == NULL) return false;
  if (strcmp(s, "on") == 0 || strcmp(s, "1") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(s, "off") == 0 || strcmp(s, "0") == 0) {
    *out = false;
    return true;
  }
  return false;
}
