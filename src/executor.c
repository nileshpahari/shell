#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../include/executor.h"
#include "../include/parser.h"
#include "../include/helpers.h"

void execute_command(command_t cmd) {
  // TODO: Probably just use execvp instead of execv
  char *path = find_in_path(cmd.argv[0]);

  if (!path) {
    printf("%s: command not found\n", cmd.argv[0]);
    return;
  }

  pid_t pid = fork();

  if (pid == 0) {
    apply_redirection(cmd);
    execv(path, cmd.argv);
    perror("execv");
    exit(1);
  }

  wait(NULL);
  free(path);
}
