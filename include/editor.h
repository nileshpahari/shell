/*
 * editor.h -- Custom line editor interface.
 *
 * A simplified line editor adapted from the linenoise library
 * (http://github.com/antirez/linenoise) by Salvatore Sanfilippo.
 * Provides interactive line editing with history, cursor movement,
 * and UTF-8 support without relying on external libraries like readline.
 */

#ifndef EDITOR_H
#define EDITOR_H

/* Read a line of input with interactive editing support.
 * Displays 'prompt' and returns a malloc'd string on success (caller must
 * free). Returns NULL on EOF (Ctrl-D on empty line, errno == ENOENT),
 * on Ctrl-C (errno == EAGAIN), or on I/O error. */
char *editor_readline(const char *prompt);

/* Add a line to the history ring buffer. Duplicate consecutive entries
 * are silently ignored. Returns 1 on success, 0 on failure. */
int editor_history_add(const char *line);

/* Set the maximum number of history entries retained (default 100). */
void editor_history_set_max_len(int len);

/* Clear the terminal screen. */
void editor_clear_screen(void);

#endif /* EDITOR_H */
