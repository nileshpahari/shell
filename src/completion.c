#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/builtins.h"
#include "../include/editor.h"

void completion(const char *buf, editorCompletions *lc) {
  size_t len = strlen(buf);
  if (len == 0)
    return;

  const char *last_space = strrchr(buf, ' ');
  int is_first_word = (last_space == NULL);
  const char *prefix = is_first_word ? buf : last_space + 1;
  size_t plen = strlen(prefix);

  if (plen == 0)
    return;

  if (is_first_word) {
    size_t count;
    const char **names = get_builtin_names(&count);
    for (size_t i = 0; i < count; i++) {
      if (strncmp(names[i], prefix, plen) == 0) {
        editor_add_completion(lc, names[i]);
      }
    }

    char *path = getenv("PATH");
    if (path) {
      char *path_copy = strdup(path);
      if (path_copy) {
        char *dir = strtok(path_copy, ":");
        while (dir) {
          DIR *dp = opendir(dir);
          if (dp) {
            struct dirent *entry;
            while ((entry = readdir(dp)) != NULL) {
              if (strncmp(entry->d_name, prefix, plen) == 0) {
                int dup = 0;
                for (size_t j = 0; j < lc->len; j++) {
                  if (strcmp(lc->cvec[j], entry->d_name) == 0) {
                    dup = 1;
                    break;
                  }
                }
                if (!dup) {
                  char full[1024];
                  snprintf(full, sizeof(full), "%s/%s", dir, entry->d_name);
                  if (access(full, X_OK) == 0) {
                    editor_add_completion(lc, entry->d_name);
                  }
                }
              }
            }
            closedir(dp);
          }
          dir = strtok(NULL, ":");
        }
        free(path_copy);
      }
    }
  } else {
    DIR *dp = opendir(".");
    if (dp) {
      struct dirent *entry;
      while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.' && prefix[0] != '.')
          continue;
        if (strncmp(entry->d_name, prefix, plen) == 0) {
          size_t head_len = (size_t)(prefix - buf);
          size_t name_len = strlen(entry->d_name);
          char *full = malloc(head_len + name_len + 1);
          if (full) {
            memcpy(full, buf, head_len);
            memcpy(full + head_len, entry->d_name, name_len);
            full[head_len + name_len] = '\0';
            editor_add_completion(lc, full);
            free(full);
          }
        }
      }
      closedir(dp);
    }
  }
}
