#include "../include/builtins.h"
#include "../include/executor.h"
#include "../include/parser.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  char *input = NULL;
  size_t len = 0;

  while (1) {
    printf("$ ");

    if (getline(&input, &len, stdin) == -1) {
      break;
    }

    token_list list = lex(input);
    command_t cmd = parse(list);

    if (!handle_builtin(cmd)) {
      token_list_free(list);
      command_free(cmd);
      continue;
    }

    execute_command(cmd);

    token_list_free(list);
    command_free(cmd);
  }

  free(input);

  return 0;
}
