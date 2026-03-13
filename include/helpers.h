#ifndef HELPER_H
#define HELPER_H

#include "parser.h"
#include <ctype.h>

#define PATH_LIST_SEPARATOR ":"

char* find_in_path(const char* cmd);

int apply_redirection(command_t cmd);

int is_number(const char* s);

#endif  
