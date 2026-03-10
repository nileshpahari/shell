#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
  TOKEN_WORD,
  TOKEN_STRING,
  TOKEN_REDIRECT_OUT,
  TOKEN_REDIRECT_APPEND,
  TOKEN_REDIRECT_IN,
  TOKEN_PIPE,
  TOKEN_EOF
} token_type;

typedef struct {
  token_type type;
  char *value;
} token;

typedef struct {
  token *tokens;
  size_t count;
} token_list;

token_list lex(char *input);

void token_list_free(token_list list);

#endif
