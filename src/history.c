#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../include/history.h"

#define HISTORY_DEFAULT_MAX_LEN 100

static int history_max_len = HISTORY_DEFAULT_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

static char *history_read_line(FILE *fp) {
  char *line = NULL;
  size_t len = 0, cap = 0;

  while (1) {
    if (len + 1 >= cap) {
      size_t newcap = cap ? cap * 2 : 16;
      char *tmp = realloc(line, newcap);
      if (!tmp) {
        free(line);
        return NULL;
      }
      line = tmp;
      cap = newcap;
    }

    int c = fgetc(fp);
    if (c == EOF || c == '\n') {
      if (c == EOF && len == 0) {
        free(line);
        return NULL;
      }
      line[len] = '\0';
      return line;
    }
    line[len++] = (char)c;
  }
}

char *get_history_path(void) {
  char *home = getenv("HOME");
  if (!home)
    return NULL;

  size_t len = strlen(home) + 1 + strlen(HISTORY_FILE) + 1;
  char *path = malloc(len);
  if (!path)
    return NULL;
  snprintf(path, len, "%s/%s", home, HISTORY_FILE);
  return path;
}

int history_add(const char *line) {
  if (history_max_len == 0) return 0;

  if (!history) {
    history = calloc((size_t)history_max_len, sizeof(*history));
    if (!history) return 0;
  }

  if (history_len && !strcmp(history[history_len - 1], line)) return 0;

  char *copy = strdup(line);
  if (!copy) return 0;

  if (history_len == history_max_len) {
    free(history[0]);
    memmove(history, history + 1,
            sizeof(*history) * (size_t)(history_max_len - 1));
    history_len--;
  }
  history[history_len++] = copy;
  return 1;
}

void history_set_max_len(int len) {
  if (len < 1) return;

  char **resized = calloc((size_t)len, sizeof(*resized));
  if (!resized) return;

  int keep = history_len < len ? history_len : len;
  for (int i = 0; i < history_len - keep; i++) free(history[i]);
  if (keep) {
    memcpy(resized, history + history_len - keep, sizeof(*history) * (size_t)keep);
  }
  free(history);
  history = resized;
  history_len = keep;
  history_max_len = len;
}

const char *history_move(const char *current_line, int *index, int direction) {
  if (history_len <= 1) return NULL;

  int next = *index + (direction == HISTORY_PREVIOUS ? 1 : -1);
  if (next < 0 || next >= history_len) return NULL;

  char *copy = strdup(current_line);
  if (!copy) return NULL;
  int current = history_len - 1 - *index;
  free(history[current]);
  history[current] = copy;
  *index = next;
  return history[history_len - 1 - *index];
}

void history_remove_last(void) {
  if (!history_len) return;
  free(history[--history_len]);
  history[history_len] = NULL;
}

int history_save(const char *filename) {
  mode_t old_umask = umask(S_IXUSR | S_IRWXG | S_IRWXO);
  FILE *fp = fopen(filename, "w");
  umask(old_umask);
  if (!fp) return -1;

  fchmod(fileno(fp), S_IRUSR | S_IWUSR);
  for (int i = 0; i < history_len; i++) {
    for (char *p = history[i]; *p; p++) fputc(*p == '\n' ? '\r' : *p, fp);
    fputc('\n', fp);
  }
  fclose(fp);
  return 0;
}

int history_load(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) return -1;

  char *line;
  while ((line = history_read_line(fp))) {
    for (size_t i = 0; line[i]; i++) {
      if (line[i] == '\r') line[i] = '\n';
    }
    history_add(line);
    free(line);
  }
  fclose(fp);
  return 0;
}

void history_free(void) {
  for (int i = 0; i < history_len; i++) free(history[i]);
  free(history);
  history = NULL;
  history_len = 0;
}
