#ifndef BUILTINS_H
#define BUILTINS_H

#include "./parser.h"

int is_builtin(const char *name);
int handle_builtin(command_t cmd);
void execute_builtin_in_child(command_t cmd);

#endif // !BUILTINS_H
