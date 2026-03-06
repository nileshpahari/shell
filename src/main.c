#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PATH_LIST_SEPARATOR ":"

char *trim(char *s) {
  char *start = s;
  char *end = s;

  while (*start && isspace((unsigned char)*start))
    start++;

  while (*start) {
    *end++ = *start++;
  }
  *end = '\0';

  end--;
  while (end >= s && isspace((unsigned char)*end)) {
    *end = '\0';
    end--;
  }

  return s;
}

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  char *path = getenv("PATH");

  const char *cmds[] = {"exit", "echo", "type", "pwd", "cd"};
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

    char *input_copy2 = strdup(input);
    char *out_redir = strstr(input_copy2, "1>");
    int offset = 1;
    if (!out_redir) {
      out_redir = strchr(input_copy2, '>');
      offset = 0;
    }

    char *cmd_part = input_copy2;
    char *file_part = NULL;

    if (out_redir) {
      *out_redir = 0;
      file_part = out_redir + 1 + offset;
    }

    trim(cmd_part);

    int saved_stdout = -1;
    int fd = -1;
    if (file_part) {
      trim(file_part);
      fd = open(file_part, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd < 0) {
        perror("open");
        continue;
      }

      saved_stdout = dup(STDOUT_FILENO);
      dup2(fd, STDOUT_FILENO);
      close(fd);
    }

    char *input_copy = strdup(input);
    // char *cmd = input_copy;
    // char *rest = NULL;
    // char *space = strchr(input_copy, ' ');
    char *cmd = cmd_part;
    char *rest = NULL;
    char *space = strchr(cmd_part, ' ');

    if (space) {
      *space = 0;
      rest = space + 1;
    }

    if (strcmp(cmd, "exit") == 0) {
      break;
    } else if (strcmp(cmd, "echo") == 0) {
      //     if (file_part) {
      //       trim(file_part);
      // trim(cmd_part);
      //       char *cmd_args = strchr(cmd_part, ' ');
      //       if (cmd_args)
      //         cmd_args++;
      //       else
      //         cmd_args = "";
      //       trim(cmd_args);
      //       FILE *fp = fopen(file_part, "w");
      //       if (fp == NULL) {
      //         perror("fopen");
      //   continue;
      //       }
      //       fprintf(fp, "%s\n", cmd_args);
      //       fclose(fp);
      //     } else {
      printf("%s\n", rest ? rest : "");
      // }
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

    } else if (strcmp(cmd, "pwd") == 0) {

      char *cwd = getcwd(NULL, 0);
      if (!cwd) {
        perror("getcwd");
        continue;
      }

      printf("%s\n", cwd);

      free(cwd);
    } else if (strcmp(cmd, "cd") == 0) {
      char *copy = strdup(rest ? rest : "");

      char *dir = strtok(copy, " ");

      if (!dir || strcmp(dir, "~") == 0) {
        dir = getenv("HOME");
      }

      if (chdir(dir) != 0) {
        fprintf(stderr, "cd: %s: No such file or directory\n", dir);
      }

      free(copy);
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
        printf("%s: command not found\n", input_copy);
      }
    } else {
      printf("%s: command not found\n", input_copy);
    }
    if (file_part) {
      fflush(stdout);
      dup2(saved_stdout, STDOUT_FILENO);
      close(saved_stdout);
    }
    free(input_copy);
    free(input_copy2);
  }
  free(input);

  return 0;
}
