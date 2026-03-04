#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  char *input = NULL;
  size_t len = 0;

  while (1) {
    printf("$ ");
    if (getline(&input, &len, stdin) == -1) {
      break;
    }
    input[strcspn(input, "\n")] = 0;
    printf("%s: command not found\n", input);
  }
  free(input);

  return 0;
}
