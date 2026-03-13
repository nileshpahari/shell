#include "../include/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/helpers.h"

static const char *cmds[] = {"exit", "echo", "type", "pwd", "cd"};
static const int cmds_len = 5;

static void builtin_echo(command_t cmd) {
  for (size_t i = 1; i < cmd.argc; ++i) {
    printf("%s", cmd.argv[i]);
    if (i + 1 < cmd.argc)
      printf(" ");
  }
  printf("\n");
}

static void builtin_type(command_t cmd) {

  if (cmd.argc == 1) {
    printf("type: missing argument\n");
    return;
  }

  char *path = getenv("PATH");

  for (size_t i = 1; i < cmd.argc; ++i) {
    char *curr = cmd.argv[i];
    int flag = 0;

    for (size_t i = 0; i < cmds_len; ++i) {
      if (strcmp(cmds[i], curr) == 0) {
        flag = 1;
        break;
      }
    }

    char *resolved_path = NULL;

    if (!flag && path) {
      resolved_path = find_in_path(curr);
      if (resolved_path) {
        flag = 2;
      }
    }

    if (flag == 1) {
      printf("%s is a shell builtin\n", curr);
    } else if (flag == 2) {
      printf("%s is %s\n", curr, resolved_path);
      free(resolved_path);
    } else {
      printf("%s: not found\n", curr);
    }
  }
}

static void builtin_pwd() {
  char *cwd = getcwd(NULL, 0);
  if (!cwd) {
    perror("getcwd");
    return;
  }

  printf("%s\n", cwd);

  free(cwd);
}

static void builtin_cd(command_t cmd) {
  if (cmd.argc > 2) {
    fprintf(stderr, "cd: too many arguments\n");
    return;
  }

  char *dir = cmd.argv[1];

  if (!dir || strcmp(dir, "~") == 0) {
    dir = getenv("HOME");
  }

  if (chdir(dir) != 0) {
    fprintf(stderr, "cd: %s: No such file or directory\n", dir);
  }
}

int handle_builtin(command_t cmd) {
  int saved = dup(STDOUT_FILENO);
  if (saved == -1)
    return 0;

  int status = 0;

  for (size_t i = 0; i < cmds_len; ++i) {
    if (strcmp(cmds[i], cmd.argv[0]) == 0) {
      status = 1;
      break;
    }
  }

  if (!status) {
    dup2(saved, STDOUT_FILENO);
    close(saved);
    return 0;
  }

  if (apply_redirection(cmd) == -1) {
    dup2(saved, STDOUT_FILENO);
    close(saved);
    return 1;
  }

  if (strcmp(cmd.argv[0], "exit") == 0) {
    dup2(saved, STDOUT_FILENO);
    close(saved);
    exit(0);
  } else if (strcmp(cmd.argv[0], "echo") == 0)
    builtin_echo(cmd);
  else if (strcmp(cmd.argv[0], "type") == 0)
    builtin_type(cmd);
  else if (strcmp(cmd.argv[0], "pwd") == 0)
    builtin_pwd();
  else if (strcmp(cmd.argv[0], "cd") == 0)
    builtin_cd(cmd);

  dup2(saved, STDOUT_FILENO);
  close(saved);
  return 1;
}
