#ifndef HISTORY_H
#define HISTORY_H

#define HISTORY_FILE ".shell_history"
#define HISTORY_NEXT 0
#define HISTORY_PREVIOUS 1

char *get_history_path(void);
int history_add(const char *line);
void history_set_max_len(int len);
const char *history_move(const char *current_line, int *index, int direction);
void history_remove_last(void);
int history_save(const char *filename);
int history_load(const char *filename);
void history_free(void);

#endif
