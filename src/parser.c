#include "../include/parser.h"
#include "../include/lexer.h"
#include <stdlib.h>
#include <string.h>

command_t parse(token_list list) {
  command_t cmd = {.argc = 0,
                   .argv = NULL,
                   .redir_in = NULL,
                   .redir_out = NULL,
                   .append = 0};

  for (size_t i = 0; i < list.count; ++i) {
    token t = list.tokens[i];
    if (t.type == TOKEN_WORD || t.type == TOKEN_STRING) {
      cmd.argv = realloc(cmd.argv, (cmd.argc + 1) * sizeof(char *));
      // Safer but not clean ig
      // if failed to realloc
      //    exit(1);

      cmd.argv[cmd.argc++] = strdup(t.value);
    } else if (t.type == TOKEN_REDIRECT_OUT ||
               t.type == TOKEN_REDIRECT_APPEND) {
      if (i + 1 < list.count && (list.tokens[i + 1].type == TOKEN_WORD ||
                                 list.tokens[i + 1].type == TOKEN_STRING)) {
        cmd.redir_out = strdup(list.tokens[++i].value);
        cmd.append = t.type == TOKEN_REDIRECT_APPEND ? 1 : 0;
      }
    } else if (t.type == TOKEN_REDIRECT_IN) {
      if (i + 1 < list.count && (list.tokens[i + 1].type == TOKEN_WORD ||
                                 list.tokens[i + 1].type == TOKEN_STRING)) {
        cmd.redir_in = strdup(list.tokens[++i].value);
      }
    } else if (t.type == TOKEN_PIPE) {
      break;
    }
  }

  if (cmd.argc > 0) {
    cmd.argv = realloc(cmd.argv, (cmd.argc + 1) * sizeof(char *));
    cmd.argv[cmd.argc] = NULL;
  }

  return cmd;
}

void command_free(command_t cmd) {
  for (size_t i = 0; i < cmd.argc; ++i) {
	  free(cmd.argv[i]);
  }
  free(cmd.argv);
  free(cmd.redir_in);
  free(cmd.redir_out);
}
