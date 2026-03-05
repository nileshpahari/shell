#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PATH_LIST_SEPARATOR ":"

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  char *path = getenv("PATH");

  const char *cmds[] = {"exit", "echo", "type"};
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

    if (strcmp(cmd, "exit") == 0) {
      break;
    } else if (strcmp(cmd, "echo") == 0) {
      printf("%s\n", rest ? rest : "");
    } else if (strcmp(cmd, "type") == 0) {
      if (!rest) {
        printf("type: missing argument\n");
        continue;
      }

      int flag = 0;

      for (size_t i = 0; i < cmds_len; ++i) {
        if (strcmp(cmds[i], rest) == 0) {
          flag = 1;
          break;
        }
      }

      char *full_path = NULL;
      if (!flag && path) {
        char *copy = strdup(path);
        char *dir = strtok(copy, PATH_LIST_SEPARATOR);

        while (dir) {
          char full[1024];
          snprintf(full, sizeof(full), "%s/%s", dir, rest);

          if (access(full, X_OK) == 0) {
            full_path = strdup(full);
            flag = 2;
            break;
          }

          dir = strtok(NULL, PATH_LIST_SEPARATOR);
        }

        free(copy);
      }

      if (flag == 1) {
        printf("%s is a shell builtin\n", rest);
      } else if (flag == 2) {
        printf("%s is %s\n", rest, full_path);
        free(full_path);
      } else {
        printf("%s: not found\n", rest);
      }

    } else if (path) {
      int flag = 0;
      char *copy_path = strdup(path);
      char *dir = strtok(copy_path, PATH_LIST_SEPARATOR);

      char *full_path = NULL;
      while (dir) {
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, cmd);

        if (access(full, X_OK) == 0) {
          pid_t pid = fork();
          if (pid == 0) {
            char **args = malloc(sizeof(char *));
            size_t cnt = 1;
            args[0] = cmd;

            if (rest) {
              char *arg = strtok(rest, " ");
              while (arg) {
                args = realloc(args, (cnt + 1) * sizeof(char *));
                args[cnt++] = arg;
                arg = strtok(NULL, " ");
              }
            }

            args = realloc(args, (cnt + 1) * sizeof(char *));
            args[cnt++] = NULL;

            execv(full, args);
            free(args);
            perror("execv");
            exit(1);
          } else if (pid > 0) {
            wait(NULL);
          } else {
            perror("fork");
          }
          flag = 1;
          break;
        }
        dir = strtok(NULL, PATH_LIST_SEPARATOR);
      }
      free(copy_path);

      if (!flag) {
        printf("%s: command not found\n", input);
      }

    } else {
      printf("%s: command not found\n", input);
    }
  }
  free(input);

  return 0;
}
