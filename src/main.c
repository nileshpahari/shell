#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../include/executor.h"
#include "../include/parser.h"
#include "../include/helpers.h"

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
