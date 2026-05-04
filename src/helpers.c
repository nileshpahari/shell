#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/helpers.h"
#include "../include/parser.h"

#define DIR_COLOR "\x1b[1;34m"
#define PROMPT_COLOR "\x1b[1;32m"
#define PROMPT_RESET "\x1b[0m"

void print_prompt(void) {
  char cwd[4096];

  if (!getcwd(cwd, sizeof(cwd))) {
    perror("getcwd");
  }

  char *home = getenv("HOME");
  if (home && strncmp(cwd, home, strlen(home)) == 0) {
    printf("\n" DIR_COLOR "~%s" PROMPT_RESET "\n", cwd + strlen(home));
  } else {
    printf("\n" DIR_COLOR "%s" PROMPT_RESET "\n", cwd);
  }

  printf(PROMPT_COLOR ">" PROMPT_RESET " ");
  fflush(stdout);
}

char *find_in_path(const char *cmd) {
  if (!cmd)
    return NULL;

  char *path = getenv("PATH");
  if (!path)
    return NULL;

  char *path_copy = strdup(path);
  if (!path_copy)
    return NULL;

  char *dir = strtok(path_copy, PATH_LIST_SEPARATOR);

  while (dir) {
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", dir, cmd);

    if (access(full, X_OK) == 0) {
      char *result = strdup(full);
      free(path_copy);
      return result;
    }

    dir = strtok(NULL, PATH_LIST_SEPARATOR);
  }

  free(path_copy);
  return NULL;
}

int apply_redirection(command_t cmd) {
  if (!cmd.redir_out)
    return 0;

  int fd = open(cmd.redir_out,
                O_WRONLY | O_CREAT | (cmd.append ? O_APPEND : O_TRUNC), 0644);
  if (fd == -1) {
    perror("open");
    return -1;
  }

  if (dup2(fd, cmd.redir_out_fd) == -1) {
    perror("dup2");
    close(fd);
    return -1;
  }

  close(fd);
  return 1;
}

int is_number(const char *s) {
  if (!*s)
    return 0;

  while (*s) {
    if (*s < '0' || *s > '9')
      return 0;
    s++;
  }
  return 1;
}
