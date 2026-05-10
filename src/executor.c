#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../include/builtins.h"
#include "../include/executor.h"
#include "../include/helpers.h"
#include "../include/parser.h"

void execute_command(command_t cmd) {
  // TODO: Probably just use execvp instead of execv
  char *path = find_in_path(cmd.argv[0]);

  if (!path) {
    printf("%s: command not found\n", cmd.argv[0]);
    return;
  }

  pid_t pid = fork();

  if (pid == 0) {
    if (apply_redirection(cmd) == -1) {
      exit(1);
    }
    execv(path, cmd.argv);
    perror("execv");
    exit(1);
  }

  wait(NULL);
  free(path);
}

void execute(pipeline_t pipeline) {
  size_t n = pipeline.count;
  if (n == 0) {
	  return;
  }

  if (n == 1) {
    command_t cmd = pipeline.commands[0];
    if (cmd.argc == 0)
      return;
    if (handle_builtin(cmd))
      return;
    execute_command(cmd);
    return;
  }

  // Multiple commands: create n-1 pipes
  // pipes[i] connects command i's stdout to command i+1's stdin
  int (*pipes)[2] = malloc((n - 1) * sizeof(int[2]));
  if (!pipes) {
    perror("malloc");
    return;
  }

  for (size_t i = 0; i < n - 1; ++i) {
    if (pipe(pipes[i]) == -1) {
      perror("pipe");
      // Close any pipes already created
      for (size_t j = 0; j < i; ++j) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      free(pipes);
      return;
    }
  }

  pid_t *pids = malloc(n * sizeof(pid_t));
  if (!pids) {
    perror("malloc");
    for (size_t i = 0; i < n - 1; ++i) {
      close(pipes[i][0]);
      close(pipes[i][1]);
    }
    free(pipes);
    return;
  }

  for (size_t i = 0; i < n; ++i) {
    command_t cmd = pipeline.commands[i];

    pid_t pid = fork();
    if (pid == -1) {
      perror("fork");
      pids[i] = -1;
      continue;
    }

    if (pid == 0) {
      // Child process

      // Wire stdin from previous pipe (unless first command)
      if (i > 0) {
        dup2(pipes[i - 1][0], STDIN_FILENO);
      }

      // Wire stdout to next pipe (unless last command)
      if (i < n - 1) {
        dup2(pipes[i][1], STDOUT_FILENO);
      }

      // Close all pipe fds in the child
      for (size_t j = 0; j < n - 1; ++j) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }

      // Apply any per-command redirections (e.g. 2> file)
      if (apply_redirection(cmd) == -1) {
		exit(1);
	  }

      // If it's a builtin, run it directly in this child
      if (cmd.argc > 0 && is_builtin(cmd.argv[0])) {
        execute_builtin_in_child(cmd);
        fflush(stdout);
        _exit(0);
      }

      // External command
      if (cmd.argc > 0) {
        char *path = find_in_path(cmd.argv[0]);
        if (!path) {
          fprintf(stderr, "%s: command not found\n", cmd.argv[0]);
          _exit(127);
        }
        execv(path, cmd.argv);
        perror("execv");
        _exit(1);
      }

      _exit(0);
    }

    pids[i] = pid;
  }

  // Parent: close all pipe fds
  for (size_t i = 0; i < n - 1; ++i) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }

  // Wait for all children
  for (size_t i = 0; i < n; ++i) {
    if (pids[i] > 0) {
      waitpid(pids[i], NULL, 0);
    }
  }

  free(pipes);
  free(pids);
}
