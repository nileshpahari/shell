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

char *build_prompt(void) {
  char cwd[4096];

  if (!getcwd(cwd, sizeof(cwd))) {
    perror("getcwd");
    return strdup("> ");
  }

  char prompt[8192];

  char *home = getenv("HOME");

  if (home && strncmp(cwd, home, strlen(home)) == 0) {
    snprintf(prompt, sizeof(prompt),
             "\r\n" DIR_COLOR "~%s" PROMPT_RESET "\r\n" PROMPT_COLOR
             ">" PROMPT_RESET " ",
             cwd + strlen(home));
  } else {
    snprintf(prompt, sizeof(prompt),
             "\r\n" DIR_COLOR "%s" PROMPT_RESET "\r\n" PROMPT_COLOR ">" PROMPT_RESET
             " ",
             cwd);
  }

  return strdup(prompt);
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

int is_all_spaces(const char *s) {
  while (*s) {
    if (!isspace((unsigned char)*s))
      return 0;
    s++;
  } return 1;
}
