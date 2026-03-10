#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../include/helpers.h"

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

// int apply_redirection(command_t *cmd)
// {
//     if (!cmd->out_file)
//         return -1;
//
//     int fd = open(cmd->out_file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
//     dup2(fd, STDOUT_FILENO);
//     close(fd);
// }
