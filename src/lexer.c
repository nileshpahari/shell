#include "../include/lexer.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void skip_spaces(char **p) {
  while (**p && isspace(**p))
    (*p)++;
}

static char *read_word(char **p) {
  char buffer[4096];
  size_t len = 0;

  while (**p && !isspace(**p) && **p != '|' && **p != '>' && **p != '<') {
    if (**p == '\'' || **p == '"') {
      char quote = **p;
      (*p)++;

      while (**p && **p != quote)
        buffer[len++] = *(*p)++;

      if (**p == quote)
        (*p)++;
    } else {
      buffer[len++] = *(*p)++;
    }
  }

  buffer[len] = '\0';
  return strdup(buffer);
}
// static char *read_word(char **p) {
//   char *start = *p;
//
//   while (**p && !isspace(**p) && **p != '|' && **p != '>' && **p != '<')
//     (*p)++;
//
//   return strndup(start, *p - start);
// }
//
// static char *read_string(char **p) {
//   char quote = **p;
//   (*p)++;
//
//   char *start = *p;
//
//   while (**p && **p != quote)
//     (*p)++;
//
//   char *s = strndup(start, *p - start);
//
//   if (**p == quote)
//     (*p)++;
//
//   return s;
// }

static void add_token(token_list *list, token_type type, char *value) {
  list->tokens = realloc(list->tokens, sizeof(token) * (list->count + 1));

  list->tokens[list->count].type = type;
  list->tokens[list->count].value = value;

  list->count++;
}

token_list lex(char *input) {
  token_list list = {NULL, 0};
  char *p = input;

  while (*p) {
    skip_spaces(&p);

    if (*p == '\0')
      break;

    if (*p == '|') {
      add_token(&list, TOKEN_PIPE, strdup("|"));
      p++;
    } else if (*p == '>') {
      if (*(p + 1) == '>') {
        add_token(&list, TOKEN_REDIRECT_APPEND, strdup(">>"));
        p += 2;
      } else {
        add_token(&list, TOKEN_REDIRECT_OUT, strdup(">"));
        p++;
      }
    } else if (*p == '<') {
      add_token(&list, TOKEN_REDIRECT_IN, strdup("<"));
      p++;
    // } else if (*p == '"' || *p == '\'') {
    //   char *s = read_string(&p);
    //   add_token(&list, TOKEN_STRING, s);
    } else {
      char *w = read_word(&p);
      add_token(&list, TOKEN_WORD, w);
    }
  }

  add_token(&list, TOKEN_EOF, NULL);

  return list;
}

void token_list_free(token_list list) {
  for (size_t i = 0; i < list.count; ++i) {
    free(list.tokens[i].value);
  }

  free(list.tokens);
}

