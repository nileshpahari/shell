#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../include/helpers.h"
#include "../include/lexer.h"
#include "../include/parser.h"

static command_t parse_command(token_list list, size_t *pos) {
  command_t cmd = {.argc = 0,
                   .argv = NULL,
                   .redir_in = NULL,
                   .redir_out = NULL,
                   .redir_in_fd = 0,
                   .redir_out_fd = 1,
                   .append = 0};

  size_t i = *pos;
  for (; i < list.count; ++i) {
    token t = list.tokens[i];

    if (t.type == TOKEN_PIPE || t.type == TOKEN_EOF) {
      break;
    }

    if (t.type == TOKEN_STRING) {
      if (i + 1 < list.count && is_number(t.value)) {
        if (list.tokens[i + 1].type == TOKEN_REDIRECT_OUT ||
            list.tokens[i + 1].type == TOKEN_REDIRECT_APPEND) {
          cmd.redir_out_fd = atoi(t.value);
          continue;
        }
        if (list.tokens[i + 1].type == TOKEN_REDIRECT_IN) {
          cmd.redir_in_fd = atoi(t.value);
          continue;
        }
      }

      cmd.argv = realloc(cmd.argv, (cmd.argc + 1) * sizeof(char *));
      cmd.argv[cmd.argc++] = strdup(t.value);
    } else if (t.type == TOKEN_REDIRECT_OUT ||
               t.type == TOKEN_REDIRECT_APPEND) {
      if (i + 1 < list.count && list.tokens[i + 1].type == TOKEN_STRING) {
        cmd.redir_out = strdup(list.tokens[++i].value);
        cmd.append = t.type == TOKEN_REDIRECT_APPEND ? 1 : 0;
      }
    } else if (t.type == TOKEN_REDIRECT_IN) {
      if (i + 1 < list.count && list.tokens[i + 1].type == TOKEN_STRING) {
        cmd.redir_in = strdup(list.tokens[++i].value);
      }
    }
  }

  // skip the pipe token if present
  if (i < list.count && list.tokens[i].type == TOKEN_PIPE) {
    i++;
  }

  *pos = i;

  if (cmd.argc > 0) {
    cmd.argv = realloc(cmd.argv, (cmd.argc + 1) * sizeof(char *));
    cmd.argv[cmd.argc] = NULL;
  }

  return cmd;
}

pipeline_t parse(token_list list) {
  if (list.count >= 2 && list.tokens[list.count - 2].type == TOKEN_PIPE) {
	return (pipeline_t){.commands = NULL, .count = 0, .valid = 0};
  }

  pipeline_t pipeline = {.commands = NULL, .count = 0, .valid = 1};
  size_t pos = 0;

  while (pos < list.count && list.tokens[pos].type != TOKEN_EOF) {
    command_t cmd = parse_command(list, &pos);

    if (cmd.argc == 0) {
      command_free(cmd);
      pipeline_free(pipeline);
      return (pipeline_t){.commands = NULL, .count = 0, .valid = 0};
    }

    pipeline.commands =
        realloc(pipeline.commands, (pipeline.count + 1) * sizeof(command_t));
    pipeline.commands[pipeline.count++] = cmd;
  }

  return pipeline;
}

void command_free(command_t cmd) {
  for (size_t i = 0; i < cmd.argc; ++i) {
    free(cmd.argv[i]);
  }
  free(cmd.argv);
  free(cmd.redir_in);
  free(cmd.redir_out);
}

void pipeline_free(pipeline_t pipeline) {
  for (size_t i = 0; i < pipeline.count; ++i) {
    command_free(pipeline.commands[i]);
  }
  free(pipeline.commands);
}
