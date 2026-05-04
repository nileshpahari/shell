#include "../include/executor.h"
#include "../include/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PROMPT_COLOR "\x1b[1;32m"
#define PROMPT_RESET "\x1b[0m"

static void print_prompt(void) {
  char *cwd = getcwd(NULL, 0);

  if (cwd) {
    printf("%s\n", cwd);
    free(cwd);
  } else {
    perror("getcwd");
  }

  printf(PROMPT_COLOR ">" PROMPT_RESET " ");
  fflush(stdout);
}

int main() {
  setbuf(stdout, NULL);

  char *input = NULL;
  size_t len = 0;

  while (1) {
    print_prompt();

    if (getline(&input, &len, stdin) == -1) {
      printf("\n");
      break;
    }

    token_list list = lex(input);
    pipeline_t pipeline = parse(list);

    if (!pipeline.valid) {
      token_list_free(list);
      pipeline_free(pipeline);
      fprintf(stderr, "Syntax error: unexpected token\n");
      continue;
    }

    if (pipeline.count == 0) {
      token_list_free(list);
      pipeline_free(pipeline);
      continue;
    }

    execute(pipeline);
    token_list_free(list);
    pipeline_free(pipeline);
  }

  free(input);

  return 0;
}
