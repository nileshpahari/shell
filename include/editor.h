#ifndef EDITOR_H
#define EDITOR_H

#include <stddef.h>
char *editor_readline(const char *prompt);

void editor_clear_screen(void);

typedef struct {
  char **cvec;
  size_t len;
} editorCompletions;

typedef void(editorCompletionCallback)(const char *, editorCompletions *);

void editor_set_completion_callback(editorCompletionCallback *fn);
void editor_add_completion(editorCompletions *lc, const char *str);

#endif
