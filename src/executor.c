#include "../include/helpers.h"
#include "../include/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../include/executor.h"

void execute_command(command_t cmd) {
  char *path = find_in_path(cmd.argv[0]);

  if (!path) {
    printf("%s: command not found\n", cmd.argv[0]);
    return;
  }

  pid_t pid = fork();

  if (pid == 0) {
    execv(path, cmd.argv);
    perror("execv");
    exit(1);
  }

  wait(NULL);
}
