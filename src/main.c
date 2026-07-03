#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../include/editor.h"
#include "../include/executor.h"
#include "../include/helpers.h"
#include "../include/parser.h"

int main() {
  setbuf(stdout, NULL);

  while (1) {
    char *prompt = build_prompt();
    char *input = editor_readline(prompt);
    free(prompt);

    if (!input) {
      if (errno == EAGAIN) {
        // Ctrl-C
        continue;
      }
      // EOF (Ctrl-D)
      break;
    }

    if (is_all_spaces(input)) {
      free(input);
      continue;
    }

    editor_history_add(input);

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
