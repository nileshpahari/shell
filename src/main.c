#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <readline/history.h>
#include <readline/readline.h>

#include "../include/executor.h"
#include "../include/helpers.h"
#include "../include/parser.h"

int main() {
  setbuf(stdout, NULL);

  while (1) {
    char *input = readline(build_prompt());

    if (!input) {
      printf("\n");
      break;
    }

    if (is_all_spaces(input)) {
      free(input);
      continue;
    }

    add_history(input);

    token_list list = lex(input);
    pipeline_t pipeline = parse(list);

    if (!pipeline.valid) {
      token_list_free(list);
      pipeline_free(pipeline);
	  free(input);
      fprintf(stderr, "Syntax error: unexpected token\n");
      continue;
    }

    execute(pipeline);
    token_list_free(list);
    pipeline_free(pipeline);
	free(input);
  }

  return 0;
}
