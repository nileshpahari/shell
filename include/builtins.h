#ifndef BUILTINS_H
#define BUILTINS_H

#include "parser.h"

int is_builtin(const char *name);
int handle_builtin(command_t cmd);
void execute_builtin_in_child(command_t cmd);
const char **get_builtin_names(size_t *count);
int builtin_exit_requested(void);

#endif
