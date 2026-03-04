#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  const char *cmds[] = {"exit", "echo",
                        "type"}; // array of pointer to str (not actual str arr)
  size_t cmds_len = sizeof(cmds) / sizeof(cmds[0]);
  char *input = NULL;
  size_t len = 0;

  while (1) {
    printf("$ ");
    if (getline(&input, &len, stdin) == -1) {
      break;
    }
    input[strcspn(input, "\n")] = 0;

    if (input[0] == '\0') {
      continue;
    }

    char *cmd = input;
    char *rest = NULL;
    char *space = strchr(input, ' ');

    if (space) {
      *space = 0;
      rest = space + 1;
    }

    if (strcmp(input, "exit") == 0) {
      break;
    } else if (strcmp(cmd, "echo") == 0) {
      printf("%s\n", rest ? rest : "");
    } else if (strcmp(cmd, "type") == 0) {
      bool flag = 0;
      for (size_t i = 0; i < cmds_len; ++i) {
        if (strcmp(cmds[i], rest) == 0) {
          flag = 1;
          break;
        }
      }

      if (!flag) {
        printf("%s: not found\n", rest);
      } else {
        printf("%s is a shell builtin\n", rest);
      }

    } else {
      printf("%s: command not found\n", input);
    }
  }
  free(input);

  return 0;
}
