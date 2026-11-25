/* Line-based bench console: "cmd arg arg" + Enter. Heap-free. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fmt.h"

#define CLI_LINE_MAX 96u
#define CLI_ARGS_MAX 8u

typedef void (*cli_write_t)(const char *s, uint32_t len);

typedef struct {
  const char *name;
  const char *help;
  void (*fn)(int argc, char **argv);
} cli_cmd_t;

void cli_init(cli_write_t write, const cli_cmd_t *cmds, uint32_t ncmds, const char *prompt);
void cli_feed(char c);
void cli_printf(const char *f, ...) __attribute__((format(printf, 1, 2)));

/* Argument helpers; return false and print usage hints on bad input. */
bool cli_int(const char *s, long *out);
bool cli_on_off(const char *s, bool *out);
