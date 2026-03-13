#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

typedef struct {
  size_t argc;
  char **argv;
  char *redir_in;
  char *redir_out;
  int redir_in_fd;
  int redir_out_fd;
  int append;
} command_t;

command_t parse(token_list tokens);

void command_free(command_t cmd);

#endif
